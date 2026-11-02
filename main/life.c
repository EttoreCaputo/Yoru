#include "life.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "eyes.h"
#include "lvgl.h"
#include "playback.h"

static const char *TAG = "life";

/* Calling you: how near it comes and how long it waits for an answer. */
#define CALL_Z 0.62f
#define CALL_WAIT_MS 11000
/* A whim now and then while it lounges: sooner when cheerful. */
#define WHIM_MIN_MS 45000
#define WHIM_MAX_MS 150000
/* Away this long and back: a greeting, if it comes within GREET_WITHIN_MS
 * (not after a whole conversation). */
#define GREET_AFTER_MS (15 * 60 * 1000)
#define BACK_MS 5000
#define GREET_WITHIN_MS 6000
/* The morning's greeting: the first time someone is around in these hours. */
#define MORNING_FROM (5 * 60)
#define MORNING_TO (12 * 60)
#define NIGHT_FROM (22 * 60)
#define NIGHT_TO (6 * 60)
/* Cheer: each bit of attention adds this, every minute alone takes this. */
#define CHEER_START 0.6f
#define CHEER_ATTEND 0.06f
#define CHEER_GREET 0.1f
#define CHEER_DECAY_PER_MIN 0.008f
#define CHEER_HIGH 0.75f
#define CHEER_LOW 0.25f
/* Alone this long before the cheer starts wearing down. */
#define CHEER_ALONE_MS 60000

typedef enum {
    ACT_LOUNGE, // floating about near you
    ACT_CALL,   // up at the glass: something waits for you
    ACT_NAP,    // yawns, finds a corner, settles down
    ACT_SLEEP,
    ACT_COME,   // called or touched: back to you
    ACT_WHIM,   // something of its own, for a moment
    ACT_GREET,  // you're back: it comes forward and waves
    ACT_COUNT,
} act_t;

static const char *const NAMES[ACT_COUNT] = {"lounge", "call", "nap", "sleep", "come", "whim", "greet"};

typedef enum {
    WHIM_PEEK,    // off to the edge, peering out of the screen
    WHIM_STRETCH,
    WHIM_DANCE,   // a little hopping dance, humming
    WHIM_STARS,   // up high, gazing at the sky it came from
    WHIM_LOOP,    // a loop the loop
    WHIM_SNEEZE,
    WHIM_SEEK,    // glum: comes up to look for you, left and right
    WHIM_COUNT,
} whim_t;

static struct {
    life_ctx_t ctx;
    uint32_t now;
    act_t act;
    int phase;
    uint32_t wait_until;
    int side;
    bool answered;
    whim_t whim;
    uint32_t whim_at;
    float cheer;
    uint32_t cheer_ms;
    /* Away long enough for a greeting; when someone came back. */
    bool missed;
    uint32_t back_ms;
    bool morning;
    int32_t greeted_day;
} L;

static float frange(float lo, float hi)
{
    return lo + (hi - lo) * (float) (esp_random() & 0xFFFF) / 65535.0f;
}

/* The point at distance `z` that shows (sx, sy) pixels from the middle. */
static eyes_pos_t at(float sx, float sy, float z)
{
    return (eyes_pos_t) {sx * z, sy * z, z};
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

static void start(act_t act)
{
    if (L.act != act) {
        ESP_LOGI(TAG, "%s -> %s", NAMES[L.act], NAMES[act]);
    }
    L.act = act;
    L.phase = 0;
    L.wait_until = L.now;
    eyes_look_around();
}

/* Drifting a little way off and pausing there; slower when drowsy. */
static void lounge(void)
{
    const bool calm = L.ctx.drowsy || L.ctx.focus;
    if (busy()) {
        return;
    }
    // Its cheer shows now and then on its face while it floats about.
    const float r = frange(0, 1);
    eyes_set_mood(L.ctx.drowsy                       ? EYES_MOOD_SLEEPY
                  : L.cheer >= CHEER_HIGH && r < 0.3f ? EYES_MOOD_HAPPY
                  : L.cheer <= CHEER_LOW && r < 0.4f  ? EYES_MOOD_BORED
                                                      : EYES_MOOD_NEUTRAL);
    const eyes_pos_t p = calm ? at(frange(-35, 35), frange(0, 30), frange(1.0f, 1.25f))
                              : at(frange(-65, 65), frange(-28, 40), frange(0.88f, 1.3f));
    const uint32_t ms = (uint32_t) frange(2600, 4600) * (calm ? 3 : 2) / 2;
    eyes_move_to(p.x, p.y, p.z, ms, EYES_PATH_DRIFT);
    wait_ms(ms + (uint32_t) frange(2500, 7000));
}

static void call(void)
{
    switch (L.phase) {
    case 0:
        L.answered = false;
        eyes_set_mood(EYES_MOOD_CURIOUS);
        // Up to the glass, its face filling the screen.
        eyes_move_to(0, 22 * CALL_Z, CALL_Z, 1400, EYES_PATH_GLIDE);
        next(1);
        break;
    case 1: {
        if (busy()) {
            return;
        }
        eyes_look_at(0, -0.12f);
        // Knock, knock: two taps on the glass, then pointing at what
        // waits for you.
        const eyes_pos_t taps[] = {at(0, 22, 0.54f), at(0, 22, CALL_Z), at(0, 22, 0.54f), at(0, 22, CALL_Z)};
        eyes_move(taps, 4, 700, EYES_PATH_GLIDE);
        eyes_gesture(EYES_GESTURE_POINT);
        playback_sound(SOUND_HMM);
        wait_ms(CALL_WAIT_MS + (uint32_t) frange(0, 4000));
        next(2);
        break;
    }
    case 2:
        if (L.answered) {
            eyes_set_mood(EYES_MOOD_HAPPY);
        } else if (waiting()) {
            eyes_look_at(0, -0.12f);
            return;
        }
        eyes_look_around();
        eyes_move_to(0, 0, 1, 1800, EYES_PATH_GLIDE);
        next(3);
        break;
    default:
        if (!busy()) {
            start(ACT_LOUNGE);
        }
        break;
    }
}

static life_want_t nap(void)
{
    switch (L.phase) {
    case 0:
        eyes_set_mood(EYES_MOOD_SLEEPY);
        eyes_gesture(EYES_GESTURE_STRETCH);
        if (!L.ctx.focus) {
            playback_sound(SOUND_YAWN);
        }
        wait_ms(2200);
        next(1);
        break;
    case 1:
        if (waiting()) {
            return LIFE_NOTHING;
        }
        // A cosy corner, low down.
        L.side = (esp_random() & 1) ? 1 : -1;
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
        wait_ms(1800);
        next(4);
        break;
    default:
        return waiting() ? LIFE_NOTHING : LIFE_SLEEP;
    }
    return LIFE_NOTHING;
}

static void come(void)
{
    if (L.phase == 0) {
        const eyes_pos_t me = eyes_where();
        eyes_set_mood(EYES_MOOD_NEUTRAL);
        // Off to a side or far: back to you.
        if (me.z > 1.4f || me.z < 0.8f || hypotf(me.x / me.z, me.y / me.z) > 90) {
            eyes_move_to(0, 0, 1, 800, EYES_PATH_GLIDE);
        }
        next(1);
        return;
    }
    if (!busy()) {
        start(ACT_LOUNGE);
    }
}

static bool night(void)
{
    return L.ctx.minute >= 0 && (L.ctx.minute >= NIGHT_FROM || L.ctx.minute < NIGHT_TO);
}

static void next_whim(void)
{
    const float pace = L.cheer >= CHEER_HIGH ? 0.7f : L.cheer <= CHEER_LOW ? 1.4f : 1.0f;
    L.whim_at = L.now + (uint32_t) (frange(WHIM_MIN_MS, WHIM_MAX_MS) * pace);
}

/* Which whim, by the hour and the cheer: dances when cheerful, the sky at
 * night, looking for you when glum. */
static whim_t pick_whim(void)
{
    const bool happy = L.cheer >= CHEER_HIGH;
    const int weights[WHIM_COUNT] = {
        [WHIM_PEEK] = 3,
        [WHIM_STRETCH] = 2,
        [WHIM_DANCE] = happy ? 3 : 1,
        [WHIM_STARS] = night() ? 4 : 1,
        [WHIM_LOOP] = happy ? 2 : 1,
        [WHIM_SNEEZE] = 1,
        [WHIM_SEEK] = L.cheer <= CHEER_LOW ? 4 : 0,
    };
    int total = 0;
    for (int i = 0; i < WHIM_COUNT; i++) {
        total += weights[i];
    }
    int pick = (int) (esp_random() % (uint32_t) total);
    for (int i = 0; i < WHIM_COUNT; i++) {
        if (pick < weights[i]) {
            return (whim_t) i;
        }
        pick -= weights[i];
    }
    return WHIM_PEEK;
}

static void whim_done(void)
{
    next_whim();
    start(ACT_LOUNGE);
}

static void peek(void)
{
    switch (L.phase) {
    case 0:
        // To the very edge, as if to look out of the glass.
        L.side = (esp_random() & 1) ? 1 : -1;
        eyes_set_mood(EYES_MOOD_CURIOUS);
        eyes_move_to((float) L.side * 175, frange(-25, 30), 1.0f, 1400, EYES_PATH_GLIDE);
        eyes_look_at((float) L.side, 0);
        next(1);
        break;
    case 1:
        if (busy()) {
            return;
        }
        eyes_lean((float) L.side, 0);
        wait_ms((uint32_t) frange(1500, 2800));
        next(2);
        break;
    case 2:
        if (waiting()) {
            return;
        }
        // Something out there; back to you, with a look over its shoulder.
        playback_sound(SOUND_HMM);
        eyes_look_at((float) L.side * 0.6f, 0);
        eyes_move_to(0, 0, 1, 1700, EYES_PATH_DRIFT);
        next(3);
        break;
    default:
        if (!busy()) {
            whim_done();
        }
        break;
    }
}

static void dance(void)
{
    if (L.phase == 0) {
        eyes_set_mood(EYES_MOOD_HAPPY);
        const eyes_pos_t hops[] = {at(-28, 0, 1), at(28, 0, 1), at(-28, 0, 1), at(28, 0, 1), at(0, 0, 1)};
        eyes_move(hops, 5, 2600, EYES_PATH_HOP);
        playback_sound(SOUND_SING);
        next(1);
    } else if (L.phase == 1 && !busy()) {
        eyes_gesture(EYES_GESTURE_CLAP);
        wait_ms(1300);
        next(2);
    } else if (L.phase == 2 && !waiting()) {
        whim_done();
    }
}

static void stars(void)
{
    switch (L.phase) {
    case 0:
        // Up high, as near the sky as the glass lets it, looking up.
        eyes_set_mood(EYES_MOOD_NEUTRAL);
        eyes_move_to(frange(-30, 30) * 1.2f, -80 * 1.2f, 1.2f, 3200, EYES_PATH_DRIFT);
        eyes_look_at(0, -1);
        next(1);
        break;
    case 1:
        if (busy()) {
            return;
        }
        eyes_blink();
        wait_ms((uint32_t) frange(3500, 6000));
        next(2);
        break;
    case 2:
        if (waiting()) {
            return;
        }
        // Far from home: a soft sigh, and back.
        playback_sound(SOUND_SIGH);
        eyes_look_around();
        eyes_move_to(0, 0, 1, 2600, EYES_PATH_DRIFT);
        next(3);
        break;
    default:
        if (!busy()) {
            whim_done();
        }
        break;
    }
}

static void loop(void)
{
    if (L.phase == 0) {
        eyes_set_mood(EYES_MOOD_HAPPY);
        const float r = 55;
        eyes_pos_t pts[EYES_PATH_POINTS];
        for (int i = 0; i < EYES_PATH_POINTS - 1; i++) {
            const float a = (float) M_PI / 2 + 2 * (float) M_PI * (float) (i + 1) / (EYES_PATH_POINTS - 1);
            pts[i] = at(r * cosf(a), r - r * sinf(a), 1);
        }
        pts[EYES_PATH_POINTS - 1] = at(0, 0, 1);
        eyes_move(pts, EYES_PATH_POINTS, 1900, EYES_PATH_GLIDE);
        playback_sound(SOUND_WHISTLE);
        next(1);
    } else if (!busy()) {
        eyes_blink();
        whim_done();
    }
}

static void sneeze(void)
{
    if (L.phase == 0) {
        eyes_set_mood(EYES_MOOD_SURPRISED);
        wait_ms(700);
        next(1);
    } else if (L.phase == 1 && !waiting()) {
        playback_sound(SOUND_SNEEZE);
        eyes_poke();
        wait_ms(900);
        next(2);
    } else if (L.phase == 2 && !waiting()) {
        eyes_set_mood(EYES_MOOD_HAPPY);
        wait_ms(1200);
        next(3);
    } else if (L.phase == 3 && !waiting()) {
        whim_done();
    }
}

/* Glum: up to the glass, a look left and right for you, a sad little call. */
static void seek(void)
{
    switch (L.phase) {
    case 0:
        eyes_set_mood(EYES_MOOD_SAD);
        eyes_move_to(0, 18 * 0.75f, 0.75f, 1600, EYES_PATH_GLIDE);
        next(1);
        break;
    case 1:
        if (busy()) {
            return;
        }
        eyes_look_at(-0.9f, 0);
        wait_ms(1100);
        next(2);
        break;
    case 2:
        if (waiting()) {
            return;
        }
        eyes_look_at(0.9f, 0);
        wait_ms(1100);
        next(3);
        break;
    case 3:
        if (waiting()) {
            return;
        }
        eyes_look_at(0, 0);
        playback_sound(SOUND_WHIMPER);
        wait_ms(2200);
        next(4);
        break;
    case 4:
        if (waiting()) {
            return;
        }
        eyes_look_around();
        eyes_move_to(0, 0, 1, 1800, EYES_PATH_DRIFT);
        next(5);
        break;
    default:
        if (!busy()) {
            whim_done();
        }
        break;
    }
}

static void whim(void)
{
    switch (L.whim) {
    case WHIM_PEEK:
        peek();
        break;
    case WHIM_DANCE:
        dance();
        break;
    case WHIM_STARS:
        stars();
        break;
    case WHIM_LOOP:
        loop();
        break;
    case WHIM_SNEEZE:
        sneeze();
        break;
    case WHIM_SEEK:
        seek();
        break;
    default:
        if (L.phase == 0) {
            eyes_gesture(EYES_GESTURE_STRETCH);
            if (L.ctx.drowsy || night()) {
                playback_sound(SOUND_YAWN);
            }
            wait_ms(2000);
            next(1);
        } else if (!waiting()) {
            whim_done();
        }
        break;
    }
}

/* You're back (or it's morning): it comes forward, stretches first in the
 * morning, and waves. */
static void greet(void)
{
    switch (L.phase) {
    case 0:
        eyes_set_mood(EYES_MOOD_HAPPY);
        eyes_look_at(0, 0);
        eyes_move_to(0, 12 * 0.78f, 0.78f, 900, EYES_PATH_GLIDE);
        if (L.morning) {
            eyes_gesture(EYES_GESTURE_STRETCH);
            playback_sound(SOUND_YAWN);
            wait_ms(1900);
        }
        next(1);
        break;
    case 1:
        if (busy()) {
            return;
        }
        eyes_gesture(EYES_GESTURE_WAVE);
        playback_sound(SOUND_HELLO);
        L.cheer = fminf(1, L.cheer + CHEER_GREET);
        wait_ms(1900);
        next(2);
        break;
    case 2:
        if (waiting()) {
            return;
        }
        eyes_move_to(0, 0, 1, 1200, EYES_PATH_GLIDE);
        next(3);
        break;
    default:
        if (!busy()) {
            start(ACT_LOUNGE);
        }
        break;
    }
}

/* Time alone wears the cheer down; someone around is noticed coming back. */
static void keep_track(void)
{
    const uint32_t dt = L.now - L.cheer_ms;
    L.cheer_ms = L.now;
    if (L.ctx.alone_ms >= CHEER_ALONE_MS) {
        L.cheer = fmaxf(0, L.cheer - CHEER_DECAY_PER_MIN * (float) dt / 60000.0f);
    }
    if (L.ctx.alone_ms >= GREET_AFTER_MS) {
        L.missed = true;
    } else if (L.ctx.alone_ms < BACK_MS && L.back_ms == 0) {
        const bool morning = L.ctx.day >= 0 && L.ctx.day != L.greeted_day && L.ctx.minute >= MORNING_FROM
                             && L.ctx.minute < MORNING_TO;
        if (L.missed || morning) {
            L.back_ms = L.now;
            L.morning = morning;
        }
        L.missed = false;
    }
}

/* A greeting due, if it still fits: not after a whole conversation. */
static bool greeting_due(void)
{
    if (L.back_ms == 0) {
        return false;
    }
    const bool fresh = L.now - L.back_ms < GREET_WITHIN_MS;
    L.back_ms = 0;
    if (fresh && !L.ctx.focus) {
        L.greeted_day = L.ctx.day;
        return true;
    }
    return false;
}

void life_init(void)
{
    memset(&L, 0, sizeof(L));
    L.now = lv_tick_get();
    L.cheer = CHEER_START;
    L.cheer_ms = L.now;
    L.greeted_day = -1;
    next_whim();
    start(ACT_LOUNGE);
}

life_want_t life_step(const life_ctx_t *ctx)
{
    L.ctx = *ctx;
    L.now = lv_tick_get();
    keep_track();
    if (ctx->asleep) {
        if (L.act != ACT_SLEEP) {
            life_slept();
        }
        return LIFE_NOTHING;
    }
    if (L.act == ACT_SLEEP) {
        life_woke(); // woken some other way
    }
    if (!ctx->free) {
        return LIFE_NOTHING;
    }
    if (ctx->lonely && L.act != ACT_NAP) {
        start(ACT_NAP);
    }
    if ((L.act == ACT_LOUNGE || L.act == ACT_COME) && greeting_due()) {
        start(ACT_GREET);
    } else if (L.act == ACT_LOUNGE && !ctx->focus && !ctx->drowsy && (int32_t) (L.now - L.whim_at) >= 0) {
        L.whim = pick_whim();
        ESP_LOGI(TAG, "whim %d, cheer %.2f", (int) L.whim, L.cheer);
        start(ACT_WHIM);
    }
    switch (L.act) {
    case ACT_CALL:
        call();
        break;
    case ACT_NAP:
        return nap();
    case ACT_COME:
        come();
        break;
    case ACT_WHIM:
        whim();
        break;
    case ACT_GREET:
        greet();
        break;
    default:
        lounge();
        break;
    }
    return LIFE_NOTHING;
}

void life_attend(void)
{
    L.now = lv_tick_get();
    L.cheer = fminf(1, L.cheer + CHEER_ATTEND);
    if (L.act == ACT_CALL && L.phase >= 1) {
        L.answered = true;
        return;
    }
    if (L.act == ACT_SLEEP) {
        return;
    }
    start(ACT_COME);
}

void life_slept(void)
{
    L.now = lv_tick_get();
    start(ACT_SLEEP);
}

void life_woke(void)
{
    L.now = lv_tick_get();
    start(ACT_COME);
}

bool life_call(void)
{
    L.now = lv_tick_get();
    if (L.act == ACT_SLEEP || (L.act == ACT_CALL && L.phase < 3) || (L.act == ACT_NAP && L.phase >= 2)) {
        return false;
    }
    start(ACT_CALL);
    return true;
}

bool life_calling(void)
{
    return L.act == ACT_CALL && L.phase >= 1 && L.phase < 3;
}

const char *life_activity(void)
{
    return NAMES[L.act];
}

float life_cheer(void)
{
    return L.cheer;
}
