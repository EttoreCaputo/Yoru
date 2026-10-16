#include "life.h"

#include <math.h>
#include <string.h>

#include "behaviour.h"
#include "board.h"
#include "esp_log.h"
#include "esp_random.h"
#include "eyes.h"
#include "lvgl.h"
#include "needs.h"
#include "pet.h"
#include "playback.h"

static const char *TAG = "life";

/* The screen's radius and Yoru's at the resting distance, in pixels. */
#define SCREEN_R (BOARD_LCD_H_RES / 2)
#define BODY_R 98
/* Asking for you: how near it comes and how long it waits for an answer. */
#define ASK_Z 0.62f
#define ASK_WAIT_MS 11000
/* Asleep, how often it thinks about waking up. */
#define WAKE_CHECK_MS 60000
/* Rested enough to wake on its own, and with someone around after a nap of
 * this long. */
#define RESTED 0.97f
#define RESTED_COMPANY 0.6f
#define NAP_MIN_MS (20 * 60 * 1000)

typedef enum {
    ACT_LOUNGE,   // floating about near you
    ACT_EXPLORE,  // off into the distance, to the edges, and back
    ACT_FORAGE,   // hunting a firefly by itself
    ACT_SNACK,    // a firefly you gave it
    ACT_ASK,      // up at the glass, asking for you
    ACT_BOUNCE,   // hops along the bottom of the screen
    ACT_PEEKABOO, // hides past the edge, peeks back from another
    ACT_LOOP,     // a loop through the space, near and far
    ACT_DAYDREAM, // looking up, lost in thought
    ACT_SULK,     // by the edge, turned away
    ACT_DANCE,    // to the music
    ACT_NAP,      // yawns, finds a corner, settles down
    ACT_SLEEP,
    ACT_WAKE,     // stretches and comes back
    ACT_COME,     // called: back to you
    ACT_COUNT,
} act_t;

static const char *const NAMES[ACT_COUNT] = {
    "lounge", "explore", "forage", "snack", "ask",  "bounce", "peekaboo", "loop",
    "daydream", "sulk", "dance", "nap", "sleep", "wake", "come",
};

static const needs_doing_t DOING[ACT_COUNT] = {
    [ACT_LOUNGE] = NEEDS_CALM,  [ACT_EXPLORE] = NEEDS_BUSY,  [ACT_FORAGE] = NEEDS_BUSY,
    [ACT_SNACK] = NEEDS_BUSY,   [ACT_ASK] = NEEDS_BUSY,      [ACT_BOUNCE] = NEEDS_PLAY,
    [ACT_PEEKABOO] = NEEDS_PLAY, [ACT_LOOP] = NEEDS_PLAY,    [ACT_DAYDREAM] = NEEDS_CALM,
    [ACT_SULK] = NEEDS_CALM,    [ACT_DANCE] = NEEDS_PLAY,    [ACT_NAP] = NEEDS_CALM,
    [ACT_SLEEP] = NEEDS_ASLEEP, [ACT_WAKE] = NEEDS_CALM,     [ACT_COME] = NEEDS_CALM,
};

/* The least time between two of the same, in seconds. */
static const uint16_t COOLDOWN_S[ACT_COUNT] = {
    [ACT_EXPLORE] = 60, [ACT_FORAGE] = 30,   [ACT_ASK] = 240,     [ACT_BOUNCE] = 90, [ACT_PEEKABOO] = 120,
    [ACT_LOOP] = 120,   [ACT_DAYDREAM] = 60, [ACT_SULK] = 180,    [ACT_DANCE] = 20,
};

/* Where the eyes rest in each of the pet's moods. The cheerful ones rest
 * neutral: happy eyes all the time would mean nothing. */
static const eyes_mood_t RESTING[PET_MOOD_COUNT] = {
    [PET_MOOD_CONTENT] = EYES_MOOD_NEUTRAL, [PET_MOOD_HAPPY] = EYES_MOOD_NEUTRAL,
    [PET_MOOD_EXCITED] = EYES_MOOD_NEUTRAL, [PET_MOOD_LOVING] = EYES_MOOD_NEUTRAL,
    [PET_MOOD_BORED] = EYES_MOOD_BORED,     [PET_MOOD_GRUMPY] = EYES_MOOD_GRUMPY,
    [PET_MOOD_SAD] = EYES_MOOD_SAD,         [PET_MOOD_SLEEPY] = EYES_MOOD_SLEEPY,
    [PET_MOOD_HOT] = EYES_MOOD_HOT,         [PET_MOOD_HUNGRY] = EYES_MOOD_SAD,
};

static struct {
    life_ctx_t ctx;
    uint32_t now;
    act_t act;
    int phase;
    uint32_t act_ms;     // when the act started
    uint32_t act_len_ms; // how long it goes on, for the open-ended ones
    uint32_t wait_until;
    int count, total;
    int side;
    /* The eyes: the act's own mood, and a passing one over it until then. */
    eyes_mood_t base, passing;
    uint32_t passing_until;
    uint32_t next_ok[ACT_COUNT];
    /* Asks nobody answered, in a row, and whether this one was. */
    int ignored;
    bool answered;
    need_t ask_for;
    uint32_t next_wake_check;
} L;

static float frand(void)
{
    return (float) (esp_random() & 0xFFFF) / 65535.0f;
}

static float frange(float lo, float hi)
{
    return lo + (hi - lo) * frand();
}

static bool coin(float p)
{
    return frand() < p;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* The point at distance `z` that shows (sx, sy) pixels from the middle. */
static eyes_pos_t at(float sx, float sy, float z)
{
    return (eyes_pos_t) {sx * z, sy * z, z};
}

/* How far from the middle Yoru's centre can be at distance `z` and still
 * show whole. */
static float reach(float z)
{
    return fmaxf(0, SCREEN_R - BODY_R / z - 12);
}

/* Somewhere it shows whole, between distances `z0` and `z1`. */
static eyes_pos_t somewhere(float z0, float z1)
{
    const float z = frange(z0, z1), a = frange(0, 2 * (float) M_PI), r = reach(z) * sqrtf(frange(0.15f, 0.9f));
    return at(r * cosf(a), r * sinf(a) * 0.8f, z);
}

static void wait_ms(uint32_t ms)
{
    L.wait_until = L.now + ms;
}

static bool waiting(void)
{
    return (int32_t) (L.now - L.wait_until) < 0;
}

/* Waiting, or still on the way. */
static bool busy(void)
{
    return waiting() || eyes_moving();
}

static void next(int phase)
{
    L.phase = phase;
}

static void say(sound_t sound)
{
    if (!L.ctx.focus) {
        playback_sound(sound);
    }
}

static void face(eyes_mood_t mood, uint32_t ms)
{
    L.passing = mood;
    L.passing_until = L.now + ms;
    eyes_set_mood(mood);
}

static eyes_mood_t resting(void)
{
    if (L.ctx.drowsy) {
        return EYES_MOOD_SLEEPY;
    }
    const pet_mood_t m = pet_mood();
    return m < PET_MOOD_COUNT ? RESTING[m] : EYES_MOOD_NEUTRAL;
}

static void home(uint32_t ms, eyes_path_t path)
{
    eyes_move_to(0, 0, 1, ms, path);
}

/* Look from where it is towards a point in the space. */
static void look_towards(eyes_pos_t p)
{
    const eyes_pos_t me = eyes_where();
    const float dx = p.x / p.z - me.x / me.z, dy = p.y / p.z - me.y / me.z;
    eyes_look_at(clampf(dx / 150, -1, 1), clampf(dy / 150, -1, 1));
}

static void start(act_t act)
{
    if (L.act != act || act == ACT_LOUNGE) {
        ESP_LOGI(TAG, "%s -> %s", NAMES[L.act], NAMES[act]);
    }
    L.act = act;
    L.phase = 0;
    L.count = 0;
    L.act_ms = L.now;
    L.wait_until = L.now;
    L.base = resting();
    L.passing_until = L.now;
    needs_doing(DOING[act]);
    eyes_look_around();
}

/* ---- Choosing what to do next -------------------------------------------------- */

/* How much each act would do for it now: the more pressing the need it
 * meets, the higher; 0 when it can't or shouldn't. */
static float score(act_t a, const needs_t *n, const pet_state_t *p)
{
    const life_ctx_t *c = &L.ctx;
    const float u_rest = 1 - n->level[NEED_REST], u_food = 1 - n->level[NEED_FOOD];
    const float u_fun = 1 - n->level[NEED_FUN], u_company = 1 - n->level[NEED_COMPANY];
    const float u_cur = 1 - n->level[NEED_CURIOSITY];
    const float live = 0.4f + 0.6f * p->energy;
    const bool lively = !c->drowsy && !c->focus;
    switch (a) {
    case ACT_LOUNGE:
        return 0.35f + (c->drowsy ? 0.6f : 0) + (c->focus ? 0.5f : 0);
    case ACT_EXPLORE:
        return lively && n->level[NEED_REST] > 0.25f ? (0.25f + 1.3f * u_cur * u_cur) * live : 0;
    case ACT_FORAGE: {
        if (u_food < 0.4f) {
            return 0;
        }
        // With you there, it would rather ask, unless you keep ignoring it.
        const bool would_ask = c->company && !c->focus && L.ignored < 2 && u_food < 0.75f;
        return 1.8f * u_food * u_food * (would_ask ? 0.4f : 1);
    }
    case ACT_ASK: {
        if (!c->company || !lively) {
            return 0;
        }
        const float m = fmaxf(fmaxf(u_food - 0.4f, u_company - 0.35f), u_fun - 0.5f);
        return m > 0 ? 4 * m * m + 0.1f : 0;
    }
    case ACT_BOUNCE:
    case ACT_PEEKABOO:
    case ACT_LOOP: {
        if (!lively || n->level[NEED_REST] < 0.3f) {
            return 0;
        }
        const float k = a == ACT_BOUNCE ? 0.9f : a == ACT_PEEKABOO ? 0.8f : 0.7f;
        // A lively Yoru plays now and then even when it isn't bored.
        return (0.2f + 1.1f * u_fun * u_fun) * live * k;
    }
    case ACT_DAYDREAM:
        return 0.1f + 0.15f * p->happiness * (u_cur < 0.5f ? 1 : 0.3f);
    case ACT_SULK:
        return p->happiness < 0.38f && !c->focus ? 0.5f + 0.15f * (float) L.ignored : 0;
    case ACT_DANCE:
        return c->music && lively && n->level[NEED_REST] > 0.3f ? 1.4f : 0;
    case ACT_NAP: {
        float s = n->level[NEED_REST] < 0.45f ? 0.6f + 3 * (0.45f - n->level[NEED_REST]) : 0;
        if (c->night && !c->company) {
            s += 1.0f;
        }
        return s + (c->drowsy && u_rest > 0.3f ? 0.3f : 0);
    }
    default:
        return 0;
    }
}

static void choose(void)
{
    if (COOLDOWN_S[L.act] > 0) {
        L.next_ok[L.act] = L.now + (uint32_t) COOLDOWN_S[L.act] * 1000
                           + (L.act == ACT_ASK ? (uint32_t) L.ignored * 120000 : 0);
    }
    needs_t n;
    needs_get_all(&n);
    pet_state_t p;
    pet_get(&p);
    static const act_t CHOICES[] = {ACT_LOUNGE, ACT_EXPLORE, ACT_FORAGE, ACT_ASK,  ACT_BOUNCE, ACT_PEEKABOO,
                                    ACT_LOOP,   ACT_DAYDREAM, ACT_SULK,  ACT_DANCE, ACT_NAP};
    const int count = (int) (sizeof(CHOICES) / sizeof(CHOICES[0]));
    float w[sizeof(CHOICES) / sizeof(CHOICES[0])];
    float total = 0;
    for (int i = 0; i < count; i++) {
        const act_t a = CHOICES[i];
        const bool cooling = a != ACT_LOUNGE && (int32_t) (L.now - L.next_ok[a]) < 0;
        // Not the same twice in a row, but for floating about.
        const bool again = a == L.act && a != ACT_LOUNGE;
        const float s = cooling || again ? 0 : score(a, &n, &p);
        // Squared: what matters most wins more often, but not always.
        w[i] = s * s;
        total += w[i];
    }
    float r = frand() * total;
    act_t pick = ACT_LOUNGE;
    for (int i = 0; i < count && total > 0; i++) {
        r -= w[i];
        if (r <= 0) {
            pick = CHOICES[i];
            break;
        }
    }
    ESP_LOGI(TAG, "rest %.2f food %.2f fun %.2f company %.2f curiosity %.2f, %s%s", n.level[NEED_REST],
             n.level[NEED_FOOD], n.level[NEED_FUN], n.level[NEED_COMPANY], n.level[NEED_CURIOSITY],
             L.ctx.company ? "company" : "alone", L.ctx.drowsy ? ", drowsy" : "");
    start(pick);
}

/* ---- What it does ---------------------------------------------------------------- */

static void lounge(void)
{
    L.base = resting();
    if (L.phase == 0) {
        L.act_len_ms = (uint32_t) (L.ctx.drowsy || L.ctx.focus ? frange(30000, 70000) : frange(15000, 40000));
        next(1);
    }
    if (busy()) {
        return;
    }
    if (L.now - L.act_ms >= L.act_len_ms) {
        choose();
        return;
    }
    // A little way off, and a pause there.
    const bool calm = L.ctx.drowsy || L.ctx.focus;
    const eyes_pos_t p = calm ? at(frange(-35, 35), frange(0, 30), frange(1.0f, 1.25f))
                              : at(frange(-65, 65), frange(-28, 40), frange(0.88f, 1.3f));
    const uint32_t ms = (uint32_t) frange(2600, 4600) * (calm ? 3 : 2) / 2;
    eyes_move_to(p.x, p.y, p.z, ms, EYES_PATH_DRIFT);
    wait_ms(ms + (uint32_t) frange(2500, 7000));
    if (L.count++ > 0 && !calm && coin(0.35f) && !pet_quiet()) {
        behaviour_whim(pet_idle_scene());
    }
}

static void explore(void)
{
    static eyes_pos_t s_peek;
    static bool s_peeking;
    switch (L.phase) {
    case 0:
        L.base = EYES_MOOD_NEUTRAL;
        L.total = 3 + (int) (esp_random() % 3);
        next(1);
        break;
    case 1:
        if (busy()) {
            return;
        }
        if (L.count >= L.total) {
            home(2800, EYES_PATH_GLIDE);
            next(3);
            return;
        }
        s_peeking = L.count > 0 && coin(0.25f);
        eyes_pos_t to;
        if (s_peeking) {
            // Up to the rim, half past it: what's out there?
            const float a = frange(0, 2 * (float) M_PI);
            to = at(190 * cosf(a), 190 * sinf(a), 1);
            s_peek = (eyes_pos_t) {cosf(a), sinf(a), 1};
        } else {
            to = somewhere(1.6f, 3.6f);
        }
        const eyes_pos_t me = eyes_where();
        const uint32_t ms = (uint32_t) (2200 + 900 * fabsf(to.z - me.z) + frange(0, 900));
        eyes_move_to(to.x, to.y, to.z, ms, EYES_PATH_GLIDE);
        L.count++;
        next(2);
        break;
    case 2:
        if (eyes_moving()) {
            return;
        }
        face(EYES_MOOD_CURIOUS, 1800);
        if (s_peeking) {
            eyes_look_at(s_peek.x, s_peek.y);
        } else {
            eyes_look_at(frange(-0.8f, 0.8f), frange(-0.5f, 0.5f));
            if (coin(0.3f)) {
                eyes_gesture(EYES_GESTURE_POINT);
                say(SOUND_HMM);
            }
        }
        wait_ms((uint32_t) frange(1800, 3200));
        next(4);
        break;
    case 4:
        if (busy()) {
            return;
        }
        eyes_look_around();
        next(1);
        break;
    case 3:
        if (busy()) {
            return;
        }
        needs_meet(NEED_CURIOSITY, 0.3f);
        needs_meet(NEED_FUN, 0.08f);
        choose();
        break;
    }
}

/* The catch, for the hunt and for a snack: pounce, gulp, and a happy moment. */
static void gulp(eyes_mood_t mood, sound_t sound)
{
    eyes_mote_eat();
    face(mood, 2400);
    say(sound);
    needs_meet(NEED_FOOD, 0.33f);
    needs_meet(NEED_FUN, 0.06f);
    wait_ms(2400);
}

static void forage(void)
{
    eyes_pos_t mote;
    switch (L.phase) {
    case 0: {
        // A firefly far off in the dark: it notices.
        const eyes_pos_t p = somewhere(3.0f, 3.4f);
        eyes_mote_show(p);
        L.base = EYES_MOOD_NEUTRAL;
        L.total = 2 + (int) (esp_random() % 2);
        wait_ms(900);
        next(1);
        break;
    }
    case 1:
        if (busy()) {
            return;
        }
        eyes_mote(&mote);
        face(EYES_MOOD_SURPRISED, 900);
        eyes_blink();
        look_towards(mote);
        say(SOUND_HMM);
        wait_ms(1200);
        next(2);
        break;
    case 2: {
        if (waiting()) {
            return;
        }
        // Creeping up on it.
        eyes_mote(&mote);
        const eyes_pos_t me = eyes_where();
        eyes_move_to(me.x + (mote.x - me.x) * 0.6f, me.y + (mote.y - me.y) * 0.6f, me.z + (mote.z - me.z) * 0.6f,
                     1700, EYES_PATH_GLIDE);
        eyes_look_around();
        wait_ms(1900);
        next(3);
        break;
    }
    case 3:
        if (waiting()) {
            return;
        }
        if (L.count >= L.total) {
            eyes_chase(70); // the pounce
            wait_ms(500);
            next(4);
            return;
        }
        // It flits off, and the chase is on.
        eyes_mote_fly(somewhere(1.3f, 2.8f), 700);
        eyes_chase(16);
        L.count++;
        wait_ms((uint32_t) frange(1300, 2100));
        break;
    case 4:
        if (waiting()) {
            return;
        }
        gulp(EYES_MOOD_HAPPY, coin(0.5f) ? SOUND_SMOOCH : SOUND_CHIRP);
        next(5);
        break;
    case 5:
        if (waiting()) {
            return;
        }
        eyes_move_to(0, 10, 1.05f, 2600, EYES_PATH_DRIFT);
        next(6);
        break;
    default:
        if (!busy()) {
            choose();
        }
        break;
    }
}

static void snack(void)
{
    const float food = needs_get(NEED_FOOD);
    eyes_pos_t mote;
    switch (L.phase) {
    case 0:
        if (busy()) {
            return;
        }
        eyes_mote(&mote);
        face(EYES_MOOD_SURPRISED, 700);
        eyes_blink();
        look_towards(mote);
        wait_ms(650);
        next(1);
        break;
    case 1:
        if (waiting()) {
            return;
        }
        if (food < 0.85f) {
            eyes_chase(38);
            wait_ms(1000);
            next(2);
        } else if (food < 0.97f) {
            // Not hungry: it bats it away instead, for fun.
            eyes_mote(&mote);
            eyes_follow(mote.x - 70 * mote.z, mote.y, mote.z, 20);
            eyes_gesture(EYES_GESTURE_POINT);
            wait_ms(1100);
            next(5);
        } else {
            // Full: no thanks.
            eyes_knock(0.22f, 0);
            say(SOUND_NOPE);
            eyes_mote_hide();
            face(EYES_MOOD_BORED, 1500);
            wait_ms(1500);
            next(4);
        }
        break;
    case 2:
        if (waiting()) {
            return;
        }
        // From you: a little more than a snack.
        gulp(pet_mood() == PET_MOOD_LOVING ? EYES_MOOD_LOVING : EYES_MOOD_HAPPY, SOUND_SMOOCH);
        pet_event(PET_EVENT_FED);
        next(3);
        break;
    case 3:
        if (waiting()) {
            return;
        }
        eyes_move_to(0, 5, 1, 2200, EYES_PATH_DRIFT);
        next(4);
        break;
    case 5:
        if (waiting()) {
            return;
        }
        eyes_mote(&mote);
        eyes_mote_fly((eyes_pos_t) {mote.x * 4 + 300, mote.y * 2 - 200, 2.5f}, 900);
        say(SOUND_GIGGLE);
        needs_meet(NEED_FUN, 0.06f);
        face(EYES_MOOD_HAPPY, 1500);
        wait_ms(900);
        next(6);
        break;
    case 6:
        if (waiting()) {
            return;
        }
        eyes_mote_hide();
        eyes_move_to(0, 0, 1, 1800, EYES_PATH_DRIFT);
        next(4);
        break;
    default:
        if (!busy()) {
            choose();
        }
        break;
    }
}

static void ask(void)
{
    switch (L.phase) {
    case 0: {
        const float u_food = 1 - needs_get(NEED_FOOD) - 0.4f, u_company = 1 - needs_get(NEED_COMPANY) - 0.35f;
        const float u_fun = 1 - needs_get(NEED_FUN) - 0.5f;
        L.ask_for = u_food >= u_company && u_food >= u_fun ? NEED_FOOD : u_company >= u_fun ? NEED_COMPANY : NEED_FUN;
        L.base = L.ask_for == NEED_FOOD ? EYES_MOOD_SAD : L.ask_for == NEED_FUN ? EYES_MOOD_BORED : EYES_MOOD_NEUTRAL;
        L.answered = false;
        // Up to the glass, its face filling the screen.
        eyes_move_to(0, 22 * ASK_Z, ASK_Z, 1400, EYES_PATH_GLIDE);
        next(1);
        break;
    }
    case 1:
        if (busy()) {
            return;
        }
        eyes_look_at(0, -0.12f);
        // Knock, knock: two taps on the glass.
        {
            const eyes_pos_t taps[] = {at(0, 22, 0.54f), at(0, 22, ASK_Z), at(0, 22, 0.54f), at(0, 22, ASK_Z)};
            eyes_move(taps, 4, 700, EYES_PATH_GLIDE);
        }
        if (L.ask_for == NEED_FOOD) {
            say(SOUND_WHIMPER);
        } else if (L.ask_for == NEED_COMPANY) {
            eyes_gesture(EYES_GESTURE_WAVE);
            say(SOUND_HELLO);
        } else {
            eyes_gesture(EYES_GESTURE_POINT);
            say(SOUND_CHIRP);
        }
        wait_ms(ASK_WAIT_MS + (uint32_t) frange(0, 4000));
        next(2);
        break;
    case 2:
        if (L.answered) {
            L.ignored = 0;
            face(EYES_MOOD_HAPPY, 1800);
            say(SOUND_HAPPY);
            eyes_look_around();
            eyes_move_to(0, 0, 1, 1600, EYES_PATH_GLIDE);
            wait_ms(1800);
            next(3);
            return;
        }
        if (waiting()) {
            // Hungry, it keeps glancing down at the black beside it: a
            // firefly would go there.
            if (L.ask_for == NEED_FOOD && ((L.now - L.act_ms) / 2500) % 2 == 1) {
                eyes_look_at(0.7f, 0.8f);
            } else {
                eyes_look_at(0, -0.12f);
            }
            return;
        }
        // Nobody came.
        L.ignored++;
        face(EYES_MOOD_SAD, 2600);
        say(SOUND_SIGH);
        needs_meet(NEED_COMPANY, -0.03f);
        eyes_look_around();
        eyes_move_to(0, 0, 1, 2600, EYES_PATH_DRIFT);
        next(3);
        break;
    default:
        if (!busy()) {
            choose();
        }
        break;
    }
}

static void bounce(void)
{
    switch (L.phase) {
    case 0: {
        // Along the bottom of the disc, as if it had a floor.
        const float d = coin(0.5f) ? 1 : -1;
        const eyes_pos_t hops[] = {
            at(-d * 105, 72, 1.15f), at(-d * 35, 78, 1.2f), at(d * 40, 74, 1.15f), at(d * 108, 68, 1.1f), at(0, 0, 1),
        };
        L.base = EYES_MOOD_HAPPY;
        eyes_move(hops, 5, 3300, EYES_PATH_HOP);
        say(SOUND_BOING);
        wait_ms(1650);
        next(1);
        break;
    }
    case 1:
        if (waiting()) {
            return;
        }
        say(SOUND_BOING);
        next(2);
        break;
    default:
        if (busy()) {
            return;
        }
        face(EYES_MOOD_HAPPY, 1200);
        say(SOUND_GIGGLE);
        needs_meet(NEED_FUN, 0.2f);
        choose();
        break;
    }
}

static void peekaboo(void)
{
    switch (L.phase) {
    case 0:
        L.side = coin(0.5f) ? 1 : -1;
        L.base = EYES_MOOD_HAPPY;
        eyes_look_at((float) L.side, 0.1f);
        say(SOUND_MISCHIEF);
        wait_ms(900);
        next(1);
        break;
    case 1:
        if (waiting()) {
            return;
        }
        // Off past the rim, out of sight.
        eyes_move_to((float) L.side * 350, 30, 1, 650, EYES_PATH_DART);
        wait_ms(650 + (uint32_t) frange(1800, 4000));
        next(2);
        break;
    case 2: {
        if (busy()) {
            return;
        }
        // Back from another side, just its face over the rim.
        L.base = EYES_MOOD_CURIOUS;
        if (coin(0.5f)) {
            const float x = (float) -L.side;
            eyes_move_to(x * 350, -10, 1, 0, EYES_PATH_GLIDE);
            eyes_move_to(x * 186, -10, 1, 900, EYES_PATH_GLIDE);
            eyes_look_at(-x, 0.15f);
        } else {
            eyes_move_to(frange(-40, 40), -350, 1, 0, EYES_PATH_GLIDE);
            eyes_move_to(frange(-30, 30), -186, 1, 900, EYES_PATH_GLIDE);
            eyes_look_at(0, 1);
        }
        wait_ms(900 + (uint32_t) frange(1000, 1800));
        next(3);
        break;
    }
    case 3:
        if (waiting()) {
            return;
        }
        // Boo!
        eyes_move_to(0, 0, 0.8f, 450, EYES_PATH_DART);
        face(EYES_MOOD_SURPRISED, 700);
        eyes_look_around();
        say(SOUND_BOO);
        next(4);
        break;
    case 4:
        if (busy()) {
            return;
        }
        L.base = EYES_MOOD_HAPPY;
        eyes_gesture(EYES_GESTURE_CLAP);
        say(SOUND_GIGGLE);
        needs_meet(NEED_FUN, 0.25f);
        if (L.ctx.company) {
            needs_meet(NEED_COMPANY, 0.03f);
        }
        wait_ms(1500);
        next(5);
        break;
    case 5:
        if (waiting()) {
            return;
        }
        home(1200, EYES_PATH_GLIDE);
        next(6);
        break;
    default:
        if (!busy()) {
            choose();
        }
        break;
    }
}

static void loop_flight(void)
{
    if (L.phase == 0) {
        // Round through the space: past one side, far behind, back past the
        // other and right up to the glass.
        const float d = coin(0.5f) ? 1 : -1;
        const eyes_pos_t loop[] = {
            {d * 170, -10, 1.2f}, {d * 300, -70, 2.4f}, {0, -100, 3.4f}, {-d * 300, -70, 2.4f},
            {-d * 170, -10, 1.2f}, {0, 15, 0.7f},     {0, 0, 1},
        };
        L.base = EYES_MOOD_HAPPY;
        eyes_move(loop, 7, 6200, EYES_PATH_GLIDE);
        say(SOUND_WHISTLE);
        next(1);
        return;
    }
    if (busy()) {
        return;
    }
    face(EYES_MOOD_HAPPY, 1200);
    say(SOUND_GIGGLE);
    needs_meet(NEED_FUN, 0.15f);
    needs_meet(NEED_CURIOSITY, 0.1f);
    choose();
}

static void daydream(void)
{
    if (L.phase == 0) {
        pet_state_t p;
        pet_get(&p);
        L.base = p.affection > 0.7f && p.happiness > 0.6f ? EYES_MOOD_LOVING : EYES_MOOD_NEUTRAL;
        const float d = coin(0.5f) ? 1 : -1;
        eyes_move_to(d * frange(10, 45), -35, 1.1f, 3500, EYES_PATH_DRIFT);
        eyes_look_at(0.35f * d, -0.85f);
        if (coin(0.35f)) {
            eyes_gesture(EYES_GESTURE_THINK);
        }
        if (p.happiness > 0.7f && coin(0.4f)) {
            say(SOUND_SING);
        }
        wait_ms((uint32_t) frange(9000, 14000));
        next(1);
        return;
    }
    if (waiting()) {
        return;
    }
    eyes_look_around();
    needs_meet(NEED_CURIOSITY, 0.05f);
    choose();
}

static void sulk(void)
{
    switch (L.phase) {
    case 0: {
        pet_state_t p;
        pet_get(&p);
        L.side = coin(0.5f) ? 1 : -1;
        L.base = p.affection > 0.6f ? EYES_MOOD_SAD : EYES_MOOD_GRUMPY;
        eyes_move_to((float) L.side * 150 * 1.25f, 35 * 1.25f, 1.25f, 2600, EYES_PATH_GLIDE);
        say(SOUND_PFFT);
        next(1);
        break;
    }
    case 1:
        if (busy()) {
            return;
        }
        eyes_look_at((float) L.side, 0.4f); // away from you
        wait_ms((uint32_t) frange(20000, 35000));
        next(2);
        break;
    case 2:
        if (waiting()) {
            return;
        }
        eyes_look_around();
        say(SOUND_SIGH);
        eyes_move_to(0, 0, 1, 3000, EYES_PATH_DRIFT);
        next(3);
        break;
    default:
        if (!busy()) {
            choose();
        }
        break;
    }
}

static void dance(void)
{
    if (L.phase == 0) {
        const eyes_pos_t hops[] = {at(-45, 15, 1), at(45, 15, 1), at(-45, 15, 1), at(45, 15, 1), at(0, 10, 1)};
        L.base = EYES_MOOD_HAPPY;
        eyes_move(hops, 5, 2400, EYES_PATH_HOP);
        eyes_gesture(EYES_GESTURE_CLAP);
        if (L.count == 0) {
            say(SOUND_BEAT);
        }
        L.count++;
        next(1);
        return;
    }
    if (busy()) {
        return;
    }
    if (L.ctx.music && L.count < 5) {
        wait_ms((uint32_t) frange(300, 1500));
        next(0);
        return;
    }
    needs_meet(NEED_FUN, 0.15f);
    home(600, EYES_PATH_GLIDE);
    choose();
}

static life_want_t nap(void)
{
    switch (L.phase) {
    case 0:
        L.base = EYES_MOOD_SLEEPY;
        eyes_gesture(EYES_GESTURE_STRETCH);
        say(SOUND_YAWN);
        wait_ms(2200);
        next(1);
        break;
    case 1:
        if (waiting()) {
            return LIFE_NOTHING;
        }
        // A cosy corner, low down.
        L.side = coin(0.5f) ? 1 : -1;
        eyes_move_to((float) L.side * 90 * 1.35f, 55 * 1.35f, 1.35f, 3800, EYES_PATH_DRIFT);
        eyes_look_at(0, 0.7f);
        next(2);
        break;
    case 2:
        if (busy()) {
            return LIFE_NOTHING;
        }
        eyes_blink();
        wait_ms(1800);
        next(3);
        break;
    case 3:
        if (waiting()) {
            return LIFE_NOTHING;
        }
        eyes_blink();
        say(SOUND_SIGH);
        wait_ms(1800);
        next(4);
        break;
    default:
        if (waiting()) {
            return LIFE_NOTHING;
        }
        return LIFE_SLEEP;
    }
    return LIFE_NOTHING;
}

/* Asleep, now and then: rested and someone around, or rested all through
 * and not in the middle of the night alone, it wakes up by itself. */
static life_want_t sleep_check(void)
{
    if ((int32_t) (L.now - L.next_wake_check) < 0 || !L.ctx.free) {
        return LIFE_NOTHING;
    }
    L.next_wake_check = L.now + WAKE_CHECK_MS;
    const float rest = needs_get(NEED_REST);
    const bool alone_at_night = L.ctx.night && !L.ctx.company;
    if (rest >= RESTED && !alone_at_night) {
        return LIFE_WAKE;
    }
    if (rest >= RESTED_COMPANY && L.ctx.company && !L.ctx.night && L.now - L.act_ms >= NAP_MIN_MS && coin(0.15f)) {
        return LIFE_WAKE;
    }
    return LIFE_NOTHING;
}

static void wake(void)
{
    switch (L.phase) {
    case 0:
        L.base = EYES_MOOD_SLEEPY;
        wait_ms(1300);
        next(1);
        break;
    case 1:
        if (waiting()) {
            return;
        }
        eyes_gesture(EYES_GESTURE_STRETCH);
        say(SOUND_YAWN);
        wait_ms(1900);
        next(2);
        break;
    case 2:
        if (waiting()) {
            return;
        }
        L.base = resting();
        home(2200, EYES_PATH_DRIFT);
        next(3);
        break;
    default:
        if (!busy()) {
            choose();
        }
        break;
    }
}

static void come(void)
{
    if (L.phase == 0) {
        const eyes_pos_t me = eyes_where();
        L.base = resting();
        // Far off, or past the rim: back to you.
        if (me.z > 1.4f || me.z < 0.8f || hypotf(me.x / me.z, me.y / me.z) > 90) {
            home(me.z > 2.5f ? 1100 : 800, EYES_PATH_GLIDE);
        }
        next(1);
        return;
    }
    if (!busy()) {
        choose();
    }
}

/* ---- Interface ---------------------------------------------------------------------- */

void life_init(void)
{
    memset(&L, 0, sizeof(L));
    L.now = lv_tick_get();
    // Nothing big right after starting.
    for (int i = 0; i < ACT_COUNT; i++) {
        L.next_ok[i] = L.now + 20000;
    }
    start(ACT_LOUNGE);
}

life_want_t life_step(const life_ctx_t *ctx)
{
    L.ctx = *ctx;
    L.now = lv_tick_get();
    if (ctx->asleep) {
        if (L.act != ACT_SLEEP) {
            life_slept();
        }
        return sleep_check();
    }
    if (L.act == ACT_SLEEP) {
        life_woke(false); // woken some other way
    }
    if (!ctx->free) {
        return LIFE_NOTHING;
    }
    if (ctx->lonely && L.act != ACT_NAP) {
        // Nobody around for a long while: a nap.
        start(ACT_NAP);
    }
    if ((int32_t) (L.now - L.passing_until) >= 0) {
        eyes_set_mood(L.base);
    }
    switch (L.act) {
    case ACT_LOUNGE:
        lounge();
        break;
    case ACT_EXPLORE:
        explore();
        break;
    case ACT_FORAGE:
        forage();
        break;
    case ACT_SNACK:
        snack();
        break;
    case ACT_ASK:
        ask();
        break;
    case ACT_BOUNCE:
        bounce();
        break;
    case ACT_PEEKABOO:
        peekaboo();
        break;
    case ACT_LOOP:
        loop_flight();
        break;
    case ACT_DAYDREAM:
        daydream();
        break;
    case ACT_SULK:
        sulk();
        break;
    case ACT_DANCE:
        dance();
        break;
    case ACT_NAP:
        return nap();
    case ACT_WAKE:
        wake();
        break;
    case ACT_COME:
        come();
        break;
    default:
        choose();
        break;
    }
    return LIFE_NOTHING;
}

void life_attend(void)
{
    L.now = lv_tick_get();
    if (L.act == ACT_ASK && L.phase >= 1) {
        L.answered = true; // what it was asking for
        return;
    }
    if (L.act == ACT_SNACK || L.act == ACT_SLEEP) {
        return;
    }
    eyes_mote_hide();
    eyes_look_around();
    start(ACT_COME);
}

void life_slept(void)
{
    L.now = lv_tick_get();
    eyes_mote_hide();
    start(ACT_SLEEP);
    L.next_wake_check = L.now + WAKE_CHECK_MS;
}

void life_woke(bool natural)
{
    L.now = lv_tick_get();
    start(natural ? ACT_WAKE : ACT_COME);
}

static bool can_snack(void)
{
    return L.act != ACT_SLEEP && L.act != ACT_SNACK && !(L.act == ACT_NAP && L.phase >= 2);
}

bool life_tap(int32_t x, int32_t y)
{
    L.now = lv_tick_get();
    if (!can_snack()) {
        return false;
    }
    eyes_mote_show(eyes_unproject(x, y, 1.0f));
    start(ACT_SNACK);
    return true;
}

bool life_feed(void)
{
    L.now = lv_tick_get();
    if (!can_snack()) {
        return false;
    }
    // In from past the rim, to somewhere near.
    const float a = frange(0, 2 * (float) M_PI);
    eyes_mote_show(at(300 * cosf(a), 300 * sinf(a), 1.2f));
    eyes_mote_fly(at(110 * cosf(a), 90 * sinf(a), 1.1f), 1600);
    start(ACT_SNACK);
    wait_ms(1500);
    return true;
}

const char *life_activity(void)
{
    return NAMES[L.act];
}
