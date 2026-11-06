/* What Yoru does on its own, a step at a time: each act is a few phases of
 * moves, faces and sounds, and says when it's done. The state it runs in
 * (life_mind.c) picks it and lends it a style: the face, how far and how
 * fast it drifts. Most of the time it rests: a living thing is mostly
 * still, and the eyes wander and blink on their own. */

#include <math.h>
#include <string.h>

#include "esp_random.h"
#include "friends.h"
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
};

/* The firefly's wander, in screen pixels from the middle. */
#define FLY_RX 150
#define FLY_RY 120
#define FLY_MIN_MS 4500
#define FLY_MAX_MS 8000
/* Come from the other Yoru: from beyond the edge into its wander. */
#define FLY_IN_MS 1400.0f
#define FLY_EDGE 260.0f

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
    /* Joining another Yoru: the friends aren't told. */
    bool guest;
    /* The firefly: come from the other Yoru's screen, at (from_x, from_y)
     * in pixels from the middle; or flying off to it, that way (to_x, to_y). */
    bool from, to;
    float from_x, from_y, to_x, to_y;
} A;

static bool s_guest;
static bool s_from;
static float s_from_x, s_from_y, s_from_along;

void acts_as_guest(void)
{
    s_guest = true;
}

void acts_firefly_from(float x, float y, float along)
{
    s_from = true;
    s_from_x = x;
    s_from_y = y;
    s_from_along = along;
}

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
    return waiting() || eyes_moving();
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
    static const float GAZE[][2] = {{-0.85f, 0.05f}, {0.85f, 0.05f}, {0.1f, -0.7f}};
    if (busy()) {
        return false;
    }
    if (A.phase < 3) {
        // Left then right, or the other way round, then up.
        const int i = A.phase == 2 ? 2 : A.side > 0 ? A.phase : 1 - A.phase;
        eyes_look_at(GAZE[i][0], GAZE[i][1]);
        wait_ms((uint32_t) life_frange(600, 1000));
        next(A.phase + 1);
        return false;
    }
    eyes_look_around();
    return true;
}

static bool tilt(void)
{
    if (A.phase == 0) {
        eyes_set_mood(EYES_MOOD_CURIOUS);
        eyes_look_at(0, -0.1f);
        life_sound(SOUND_HMM);
        wait_ms(1500);
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
        eyes_play(CLIP_WAKE_STRETCH);
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
        eyes_set_mood(EYES_MOOD_SURPRISED);
        wait_ms(700);
        next(1);
        return false;
    case 1:
        if (waiting()) {
            return false;
        }
        life_sound(SOUND_SNEEZE);
        eyes_poke();
        wait_ms(900);
        next(2);
        return false;
    case 2:
        if (waiting()) {
            return false;
        }
        eyes_set_mood(EYES_MOOD_HAPPY);
        wait_ms(1200);
        next(3);
        return false;
    default:
        return !waiting();
    }
}

/* Two hiccups, a jolt each, then a giggle at itself. */
static bool hiccup(void)
{
    if (waiting()) {
        return false;
    }
    if (A.phase < 2) {
        eyes_set_mood(EYES_MOOD_SURPRISED);
        life_sound(SOUND_HICCUP);
        eyes_knock(0, -0.45f);
        wait_ms((uint32_t) life_frange(1100, 1700));
        next(A.phase + 1);
        return false;
    }
    if (A.phase == 2) {
        eyes_set_mood(EYES_MOOD_DELIGHTED);
        life_sound(SOUND_GIGGLE);
        wait_ms(1200);
        next(3);
        return false;
    }
    return true;
}

/* Off to the very edge, peering out of the glass; back with a look over
 * its shoulder. */
static bool peek(void)
{
    switch (A.phase) {
    case 0:
        A.side = coin();
        eyes_set_mood(EYES_MOOD_CURIOUS);
        eyes_move_to((float) A.side * 175, life_frange(-25, 30), 1.0f, 1400, EYES_PATH_GLIDE);
        eyes_look_at((float) A.side, 0);
        next(1);
        return false;
    case 1:
        if (busy()) {
            return false;
        }
        eyes_lean((float) A.side, 0);
        wait_ms((uint32_t) life_frange(1500, 2800));
        next(2);
        return false;
    case 2:
        if (waiting()) {
            return false;
        }
        eyes_lean(0, 0);
        life_sound(SOUND_HMM);
        eyes_look_at((float) A.side * 0.6f, 0);
        eyes_move_to(0, 0, 1, 1700, EYES_PATH_DRIFT);
        next(3);
        return false;
    default:
        if (busy()) {
            return false;
        }
        eyes_look_around();
        return true;
    }
}

/* Down to a corner to sniff at something there: little hops, a look. */
static bool sniff(void)
{
    switch (A.phase) {
    case 0:
        A.side = coin();
        eyes_set_mood(EYES_MOOD_CURIOUS);
        eyes_move_to((float) A.side * 115 * 1.05f, 75 * 1.05f, 1.05f, 1600, EYES_PATH_GLIDE);
        eyes_look_at((float) A.side * 0.8f, 0.8f);
        next(1);
        return false;
    case 1: {
        if (busy()) {
            return false;
        }
        const eyes_pos_t me = eyes_where();
        const eyes_pos_t hops[] = {{me.x - (float) A.side * 12, me.y, me.z}, {me.x, me.y, me.z},
                                   {me.x - (float) A.side * 8, me.y, me.z}, me};
        eyes_move(hops, 4, 1500, EYES_PATH_HOP);
        life_sound(SOUND_SCAN);
        next(2);
        return false;
    }
    case 2:
        if (busy()) {
            return false;
        }
        eyes_set_mood(life_frange(0, 1) < 0.5f ? EYES_MOOD_HAPPY : EYES_MOOD_CURIOUS);
        eyes_look_at(0, -0.1f);
        wait_ms(1100);
        next(3);
        return false;
    case 3:
        if (waiting()) {
            return false;
        }
        eyes_look_around();
        eyes_move_to(0, 0, 1, 1800, EYES_PATH_DRIFT);
        next(4);
        return false;
    default:
        return !busy();
    }
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
        if (A.from) {
            A.caught = life_frange(0, 1) < 0.85f; // a gift: mostly caught
        } else if (friends_awake() > 0 && friends_side(&A.to_x, &A.to_y)) {
            // A friend nearby: now and then it flies over there.
            A.to = life_frange(0, 1) < 0.5f;
            A.caught = A.caught && !A.to;
        }
        eyes_set_mood(EYES_MOOD_CURIOUS);
        life_sound(SOUND_FLUTTER);
        next(1);
        return false;
    case 1: {
        const uint32_t ms = LT.now - A.t0;
        fly_at(ms, &fx, &fy);
        if (A.from && ms < FLY_IN_MS) {
            // In from the edge facing the other Yoru, into its wander.
            const float k = (float) ms / FLY_IN_MS, e = k * k * (3 - 2 * k);
            fx = A.from_x + (fx - A.from_x) * e;
            fy = A.from_y + (fy - A.from_y) * e;
        }
        eyes_firefly(true, fx, fy);
        eyes_look_at(fx / 180, fy / 180);
        // The body lags behind, a lazy spring, a little short of it.
        eyes_follow(fx * 0.55f, fy * 0.5f, 1, 7);
        if (ms > 1500) {
            eyes_set_mood(EYES_MOOD_STARRY);
        }
        if ((int32_t) ms >= A.n) {
            if (A.caught) {
                eyes_move_to(fx, fy, 1, 380, EYES_PATH_DART);
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
        eyes_firefly(false, 0, 0);
        eyes_set_mood(EYES_MOOD_DELIGHTED);
        eyes_gesture(EYES_GESTURE_CLAP);
        life_sound(SOUND_GIGGLE);
        mood_feel(LIFE_FEEL_FUN);
        wait_ms(1500);
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
        // It flies off upwards, or to the other Yoru; Yoru stretches after it.
        const float k = fminf(1, (float) (LT.now - A.t0) / 900.0f);
        fly_at(LT.now - A.t0 + (uint32_t) A.n, &fx, &fy);
        if (A.to) {
            fx += k * A.to_x * 320;
            fy += k * A.to_y * 320;
        } else {
            fy -= k * 300;
        }
        eyes_firefly(k < 1, fx, fy);
        eyes_look_at(fmaxf(-1, fminf(1, fx / 180)), fmaxf(-1, fminf(1, fy / 180)));
        if (k >= 1 && A.to) {
            // Over to the friend: where along the edge it left.
            const float along = (fx * -A.to_y + fy * A.to_x) / 150;
            friends_send(FRIEND_FIREFLY, (int16_t) lroundf(fmaxf(-1, fminf(1, along)) * 1000), 0);
            eyes_set_mood(EYES_MOOD_HAPPY);
            eyes_gesture(EYES_GESTURE_WAVE);
            life_sound(SOUND_HELLO);
            wait_ms(1600);
            next(5);
        } else if (k >= 1) {
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
        const eyes_pos_t hops[] = {life_at(-28, 0, 1), life_at(28, 0, 1), life_at(-28, 0, 1), life_at(28, 0, 1),
                                   life_at(0, 0, 1)};
        eyes_move(hops, 5, 2600, EYES_PATH_HOP);
        life_sound(SOUND_SING);
        next(1);
        return false;
    }
    if (A.phase == 1) {
        if (busy()) {
            return false;
        }
        eyes_gesture(EYES_GESTURE_CLAP);
        mood_feel(LIFE_FEEL_FUN);
        wait_ms(1300);
        next(2);
        return false;
    }
    return !waiting();
}

static bool loop_the_loop(void)
{
    if (A.phase == 0) {
        eyes_set_mood(EYES_MOOD_HAPPY);
        const float r = 55;
        eyes_pos_t pts[EYES_PATH_POINTS];
        for (int i = 0; i < EYES_PATH_POINTS - 1; i++) {
            const float a = (float) M_PI / 2 + 2 * (float) M_PI * (float) (i + 1) / (EYES_PATH_POINTS - 1);
            pts[i] = life_at(r * cosf(a), r - r * sinf(a), 1);
        }
        pts[EYES_PATH_POINTS - 1] = life_at(0, 0, 1);
        eyes_move(pts, EYES_PATH_POINTS, 1900, EYES_PATH_GLIDE);
        life_sound(SOUND_WHISTLE);
        next(1);
        return false;
    }
    if (busy()) {
        return false;
    }
    eyes_blink();
    mood_feel(LIFE_FEEL_FUN);
    return true;
}

/* Bouncing off the walls of the screen, squashing at each. */
static bool bounce(void)
{
    if (A.phase == 0) {
        eyes_set_mood(EYES_MOOD_DELIGHTED);
        const float s = (float) coin();
        const eyes_pos_t hops[] = {life_at(s * 120, 40, 1), life_at(-s * 110, 10, 1), life_at(s * 70, -40, 1),
                                   life_at(-s * 40, 30, 1), life_at(0, 0, 1)};
        eyes_move(hops, 5, 2800, EYES_PATH_HOP);
        life_sound(SOUND_BOING);
        next(1);
        return false;
    }
    if (A.phase == 1) {
        if (busy()) {
            return false;
        }
        eyes_set_mood(EYES_MOOD_HAPPY);
        mood_feel(LIFE_FEEL_FUN);
        wait_ms(800);
        next(2);
        return false;
    }
    return !waiting();
}

/* Up high, as near the sky as the glass lets it: the sky it came from. */
static bool stars(void)
{
    switch (A.phase) {
    case 0:
        eyes_set_mood(EYES_MOOD_DREAMY);
        eyes_move_to(life_frange(-30, 30) * 1.2f, -80 * 1.2f, 1.2f, 3200, EYES_PATH_DRIFT);
        eyes_look_at(0, -1);
        next(1);
        return false;
    case 1:
        if (busy()) {
            return false;
        }
        eyes_blink();
        wait_ms((uint32_t) life_frange(3500, 6000));
        next(2);
        return false;
    case 2:
        if (waiting()) {
            return false;
        }
        life_sound(SOUND_SIGH);
        eyes_look_around();
        eyes_move_to(0, 0, 1, 2600, EYES_PATH_DRIFT);
        next(3);
        return false;
    default:
        return !busy();
    }
}

/* Humming to itself, swaying slowly, eyes up. */
static bool hum(void)
{
    if (A.phase == 0) {
        eyes_set_mood(EYES_MOOD_DREAMY);
        eyes_look_at(0.2f, -0.6f);
        const eyes_pos_t sway[] = {life_at(-26, -12, 1.05f), life_at(26, -12, 1.05f), life_at(-26, -12, 1.05f),
                                   life_at(0, -8, 1.05f)};
        eyes_move(sway, 4, 5200, EYES_PATH_DRIFT);
        life_sound(SOUND_SING);
        next(1);
        return false;
    }
    if (busy()) {
        return false;
    }
    eyes_look_around();
    return true;
}

/* Bored: a sigh, sinking a little, a look away. */
static bool sigh(void)
{
    switch (A.phase) {
    case 0: {
        eyes_set_mood(EYES_MOOD_BORED);
        life_sound(SOUND_SIGH);
        const eyes_pos_t me = eyes_where();
        eyes_move_to(me.x, me.y + 25 * me.z, me.z, 1300, EYES_PATH_DRIFT);
        eyes_look_at((float) coin() * 0.8f, 0.3f);
        wait_ms((uint32_t) life_frange(2400, 3600));
        next(1);
        return false;
    }
    case 1:
        if (busy()) {
            return false;
        }
        eyes_look_around();
        return true;
    default:
        return true;
    }
}

static bool yawn(void)
{
    if (A.phase == 0) {
        eyes_set_mood(EYES_MOOD_SLEEPY);
        eyes_gesture(EYES_GESTURE_STRETCH);
        life_sound(SOUND_YAWN);
        wait_ms(2300);
        next(1);
        return false;
    }
    return !waiting();
}

/* Nodding off: sinking, eyes closing, then a jolt awake. */
static bool nod(void)
{
    switch (A.phase) {
    case 0: {
        eyes_set_mood(EYES_MOOD_SLEEPY);
        const eyes_pos_t me = eyes_where();
        eyes_move_to(me.x, me.y + 35 * me.z, me.z, 3200, EYES_PATH_DRIFT);
        eyes_look_at(0, 0.7f);
        next(1);
        return false;
    }
    case 1:
        if (busy()) {
            return false;
        }
        eyes_knock(0, -0.6f);
        eyes_set_mood(EYES_MOOD_SURPRISED);
        life_sound(SOUND_EEP);
        wait_ms(700);
        next(2);
        return false;
    case 2: {
        if (waiting()) {
            return false;
        }
        eyes_set_mood(EYES_MOOD_SLEEPY);
        const eyes_pos_t me = eyes_where();
        eyes_move_to(me.x, me.y - 35 * me.z, me.z, 1500, EYES_PATH_GLIDE);
        eyes_look_around();
        next(3);
        return false;
    }
    default:
        return !busy();
    }
}

/* Sulking: off to one side, turned away; a glance back now and then. */
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
        wait_ms((uint32_t) life_frange(4000, 7000));
        next(2);
        return false;
    case 2:
        if (waiting()) {
            return false;
        }
        // Is anybody coming? A quick glance back.
        eyes_look_at(-(float) A.side * 0.7f, 0);
        wait_ms(900);
        next(3);
        return false;
    case 3:
        if (waiting()) {
            return false;
        }
        life_sound(SOUND_PFFT);
        eyes_look_at((float) A.side, 0.2f);
        wait_ms(1500);
        next(4);
        return false;
    default:
        return !waiting();
    }
}

/* Looking at you: a little nearer, a slow blink (the cat's: I like you). */
static bool gaze(void)
{
    switch (A.phase) {
    case 0: {
        eyes_set_mood(mood(EMO_HAPPY) > 0.35f ? EYES_MOOD_HAPPY : face());
        eyes_look_at(0, -0.05f);
        const eyes_pos_t me = eyes_where();
        eyes_move_to(me.x * 0.5f, me.y * 0.5f, A.style.z_lo, 1800, EYES_PATH_DRIFT);
        wait_ms((uint32_t) life_frange(2500, 4000));
        next(1);
        return false;
    }
    case 1:
        if (busy()) {
            return false;
        }
        eyes_blink();
        wait_ms((uint32_t) life_frange(2000, 3500));
        next(2);
        return false;
    default:
        if (waiting()) {
            return false;
        }
        eyes_look_around();
        return true;
    }
}

static bool wink(void)
{
    if (A.phase == 0) {
        eyes_look_at(0, -0.05f);
        eyes_set_mood(EYES_MOOD_HAPPY);
        eyes_wink((esp_random() & 1) ? 0 : 1, 450);
        life_sound(SOUND_WINK);
        wait_ms(1500);
        next(1);
        return false;
    }
    if (waiting()) {
        return false;
    }
    eyes_look_around();
    return true;
}

/* Looking for you: left, right, and a small call. */
static bool search(void)
{
    static const float GAZE[][2] = {{-1, 0}, {1, 0}, {-0.5f, -0.4f}, {0, 0}};
    if (busy()) {
        return false;
    }
    if (A.phase == 0) {
        eyes_set_mood(EYES_MOOD_SAD);
    }
    if (A.phase < 4) {
        eyes_look_at(GAZE[A.phase][0], GAZE[A.phase][1]);
        wait_ms(A.phase == 3 ? 400 : 1300);
        next(A.phase + 1);
        return false;
    }
    if (A.phase == 4) {
        life_sound(SOUND_WHIMPER);
        wait_ms(2500);
        next(5);
        return false;
    }
    return true;
}

static bool sad(void)
{
    if (A.phase == 0) {
        eyes_set_mood(EYES_MOOD_SAD);
        eyes_look_at(0, 0.3f);
        const eyes_pos_t me = eyes_where();
        eyes_move_to(me.x, me.y + 20 * me.z, me.z, 2000, EYES_PATH_DRIFT);
        wait_ms((uint32_t) life_frange(4000, 6000));
        next(1);
        return false;
    }
    return !busy();
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
};

/* What the friends hear about, so they can join in. */
static void tell_friends(act_id_t id)
{
    switch (id) {
    case ACT_YAWN:
        friends_send(FRIEND_YAWN, 0, 0);
        break;
    case ACT_SNEEZE:
        friends_send(FRIEND_SNEEZE, 0, 0);
        break;
    case ACT_HICCUP:
        friends_send(FRIEND_HICCUP, 0, 0);
        break;
    case ACT_DANCE:
        friends_send(FRIEND_DANCE, 0, 0);
        break;
    default:
        break;
    }
}

void acts_start(act_id_t id, const act_style_t *style)
{
    memset(&A, 0, sizeof(A));
    A.id = id < ACT_COUNT ? id : ACT_REST;
    A.style = *style;
    A.wait_until = LT.now;
    A.side = coin();
    A.guest = s_guest;
    s_guest = false;
    if (A.id == ACT_FIREFLY && s_from) {
        A.from = true;
        // Beyond the edge facing the friend, `along` it.
        A.from_x = s_from_x * FLY_EDGE - s_from_y * s_from_along * 150;
        A.from_y = s_from_y * FLY_EDGE + s_from_x * s_from_along * 150;
    }
    s_from = false;
    if (!A.guest) {
        tell_friends(A.id);
    }
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
