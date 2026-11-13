/* What Yoru does on its own, a step at a time: each act is a few phases of
 * moves, faces and sounds, and says when it's done. The state it runs in
 * (life_mind.c) picks it and lends it a style: the face, how far and how
 * fast it drifts. Most of the time it rests: a living thing is mostly
 * still, and the eyes wander and blink on their own. */

#include <math.h>
#include <string.h>

#include "esp_random.h"
#include "life_int.h"
#include "lvgl.h"
#include "playback.h"

const act_info_t ACTS[ACT_COUNT] = {
    [ACT_REST] = {"rest", 0, 0},
    [ACT_DRIFT] = {"drift", 0, 0},
    [ACT_LOOK] = {"look", 0, 30},
    [ACT_GAZE] = {"gaze", 0, 20},
    [ACT_WINK] = {"wink", 0, 180},
    [ACT_TILT] = {"tilt", 0, 60},
    [ACT_STRETCH] = {"stretch", 0, 300},
    [ACT_SNEEZE] = {"sneeze", ACTF_LOUD, 1200},
    [ACT_HICCUP] = {"hiccup", ACTF_LOUD, 1800},
    [ACT_PEEK] = {"peek", 0, 120},
    [ACT_SNIFF] = {"sniff", 0, 120},
    [ACT_FIREFLY] = {"firefly", 0, 600},
    [ACT_DANCE] = {"dance", ACTF_LOUD, 300},
    [ACT_LOOP] = {"loop", ACTF_LOUD, 300},
    [ACT_BOUNCE] = {"bounce", ACTF_LOUD, 300},
    [ACT_STARS] = {"stars", 0, 300},
    [ACT_HUM] = {"hum", ACTF_LOUD, 300},
    [ACT_SIGH] = {"sigh", 0, 60},
    [ACT_YAWN] = {"yawn", 0, 90},
    [ACT_NOD] = {"nod", 0, 60},
    [ACT_SEARCH] = {"search", 0, 20},
    [ACT_SAD] = {"sad", 0, 30},
    [ACT_SULK] = {"sulk", 0, 0},
    // Rare: unlocked by time together (growth.c), and kept rare by their
    // long cooldowns, so that each still surprises.
    [ACT_WAVE] = {"wave", ACTF_RARE, 1800},
    [ACT_SHY] = {"shy", ACTF_RARE, 2400},
    [ACT_HOP] = {"hop", ACTF_RARE | ACTF_LOUD, 2400},
    [ACT_GIGGLE] = {"giggle", ACTF_RARE | ACTF_LOUD, 3000},
    [ACT_CHEER] = {"cheer", ACTF_RARE | ACTF_LOUD, 3600},
    [ACT_LOVE] = {"love", ACTF_RARE, 3600},
};

/* The firefly's wander, in screen pixels from the middle. */
#define FLY_RX 150
#define FLY_RY 120
#define FLY_MIN_MS 4500
#define FLY_MAX_MS 8000

static struct {
    act_id_t id;
    act_style_t style;
    int phase;
    uint32_t wait_until;
    uint32_t t0;
    int side;
    int n;
    float a, b; // the firefly's phases
    bool caught;
} A;

static void wait_ms(uint32_t ms)
{
    A.wait_until = LT.now + ms;
}

static bool waiting(void)
{
    return (int32_t) (LT.now - A.wait_until) < 0;
}

static bool busy(void)
{
    return waiting() || eyes_moving() || eyes_playing();
}

static void next(int phase)
{
    A.phase = phase;
}

static int coin(void)
{
    return (esp_random() & 1) ? 1 : -1;
}

/* Its face in this state, now and then coloured by how it feels. */
static eyes_mood_t face(void)
{
    const float r = life_frange(0, 1);
    if (A.style.face == EYES_MOOD_NEUTRAL) {
        if (mood(EMO_HAPPY) > 0.45f && r < 0.35f) {
            return EYES_MOOD_HAPPY;
        }
        if (mood(EMO_STIM) < -0.45f && r < 0.4f) {
            return EYES_MOOD_BORED;
        }
        if (mood(EMO_ENERGY) < -0.5f) {
            return EYES_MOOD_SLEEPY;
        }
    }
    return A.style.face;
}

/* ---- The acts ---------------------------------------------------------------- */


/* Most acts are a clip: home first if it has drifted off (the clip moves
 * from where Yoru is, and some go to an edge and back), then the clip with
 * its face and sound, and the face it eases into after. Done when it ends. */
#define HOME_NEAR 30.0f
#define HOME_MS 900

static bool act_clip(clip_id_t clip, bool mirror, eyes_mood_t during, eyes_mood_t after, sound_t sound)
{
    switch (A.phase) {
    case 0: {
        eyes_set_mood(during);
        const eyes_pos_t me = eyes_where();
        if (fabsf(me.x) > HOME_NEAR || fabsf(me.y) > HOME_NEAR || fabsf(me.z - 1) > 0.1f) {
            eyes_move_to(0, 0, 1, HOME_MS, EYES_PATH_GLIDE);
        }
        next(1);
        return false;
    }
    case 1:
        if (busy()) {
            return false;
        }
        eyes_play(clip, mirror);
        if (sound != SOUND_COUNT) {
            life_sound(sound);
        }
        eyes_set_mood(after);
        next(2);
        return false;
    default:
        return !busy();
    }
}

/* Doing nothing in particular, where it is: the eyes look about and blink
 * by themselves. Longer the slower the state. */
static bool rest(void)
{
    if (A.phase == 0) {
        eyes_set_mood(face());
        eyes_look_around();
        wait_ms((uint32_t) (life_frange(6000, 14000) * A.style.pace));
        next(1);
        return false;
    }
    return !waiting();
}

/* Float off to somewhere in the state's room, slower when tired. */
static bool drift(void)
{
    if (A.phase == 0) {
        eyes_set_mood(face());
        const float tired = mood(EMO_ENERGY) < -0.3f ? 1.4f : 1;
        const act_style_t *s = &A.style;
        const eyes_pos_t p = life_at(life_frange(-s->roam_x, s->roam_x), life_frange(-s->roam_y * 0.7f, s->roam_y),
                                     life_frange(s->z_lo, s->z_hi));
        eyes_move_to(p.x, p.y, p.z, (uint32_t) (life_frange(2600, 4600) * s->pace * tired), EYES_PATH_DRIFT);
        next(1);
        return false;
    }
    return !busy();
}

/* A look round: left, right, up, and back to its own gaze. */
static bool look(void)
{
    return act_clip(CLIP_LOOK_AROUND, A.side < 0, face(), face(), SOUND_COUNT);
}

static bool tilt(void)
{
    if (A.phase == 0) {
        eyes_set_mood(EYES_MOOD_CURIOUS);
        eyes_play(CLIP_CURIOUS_TILT, coin() < 0);
        life_sound(SOUND_HMM);
        wait_ms(2300);
        next(1);
        return false;
    }
    if (waiting()) {
        return false;
    }
    eyes_look_around();
    return true;
}

static bool stretch(void)
{
    if (A.phase == 0) {
        eyes_play(CLIP_WAKE_STRETCH, false);
        if (mood(EMO_ENERGY) < 0) {
            life_sound(SOUND_YAWN);
        }
        wait_ms(3000);
        next(1);
        return false;
    }
    return !waiting();
}

static bool sneeze(void)
{
    switch (A.phase) {
    case 0:
        // "Ah... ah..." is the clip's; the sound comes with its "choo".
        eyes_play(CLIP_SNEEZE, coin() < 0);
        wait_ms(1100);
        next(1);
        return false;
    case 1:
        if (waiting()) {
            return false;
        }
        life_sound(SOUND_SNEEZE);
        eyes_set_mood(EYES_MOOD_HAPPY);
        wait_ms(1100);
        next(2);
        return false;
    default:
        return !waiting();
    }
}

/* Two hiccups, a jolt each, then a giggle at itself. The hiccup clip
 * jolts at these times; the sounds come with them. */
#define HIC_FIRST_MS 230
#define HIC_SECOND_MS 1370
#define HIC_GIGGLE_MS 1800

static bool hiccup(void)
{
    if (waiting()) {
        return false;
    }
    switch (A.phase) {
    case 0:
        eyes_set_mood(EYES_MOOD_SURPRISED);
        eyes_play(CLIP_HICCUP, coin() < 0);
        wait_ms(HIC_FIRST_MS);
        next(1);
        return false;
    case 1:
        life_sound(SOUND_HICCUP);
        wait_ms(HIC_SECOND_MS - HIC_FIRST_MS);
        next(2);
        return false;
    case 2:
        life_sound(SOUND_HICCUP);
        wait_ms(HIC_GIGGLE_MS - HIC_SECOND_MS);
        next(3);
        return false;
    case 3:
        eyes_set_mood(EYES_MOOD_DELIGHTED);
        life_sound(SOUND_GIGGLE);
        next(4);
        return false;
    default:
        return !busy();
    }
}

/* Off to the very edge, peering out of the glass; back with a look over
 * its shoulder. */
static bool peek(void)
{
    if (A.phase == 0 && life_frange(0, 1) < 0.35f) {
        // Hide and seek: out at one edge, back in at the other.
        eyes_set_mood(EYES_MOOD_CURIOUS);
        eyes_play(CLIP_HIDE_SEEK, coin() < 0);
        life_sound(SOUND_GIGGLE);
        next(9);
        return false;
    }
    if (A.phase == 9) {
        if (busy()) {
            return false;
        }
        eyes_set_mood(EYES_MOOD_DELIGHTED);
        mood_feel(LIFE_FEEL_FUN);
        return true;
    }
    return act_clip(CLIP_PEEK_SIDE, coin() < 0, EYES_MOOD_CURIOUS, EYES_MOOD_CURIOUS, SOUND_HMM);
}

/* Down to a corner to sniff at something there: little hops, a look. */
static bool sniff(void)
{
    return act_clip(CLIP_SNIFF, coin() < 0, EYES_MOOD_CURIOUS, EYES_MOOD_HAPPY, SOUND_SCAN);
}

/* Where the firefly is, `ms` into its flight: a wandering knot of sines. */
static void fly_at(uint32_t ms, float *x, float *y)
{
    const float t = (float) ms / 1000.0f;
    *x = FLY_RX * (0.75f * sinf(t * 0.9f + A.a) + 0.25f * sinf(t * 2.6f + A.b));
    *y = FLY_RY * (0.7f * sinf(t * 1.3f + A.b) + 0.3f * sinf(t * 3.1f + A.a)) - 20;
}

/* A firefly, the one thing it knows from home: it drifts in, Yoru follows
 * it with its eyes and its whole body, and pounces. Sometimes it gets away. */
/* The firefly_gulp clip pounces this far (screen pixels, right and up) and
 * closes its hands on the firefly this far in; it lasts the rest. */
#define GULP_DX 110
#define GULP_DY 40
#define GULP_CATCH_MS 900
#define GULP_REST_MS 2200

static bool firefly(void)
{
    float fx, fy;
    switch (A.phase) {
    case 0:
        A.a = life_frange(0, 6.3f);
        A.b = life_frange(0, 6.3f);
        A.t0 = LT.now;
        A.n = (int) life_frange(FLY_MIN_MS, FLY_MAX_MS);
        A.caught = life_frange(0, 1) < 0.65f;
        eyes_set_mood(EYES_MOOD_CURIOUS);
        life_sound(SOUND_FLUTTER);
        next(1);
        return false;
    case 1: {
        const uint32_t ms = LT.now - A.t0;
        fly_at(ms, &fx, &fy);
        eyes_firefly(true, fx, fy);
        eyes_look_at(fx / 180, fy / 180);
        // The body lags behind, a lazy spring, a little short of it.
        eyes_follow(fx * 0.55f, fy * 0.5f, 1, 7);
        if (ms > 1500) {
            eyes_set_mood(EYES_MOOD_STARRY);
        }
        if ((int32_t) ms >= A.n) {
            if (A.caught) {
                // Beside it, where the clip's pounce lands on it.
                A.side = fx < 0 ? -1 : 1;
                eyes_move_to(fx - (float) A.side * GULP_DX, fy + GULP_DY, 1, 380, EYES_PATH_DART);
                next(2);
            } else {
                // Away, out of the screen.
                next(4);
                A.t0 = LT.now;
            }
        }
        return false;
    }
    case 2:
        if (busy()) {
            return false;
        }
        eyes_play(CLIP_FIREFLY_GULP, A.side < 0);
        wait_ms(GULP_CATCH_MS);
        next(7);
        return false;
    case 7:
        // The hands close on it.
        if (waiting()) {
            return false;
        }
        eyes_firefly(false, 0, 0);
        eyes_set_mood(EYES_MOOD_DELIGHTED);
        life_sound(SOUND_GIGGLE);
        mood_feel(LIFE_FEEL_FUN);
        wait_ms(GULP_REST_MS);
        next(3);
        return false;
    case 3:
        if (waiting()) {
            return false;
        }
        eyes_move_to(0, 0, 1, 1500, EYES_PATH_DRIFT);
        eyes_look_around();
        next(6);
        return false;
    case 4: {
        // It flies off upwards; Yoru stretches after it.
        const float k = fminf(1, (float) (LT.now - A.t0) / 900.0f);
        fly_at(LT.now - A.t0 + (uint32_t) A.n, &fx, &fy);
        fy -= k * 300;
        eyes_firefly(k < 1, fx, fy);
        eyes_look_at(fmaxf(-1, fminf(1, fx / 180)), fmaxf(-1, fminf(1, fy / 180)));
        if (k >= 1) {
            eyes_set_mood(EYES_MOOD_SAD);
            life_sound(SOUND_AWW);
            wait_ms(1600);
            next(5);
        }
        return false;
    }
    case 5:
        if (waiting()) {
            return false;
        }
        eyes_move_to(0, 0, 1, 1500, EYES_PATH_DRIFT);
        eyes_look_around();
        next(6);
        return false;
    default:
        return !busy();
    }
}

static bool dance(void)
{
    if (A.phase == 0) {
        eyes_set_mood(EYES_MOOD_HAPPY);
        eyes_play(CLIP_DANCE, coin() < 0);
        life_sound(SOUND_SING);
        next(1);
        return false;
    }
    if (busy()) {
        return false;
    }
    mood_feel(LIFE_FEEL_FUN);
    return true;
}

static bool loop_the_loop(void)
{
    if (A.phase == 2 && !busy()) {
        mood_feel(LIFE_FEEL_FUN);
    }
    return act_clip(CLIP_LOOP_THE_LOOP, coin() < 0, EYES_MOOD_HAPPY, EYES_MOOD_HAPPY, SOUND_WHISTLE);
}

/* Bouncing off the walls of the screen, squashing at each. */
static bool bounce(void)
{
    if (A.phase == 2 && !busy()) {
        mood_feel(LIFE_FEEL_FUN);
    }
    return act_clip(CLIP_BOUNCE_WALLS, coin() < 0, EYES_MOOD_DELIGHTED, EYES_MOOD_HAPPY, SOUND_BOING);
}

/* Up high, as near the sky as the glass lets it: the sky it came from. */
static bool stars(void)
{
    return act_clip(CLIP_STARGAZE, coin() < 0, EYES_MOOD_DREAMY, EYES_MOOD_DREAMY, SOUND_COUNT);
}

/* Humming to itself, swaying slowly, eyes up. */
static bool hum(void)
{
    return act_clip(CLIP_HUM_SWAY, coin() < 0, EYES_MOOD_DREAMY, EYES_MOOD_DREAMY, SOUND_SING);
}

/* Bored: a sigh, sinking a little, a look away. */
static bool sigh(void)
{
    return act_clip(CLIP_SIGH, coin() < 0, EYES_MOOD_BORED, EYES_MOOD_BORED, SOUND_SIGH);
}

static bool yawn(void)
{
    if (A.phase == 0) {
        eyes_set_mood(EYES_MOOD_SLEEPY);
        eyes_play(CLIP_YAWN, false);
        life_sound(SOUND_YAWN);
        wait_ms(2100);
        next(1);
        return false;
    }
    return !waiting();
}

/* Nodding off: sinking, eyes closing, then a jolt awake. */
/* The nod_off clip jolts awake this far in. */
#define NOD_JOLT_MS 2130

static bool nod(void)
{
    switch (A.phase) {
    case 0:
        eyes_set_mood(EYES_MOOD_SLEEPY);
        eyes_play(CLIP_NOD_OFF, coin() < 0);
        wait_ms(NOD_JOLT_MS);
        next(1);
        return false;
    case 1:
        if (waiting()) {
            return false;
        }
        life_sound(SOUND_EEP);
        next(2);
        return false;
    case 2:
        if (busy()) {
            return false;
        }
        eyes_set_mood(EYES_MOOD_SLEEPY);
        eyes_look_around();
        return true;
    default:
        return !busy();
    }
}

/* Sulking: off to one side, turned away; a glance back now and then. The
 * sulk_turn clip snaps away again this far in, and lasts the rest. */
#define SULK_PFFT_MS 2500
#define SULK_REST_MS 1500

static bool sulk(void)
{
    switch (A.phase) {
    case 0:
        A.side = coin();
        eyes_set_mood(EYES_MOOD_GRUMPY);
        eyes_move_to((float) A.side * 125 * 1.15f, 20 * 1.15f, 1.15f, 1800, EYES_PATH_GLIDE);
        eyes_look_at((float) A.side, 0.2f);
        life_sound(SOUND_GRUMBLE);
        next(1);
        return false;
    case 1:
        if (busy()) {
            return false;
        }
        // Turned away towards its edge, a glance back, "pfft", away again.
        eyes_play(CLIP_SULK_TURN, A.side < 0);
        wait_ms(SULK_PFFT_MS);
        next(2);
        return false;
    case 2:
        if (waiting()) {
            return false;
        }
        life_sound(SOUND_PFFT);
        wait_ms(SULK_REST_MS);
        next(3);
        return false;
    default:
        return !waiting();
    }
}

/* Looking at you: a little nearer, a slow blink (the cat's: I like you). */
static bool gaze(void)
{
    const eyes_mood_t after = mood(EMO_HAPPY) > 0.35f ? EYES_MOOD_HAPPY : face();
    return act_clip(CLIP_GAZE_BLINK, false, after, after, SOUND_COUNT);
}

static bool wink(void)
{
    return act_clip(CLIP_WINK, coin() < 0, EYES_MOOD_HAPPY, EYES_MOOD_HAPPY, SOUND_WINK);
}

/* Looking for you: left, right, and a small call. */
static bool search(void)
{
    return act_clip(CLIP_SEARCH_YOU, coin() < 0, EYES_MOOD_SAD, EYES_MOOD_SAD, SOUND_WHIMPER);
}

static bool sad(void)
{
    if (A.phase == 0) {
        // The clip sinks it and leaves it there, low.
        eyes_set_mood(EYES_MOOD_SAD);
        eyes_play(CLIP_SAD_DROOP, coin() < 0);
        wait_ms((uint32_t) life_frange(4500, 6500));
        next(1);
        return false;
    }
    return !busy();
}

/* The rare ones (growth.c): what it does when touched or told, now and then
 * of its own accord. */
static bool wave(void)
{
    return act_clip(CLIP_WAVE, coin() < 0, EYES_MOOD_HAPPY, EYES_MOOD_HAPPY, SOUND_HELLO);
}

static bool shy(void)
{
    return act_clip(CLIP_SHY_PEEK, false, EYES_MOOD_HAPPY, EYES_MOOD_HAPPY, SOUND_COUNT);
}

static bool hop(void)
{
    if (A.phase == 2 && !busy()) {
        mood_feel(LIFE_FEEL_FUN);
    }
    return act_clip(CLIP_HAPPY_HOP, coin() < 0, EYES_MOOD_HAPPY, EYES_MOOD_HAPPY, SOUND_HAPPY);
}

static bool giggle(void)
{
    return act_clip(CLIP_GIGGLE, false, EYES_MOOD_DELIGHTED, EYES_MOOD_HAPPY, SOUND_GIGGLE);
}

static bool cheer(void)
{
    return act_clip(CLIP_CELEBRATE, false, EYES_MOOD_HAPPY, EYES_MOOD_HAPPY, SOUND_TADA);
}

static bool love(void)
{
    return act_clip(CLIP_LOVE, false, EYES_MOOD_LOVING, EYES_MOOD_HAPPY, SOUND_AWW);
}

/* ---- Running them ------------------------------------------------------------- */

typedef bool (*act_fn)(void);

static const act_fn FNS[ACT_COUNT] = {
    [ACT_REST] = rest,       [ACT_DRIFT] = drift,         [ACT_LOOK] = look,     [ACT_GAZE] = gaze,
    [ACT_WINK] = wink,       [ACT_TILT] = tilt,           [ACT_STRETCH] = stretch, [ACT_SNEEZE] = sneeze,
    [ACT_HICCUP] = hiccup,   [ACT_PEEK] = peek,           [ACT_SNIFF] = sniff,   [ACT_FIREFLY] = firefly,
    [ACT_DANCE] = dance,     [ACT_LOOP] = loop_the_loop,  [ACT_BOUNCE] = bounce, [ACT_STARS] = stars,
    [ACT_HUM] = hum,         [ACT_SIGH] = sigh,           [ACT_YAWN] = yawn,     [ACT_NOD] = nod,
    [ACT_SEARCH] = search,   [ACT_SAD] = sad,             [ACT_SULK] = sulk,
    [ACT_WAVE] = wave,       [ACT_SHY] = shy,             [ACT_HOP] = hop,       [ACT_GIGGLE] = giggle,
    [ACT_CHEER] = cheer,     [ACT_LOVE] = love,
};

void acts_start(act_id_t id, const act_style_t *style)
{
    memset(&A, 0, sizeof(A));
    A.id = id < ACT_COUNT ? id : ACT_REST;
    A.style = *style;
    A.wait_until = LT.now;
    A.side = coin();
}

bool acts_step(void)
{
    const bool done = FNS[A.id]();
    if (done) {
        eyes_firefly(false, 0, 0);
    }
    return done;
}

act_id_t acts_by_name(const char *name)
{
    for (int i = 0; i < ACT_COUNT; i++) {
        if (strcmp(name, ACTS[i].name) == 0) {
            return (act_id_t) i;
        }
    }
    return ACT_COUNT;
}
