#include "eyes.h"

#include <math.h>
#include <string.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "look.h"

/* One frame every 33 ms; when nothing moves, nothing is redrawn. Motion
 * follows the clock, not the frame count, so a slow frame doesn't slow it. */
#define EYES_FRAME_MS 33
/* A frame later than this is a stall (a flash write, a long log): the
 * animation skips ahead at most this far. */
#define EYES_MAX_STEP_MS 200
#define CX (BOARD_LCD_H_RES / 2)
#define CY (BOARD_LCD_V_RES / 2)
#define R LOOK_SPHERE_R
/* Sizes below are at the Rive file's scale; K brings them to the screen's. */
#define K LOOK_SCALE
/* Yoru's eyes: half the gap between them, how far below the sphere's centre
 * they sit, and how far the gaze slides them over the surface. */
#define EYE_SPACING (32 * K)
#define FACE_Y (16 * K)
#define GAZE_X (62 * K)
#define GAZE_Y (48 * K)
#define HAND_Y 40
#define BADGE_X (-98 * K)
#define BADGE_Y (-98 * K)
#define HEART_X (160 * K)
/* Asleep, Yoru drifts away into the dark: this far, in sphere sizes, which
 * leaves a dot a tenth of its size, dimmed. */
#define SLEEP_FAR 9.0f
#define SLEEP_DIM 0.55f
/* The distance changes at a steady pace, which in perspective reads as
 * slowing down as it goes and speeding up as it comes back. */
#define SLEEP_AWAY_MS 2000
#define SLEEP_BACK_MS 700
/* Far away the dark swallows it: dimmer the farther, down to FOG_FLOOR. */
#define FOG 0.35f
#define FOG_FLOOR 0.45f
/* The nearest it comes: its face up at the glass, filling the disc. */
#define Z_NEAR 0.5f
/* Faster than this across the screen (pixels a second) it stretches along
 * the way it goes. */
#define SPEED_STRETCH 900.0f
/* A hop rises this high on screen, plus a share of how far it goes. */
#define HOP_ARC 34.0f
/* With the menu or an approval card open Yoru shrinks to this, clear of
 * the icons or the card, in this long. */
#define MENU_SIZE 0.62f
#define MENU_SIZE_MS 220
/* Tinted (thinking, done, error) Yoru steps back a little, inside its halo. */
#define STATUS_SIZE 0.86f
/* The most pieces an overlay has (three z's, three dots). */
#define MAX_OVERLAY_RECTS 3
/* The eye sprites: the largest eye (surprised, swelling, stretched) fits
 * in this. */
#define EYE_BUF_W 40
#define EYE_BUF_H 80
#define EYE_MARGIN 2

/* What a mood looks like. The lids: `top` cuts the eye from above (0..1 of
 * its height), `slope` tilts that cut (> 0: lower towards the nose, angry;
 * < 0: lower outside, worried), `bottom` raises a curve from below (the
 * ^ ^ of a smile). `tilt` turns the face, in degrees; `left`/`right` scale
 * each eye's height. Sizes in pixels, for Yoru's small pill eyes. */
typedef struct {
    float w, h;
    float top, slope, bottom;
    float tilt;
    float left, right;
    /* 1: drawn as a spiral (dizzy) instead of a pill. */
    float spiral;
} eye_shape_t;

static const eye_shape_t s_moods[EYES_MOOD_COUNT] = {
    [EYES_MOOD_NEUTRAL] = {26, 60, 0, 0, 0, 0, 1, 1},
    [EYES_MOOD_HAPPY] = {29, 57, 0, 0, 1, 0, 1, 1},
    [EYES_MOOD_CURIOUS] = {27, 60, 0, 0, 0, -9, 1.15f, 0.72f},
    [EYES_MOOD_THINKING] = {24, 50, 0.18f, -0.12f, 0, 5, 1, 1},
    [EYES_MOOD_GRUMPY] = {27, 53, 0.32f, 0.38f, 0, 0, 1, 1},
    [EYES_MOOD_HOT] = {26, 51, 0.30f, -0.32f, 0, 0, 1, 1},
    [EYES_MOOD_SLEEPY] = {28, 57, 0.58f, 0, 0, 0, 1, 1},
    [EYES_MOOD_ASLEEP] = {28, 57, 0.90f, 0, 0, 0, 1, 1},
    [EYES_MOOD_SAD] = {25, 54, 0.26f, -0.40f, 0, 0, 1, 1},
    [EYES_MOOD_LOVING] = {29, 57, 0, 0, 1, 0, 1, 1},
    [EYES_MOOD_SURPRISED] = {30, 69, 0, 0, 0, 0, 1.04f, 1.04f},
    [EYES_MOOD_BORED] = {27, 55, 0.44f, 0, 0.2f, 0, 1, 1},
    [EYES_MOOD_LISTENING] = {29, 65, 0, 0, 0, -4, 1, 1},
    [EYES_MOOD_SPEAKING] = {28, 58, 0, 0, 0.3f, 0, 1, 1},
    [EYES_MOOD_DIZZY] = {36, 36, 0, 0, 0, 0, 1, 1, 1},
    [EYES_MOOD_SCARED] = {22, 70, 0.12f, -0.45f, 0, 0, 1, 1},
    [EYES_MOOD_PEEK] = {20, 44, 0, 0, 0, 0, 1, 1},
    [EYES_MOOD_WAITING] = {24, 54, 0, 0, 0, 7, 1.08f, 0.92f},
};

static const char *const s_mood_names[EYES_MOOD_COUNT] = {
    "neutral", "happy", "curious", "thinking", "grumpy", "hot", "sleepy", "asleep", "sad", "loving", "surprised", "bored", "listening", "speaking", "dizzy", "scared", "peek", "waiting",
};

/* The status colours: blue thinking, green done, red error. */
static const uint32_t s_status_rgb[] = {0x3D8BFF, 0x3D8BFF, 0x3DDC84, 0xFF4D5E};

/* Where a hand floats, from the body's centre, and its size (0 = tucked
 * away). */
typedef struct {
    float x, y, s;
} hand_pose_t;

typedef struct {
    uint16_t ms;
    hand_pose_t l, r;
} hand_key_t;

#define TUCK_L {-112, HAND_Y, 0}
#define TUCK_R {112, HAND_Y, 0}
#define OUT_L {-168, HAND_Y, 1}
#define OUT_R {168, HAND_Y, 1}
#define WAVE_A {150, -70, 1.05f}
#define WAVE_B {156, -74, 1.05f}

/* The gestures, as in the Rive file: short paths for the two hands that
 * start and end tucked away. */
static const hand_key_t WAVE[] = {
    {0, TUCK_L, TUCK_R},       {250, OUT_L, WAVE_A},  {450, OUT_L, WAVE_B}, {650, OUT_L, WAVE_A},
    {850, OUT_L, WAVE_B},      {1050, OUT_L, WAVE_A}, {1250, OUT_L, WAVE_B}, {1450, OUT_L, WAVE_A},
    {1750, TUCK_L, TUCK_R},
};
static const hand_key_t POINT[] = {
    {0, TUCK_L, TUCK_R},
    {250, OUT_L, {180, -24, 1.1f}},
    {420, OUT_L, {194, -30, 1.1f}},
    {600, OUT_L, {180, -24, 1.1f}},
    {1500, OUT_L, {180, -24, 1.1f}},
    {1800, TUCK_L, TUCK_R},
};
static const hand_key_t COVER[] = {
    {0, TUCK_L, TUCK_R},
    {280, {-36, 20, 1.25f}, {36, 20, 1.25f}},
    {1300, {-36, 20, 1.25f}, {36, 20, 1.25f}},
    {1650, {-36, 20, 1.25f}, {46, 84, 1.15f}},
    {2700, {-36, 20, 1.25f}, {46, 84, 1.15f}},
    {3000, TUCK_L, TUCK_R},
};
static const hand_key_t THINK[] = {
    {0, TUCK_L, TUCK_R},
    {300, TUCK_L, {36, 136, 0.95f}},
    {420, TUCK_L, {36, 130, 0.95f}},
    {540, TUCK_L, {36, 136, 0.95f}},
    {1700, TUCK_L, {36, 136, 0.95f}},
    {1820, TUCK_L, {36, 130, 0.95f}},
    {1940, TUCK_L, {36, 136, 0.95f}},
    {3000, TUCK_L, {36, 136, 0.95f}},
    {3300, TUCK_L, TUCK_R},
};
static const hand_key_t CLAP[] = {
    {0, TUCK_L, TUCK_R},
    {250, {-72, 92, 1}, {72, 92, 1}},
    {380, {-25, 92, 1}, {25, 92, 1}},
    {500, {-72, 92, 1}, {72, 92, 1}},
    {630, {-25, 92, 1}, {25, 92, 1}},
    {750, {-72, 92, 1}, {72, 92, 1}},
    {880, {-25, 92, 1}, {25, 92, 1}},
    {1000, {-72, 92, 1}, {72, 92, 1}},
    {1300, TUCK_L, TUCK_R},
};
/* Holding its head while the world spins: the hands at its temples, swaying. */
static const hand_key_t HOLD[] = {
    {0, TUCK_L, TUCK_R},
    {300, {-118, -30, 1.1f}, {118, -30, 1.1f}},
    {600, {-112, -40, 1.1f}, {124, -20, 1.1f}},
    {900, {-124, -20, 1.1f}, {112, -40, 1.1f}},
    {1200, {-112, -40, 1.1f}, {124, -20, 1.1f}},
    {1500, {-124, -20, 1.1f}, {112, -40, 1.1f}},
    {1800, {-118, -30, 1.1f}, {118, -30, 1.1f}},
    {2100, TUCK_L, TUCK_R},
};
/* Go away: the right hand pushes out at you, twice. */
static const hand_key_t SHOO[] = {
    {0, TUCK_L, TUCK_R},
    {200, TUCK_L, {120, 60, 1.0f}},
    {330, TUCK_L, {70, 50, 1.4f}},
    {480, TUCK_L, {120, 60, 1.0f}},
    {610, TUCK_L, {70, 50, 1.4f}},
    {900, TUCK_L, {120, 60, 1.0f}},
    {1100, TUCK_L, TUCK_R},
};
/* Falling: both hands up, flapping. */
static const hand_key_t FLAIL[] = {
    {0, TUCK_L, TUCK_R},
    {150, {-160, -120, 1}, {160, -110, 1}},
    {270, {-150, -96, 1}, {170, -128, 1}},
    {390, {-170, -128, 1}, {150, -96, 1}},
    {510, {-150, -96, 1}, {170, -128, 1}},
    {630, {-170, -128, 1}, {150, -96, 1}},
    {750, {-150, -96, 1}, {170, -128, 1}},
    {870, {-160, -120, 1}, {160, -110, 1}},
    {1150, TUCK_L, TUCK_R},
};
/* Ready to jump in: the hands meet over its head, and stay there a moment. */
static const hand_key_t DIVE[] = {
    {0, TUCK_L, TUCK_R},
    {220, {-70, -110, 1}, {70, -110, 1}},
    {420, {-20, -166, 1.05f}, {20, -166, 1.05f}},
    {800, {-20, -172, 1.05f}, {20, -172, 1.05f}},
    {1000, TUCK_L, TUCK_R},
};
/* Bringing something back: both hands low in front, as if holding a box. */
#define CARRY_HOLD_MS 2300
static const hand_key_t CARRY[] = {
    {0, TUCK_L, TUCK_R},
    {260, {-46, 112, 1}, {46, 112, 1}},
    {CARRY_HOLD_MS, {-46, 112, 1}, {46, 112, 1}},
    {CARRY_HOLD_MS + 300, TUCK_L, TUCK_R},
};
static const hand_key_t STRETCH[] = {
    {0, TUCK_L, TUCK_R},
    {500, {-206, -34, 1.1f}, {206, -34, 1.1f}},
    {1100, {-206, -34, 1.1f}, {206, -34, 1.1f}},
    {1500, OUT_L, OUT_R},
    {1800, TUCK_L, TUCK_R},
};

typedef struct {
    const char *name;
    const hand_key_t *keys;
    int n;
    bool front; // in front of the body (over the eyes, under the chin)
} gesture_t;

#define GESTURE(name, keys, front) {name, keys, sizeof(keys) / sizeof(keys[0]), front}
static const gesture_t s_gestures[EYES_GESTURE_COUNT] = {
    [EYES_GESTURE_NONE] = {"none", NULL, 0, false},
    [EYES_GESTURE_WAVE] = GESTURE("wave", WAVE, false),
    [EYES_GESTURE_POINT] = GESTURE("point", POINT, false),
    [EYES_GESTURE_COVER] = GESTURE("cover", COVER, true),
    [EYES_GESTURE_THINK] = GESTURE("think", THINK, true),
    [EYES_GESTURE_CLAP] = GESTURE("clap", CLAP, true),
    [EYES_GESTURE_STRETCH] = GESTURE("stretch", STRETCH, false),
    [EYES_GESTURE_HOLD] = GESTURE("hold", HOLD, false),
    [EYES_GESTURE_SHOO] = GESTURE("shoo", SHOO, true),
    [EYES_GESTURE_FLAIL] = GESTURE("flail", FLAIL, false),
    [EYES_GESTURE_DIVE] = GESTURE("dive", DIVE, false),
    [EYES_GESTURE_CARRY] = GESTURE("carry", CARRY, true),
};

/* Something that moves through the space: along a path, or after a point. */
typedef struct {
    eyes_pos_t pos, vel;
    eyes_pos_t pts[EYES_PATH_POINTS + 1]; // pts[0]: where the path started
    int n;                                // points in pts; 0: no path
    int32_t ms, dur_ms;
    eyes_path_t style;
    int seg;                              // the stretch it is on (a hop each)
    bool follow;
    eyes_pos_t to;
    float stiff;
} mover_t;

/* What a mover's step did, for the hops' squash. */
#define MOVED_TAKEOFF 1
#define MOVED_LANDED 2

/* Everything one frame shows, worked out before drawing so the same numbers
 * decide what to redraw. Positions are screen pixels. */
typedef struct {
    int32_t bx, by;      // the body's centre
    int32_t rx, ry;      // the sphere's radii: distance, squash and stretch
    float sx, sy;        // the same as scales of the round one
    float near;          // 1 here .. 0 far away, asleep
    float dist;          // how big its distance leaves it, ~2 .. ~0.1
    float fog;           // how bright that distance leaves it, 1 .. FOG_FLOOR
    float tint;          // the status tint, halo and badge
    float tint_rgb[3];   // floats, so the struct compares without padding
} body_vis_t;

typedef struct {
    float x, y, w, h;      // centre and size in pixels
    float top_l, top_r;    // where the top lid cuts, from the centre
    float bottom;
    float spiral;          // > 0.5: a spiral, turned by `phase` (radians)
    float phase;
} eye_box_t;

/* An eye's picture, redrawn only when its shape changes. */
typedef struct {
    look_sprite_t sp;
    int32_t key[6];
    lv_area_t drawn;
    bool valid;
} eye_sprite_t;

static struct {
    lv_obj_t *obj;
    eyes_mood_t mood;
    eye_shape_t cur;
    /* Gaze, -1..1, and where it is going. */
    float gx, gy, tgx, tgy;
    bool held;            // eyes_look_at() in charge
    int32_t next_look_ms; // until the next saccade
    /* Blink: 1 open, ~0 closed. */
    float lid;
    int32_t blink_ms;     // > 0 while closing
    int32_t next_blink_ms;
    float squash;
    /* The voice's level, eased, and where it is going. */
    float level, tlevel;
    uint32_t t_ms;        // animation clock
    uint32_t last_tick;   // when the last frame ran
    int32_t dt_ms;        // how long since, for this frame's step
    float frames;         // the same in 33 ms frames, for the easing
    float near;
    float size;           // 1, MENU_SIZE with the menu open, STATUS_SIZE tinted
    eyes_status_t status;
    float tint;
    float tint_rgb[3];
    /* The gesture playing, how far in, and where the hands were when it
     * started (so a new one picks up from there). */
    eyes_gesture_t gesture;
    int32_t gesture_ms;
    hand_pose_t hand_from[2];
    hand_pose_t hand[2];
    bool hands_front;
    /* A knock: where the body is pushed to and how fast it moves, in pixels;
     * the lean from motion.c, eased, and where it is going. */
    float kx, ky, kvx, kvy;
    float lx, ly, tlx, tly;
    /* Where it is in the space, and how it gets around. */
    mover_t mv;
    float vsq;            // stretch from speed, eased
    uint32_t frame;
    const look_sprite_t *ball;
    /* This frame and what the last one covered. */
    body_vis_t body;
    lv_area_t body_drawn;
    bool body_valid;
    eye_box_t eye[2];
    eye_sprite_t eye_sp[2];
    lv_area_t hand_drawn[2];
    lv_area_t badge_drawn;
    float badge_scale;
    lv_area_t over_drawn[MAX_OVERLAY_RECTS];
    int over_count;
    /* The parcel in its hands (0: none), and where it was drawn. */
    uint32_t carry_rgb;
    lv_area_t carry_drawn;
    bool carry_shown;
} s;

static float lerp(float a, float b, float k)
{
    return a + (b - a) * k;
}

/* An easing step tuned per 33 ms frame, for however long this frame took. */
static float ease(float k)
{
    return 1 - powf(1 - k, s.frames);
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float frand(void)
{
    return (float) (esp_random() & 0xFFFF) / 65535.0f;
}

/* 0..1 as t goes from a to b. */
static float seg(float t, float a, float b)
{
    return clampf((t - a) / (b - a), 0, 1);
}

static float smooth(float k)
{
    return k * k * (3 - 2 * k);
}

static lv_color_t rgb_color(const float rgb[3])
{
    return lv_color_make((uint8_t) rgb[0], (uint8_t) rgb[1], (uint8_t) rgb[2]);
}

static bool has_overlay(eyes_mood_t mood)
{
    return mood == EYES_MOOD_THINKING || mood == EYES_MOOD_HOT || mood == EYES_MOOD_ASLEEP || mood == EYES_MOOD_LOVING
           || mood == EYES_MOOD_DIZZY;
}

/* ---- Hands ---------------------------------------------------------------- */

static hand_pose_t pose_lerp(hand_pose_t a, hand_pose_t b, float k)
{
    return (hand_pose_t) {lerp(a.x, b.x, k), lerp(a.y, b.y, k), lerp(a.s, b.s, k)};
}

/* Where the hands are `ms` into the gesture playing. */
static void hands_step(void)
{
    const gesture_t *g = &s_gestures[s.gesture];
    if (g->n == 0) {
        s.hand[0] = (hand_pose_t) TUCK_L;
        s.hand[1] = (hand_pose_t) TUCK_R;
        return;
    }
    const int32_t was_ms = s.gesture_ms;
    s.gesture_ms += s.dt_ms;
    if (s.gesture_ms >= g->keys[g->n - 1].ms) {
        s.gesture = EYES_GESTURE_NONE;
        s.hand[0] = (hand_pose_t) TUCK_L;
        s.hand[1] = (hand_pose_t) TUCK_R;
        return;
    }
    int i = 0;
    while (i < g->n - 2 && s.gesture_ms >= g->keys[i + 1].ms) {
        i++;
    }
    const hand_key_t *a = &g->keys[i], *b = &g->keys[i + 1];
    float k = smooth(seg((float) s.gesture_ms, a->ms, b->ms));
    // The first stretch starts from wherever the hands were.
    hand_pose_t al = i == 0 ? s.hand_from[0] : a->l;
    hand_pose_t ar = i == 0 ? s.hand_from[1] : a->r;
    s.hand[0] = pose_lerp(al, b->l, k);
    s.hand[1] = pose_lerp(ar, b->r, k);
    s.hands_front = g->front;
    // The end of a stretch: a big blink.
    if (s.gesture == EYES_GESTURE_STRETCH && was_ms < 1500 && s.gesture_ms >= 1500) {
        eyes_blink();
    }
}

/* The body stretches tall while the hands reach out. */
static float stretch_amount(void)
{
    if (s.gesture != EYES_GESTURE_STRETCH) {
        return 0;
    }
    return -0.9f * sinf((float) M_PI * seg((float) s.gesture_ms, 250, 1500));
}

/* ---- One frame's layout --------------------------------------------------- */

/* The sphere's radius snaps to whole pixels, coarser when it is big: each
 * size is rendered once (look_ball), so fewer sizes mean fewer renders. */
static int32_t ball_radius(float r)
{
    const int32_t step = r > 160 ? 4 : r > 110 ? 2 : 1;
    int32_t q = (int32_t) lroundf(r / (float) step) * step;
    return q < 2 ? 2 : q;
}

/* How far away it looks: its distance, and the drift into the dark asleep. */
static float body_z(void)
{
    return fmaxf(Z_NEAR, s.mv.pos.z) * (1 + SLEEP_FAR * (1 - s.near));
}

static void body_layout(body_vis_t *v)
{
    memset(v, 0, sizeof(*v));
    const float t = (float) s.t_ms / 1000.0f;
    const float zf = body_z();
    // Perspective: the size and the offset from the middle shrink together
    // with the distance, so going away also draws it towards the middle.
    v->dist = s.size / zf;
    const float near1 = fminf(1, v->dist);
    // Breathing: a slow bob of a few pixels. Not asleep (far away and still),
    // not with the tint on (the halo covers the screen), not under the menu.
    const bool still = s.near < 0.5f || s.tint > 0.01f || s.size < 0.999f;
    const float bob = still ? 0 : 3 * sinf(t * 2.1f) * near1;
    // Scared, it trembles; knocked, it is pushed and springs back; leaning,
    // it sags towards the low side.
    const float shiver = s.mood == EYES_MOOD_SCARED && s.near > 0.5f ? 2.2f * sinf(t * 70) : 0;
    v->bx = CX + (int32_t) lroundf(s.mv.pos.x / zf + (s.kx + shiver + s.lx * 8 * s.near) * near1);
    v->by = CY + (int32_t) lroundf(s.mv.pos.y / zf + bob + (s.ky + s.ly * 6 * s.near) * near1);
    const float sq = clampf(s.squash + stretch_amount() + s.vsq, -1, 1);
    v->rx = ball_radius(R * (1 + LOOK_SQUASH * sq) * v->dist);
    v->ry = ball_radius(R * (1 - LOOK_SQUASH * sq) * v->dist);
    v->sx = (float) v->rx / R;
    v->sy = (float) v->ry / R;
    v->near = s.near;
    v->fog = FOG_FLOOR + (1 - FOG_FLOOR) / (1 + FOG * fmaxf(0, zf - 1));
    v->tint = s.tint;
    memcpy(v->tint_rgb, s.tint_rgb, sizeof(v->tint_rgb));
}

static void eye_layout(void)
{
    // Near the edge of the ball the eyes get narrower: it seems to turn.
    const float nx = 1 - 0.3f * s.gx * s.gx;
    const float ny = 1 - 0.25f * s.gy * s.gy;
    const float swell = 1 + 0.14f * s.level;
    const float w = s.cur.w * K * nx * swell * s.body.sx;
    const float h = s.cur.h * K * ny * swell * s.body.sy * s.lid;
    const float lift = EYE_SPACING * sinf(s.cur.tilt * (float) M_PI / 180.0f);
    // The lean rolls the eyes towards the low side, on top of the gaze.
    const float gx = clampf(s.gx + 0.6f * s.lx, -1, 1), gy = clampf(s.gy + 0.6f * s.ly, -1, 1);
    const float ox = gx * GAZE_X;
    const float oy = gy * GAZE_Y;
    for (int i = 0; i < 2; i++) {
        const int side = i == 0 ? -1 : 1;
        const int inner = -side; // the left eye's inner side is to the right
        eye_box_t *e = &s.eye[i];
        e->w = w;
        e->h = h * (i == 0 ? s.cur.left : s.cur.right);
        e->x = (float) s.body.bx + ((float) side * EYE_SPACING * nx + ox) * s.body.sx;
        e->y = (float) s.body.by + (FACE_Y + oy) * s.body.sy - (float) side * lift * s.body.dist;
        const float hh = e->h < 2 ? 2 : e->h;
        const float cut = -hh / 2 + s.cur.top * hh;
        e->top_l = cut - s.cur.slope * hh * 0.6f * (float) inner;
        e->top_r = cut + s.cur.slope * hh * 0.6f * (float) inner;
        e->bottom = s.cur.bottom;
        e->spiral = s.cur.spiral;
        // The two spirals turn opposite ways, the silly way.
        e->phase = (float) side * (float) s.t_ms / 1000.0f * 7.0f;
    }
}

/* ---- Eye sprites ---------------------------------------------------------- */

static float sd_round_box(float px, float py, float hw, float hh, float r)
{
    float qx = fabsf(px) - hw + r, qy = fabsf(py) - hh + r;
    float ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
    return hypotf(ox, oy) + fminf(fmaxf(qx, qy), 0) - r;
}

static void eye_key(const eye_box_t *e, int32_t key[6])
{
    key[0] = (int32_t) lroundf(e->w * 4);
    key[1] = (int32_t) lroundf(e->h * 4);
    key[2] = (int32_t) lroundf(e->top_l * 4);
    key[3] = (int32_t) lroundf(e->top_r * 4);
    key[4] = (int32_t) lroundf(e->bottom * 64);
    // A turning spiral is redrawn every 1/24 turn, a pill never for this.
    key[5] = e->spiral > 0.5f ? (int32_t) lroundf(e->phase * 24 / (2 * (float) M_PI)) : 0;
}

/* A dark spiral of two and a half turns in a circle of radius `rad`. */
static void spiral_render(lv_color32_t *px, int w, int h, float rad, float phase)
{
    const float pitch = rad / 2.5f; // the gap between the arms
    const float half = 1.7f;        // half the arm's width
    for (int y = 0; y < h; y++) {
        const float py = (float) y + 0.5f - (float) h / 2;
        for (int x = 0; x < w; x++, px++) {
            const float pxf = (float) x + 0.5f - (float) w / 2;
            const float r = hypotf(pxf, py);
            const float a = atan2f(py, pxf) + phase;
            const float u = r - pitch * a / (2 * (float) M_PI);
            const float d = fabsf(u - pitch * roundf(u / pitch)) - half;
            // The centre is a dot, and the spiral stops at the rim.
            float cov = clampf(0.5f - d, 0, 1);
            cov = fmaxf(cov, clampf(2.5f - r, 0, 1));
            cov *= clampf(rad + 0.5f - r, 0, 1);
            px->red = 29;
            px->green = 27;
            px->blue = 36;
            px->alpha = (uint8_t) (cov * 255);
        }
    }
}

/* Draw one eye into its sprite: a dark pill, a rounded box cut from above by
 * the top lid and from below by a circle (the smile). Antialiased from the
 * signed distance to that shape. */
static void eye_render(eye_sprite_t *es, const eye_box_t *e)
{
    const int margin = EYE_MARGIN;
    const float hw = fmaxf(e->w / 2, 1), hh = fmaxf(e->h / 2, 1);
    int w = (int) ceilf(2 * hw) + 2 * margin;
    int h = (int) ceilf(2 * hh) + 2 * margin;
    w = w > EYE_BUF_W ? EYE_BUF_W : w;
    h = h > EYE_BUF_H ? EYE_BUF_H : h;
    es->sp.img.header.w = w;
    es->sp.img.header.h = h;
    es->sp.img.header.stride = w * 4;
    es->sp.img.data_size = (uint32_t) (w * h * 4);
    es->sp.ax = w / 2;
    es->sp.ay = h / 2;

    if (e->spiral > 0.5f) {
        spiral_render((lv_color32_t *) es->sp.img.data, w, h, fminf(hw, hh), e->phase);
        return;
    }
    const float rr = fminf(hw, hh);
    const bool lid = fmaxf(e->top_l, e->top_r) > -hh + 0.5f;
    const float lx0 = -hw - 6, lx1 = hw + 6;
    const float slope = (e->top_r - e->top_l) / (lx1 - lx0);
    const float lid_norm = 1 / sqrtf(1 + slope * slope);
    const bool smile = e->bottom > 0.01f;
    const float rb = 1.8f * hw;
    const float smile_cy = hh + 2 - e->bottom * (0.6f * hh + 2) + rb;

    const uint8_t r = 29, g = 27, b = 36;

    lv_color32_t *px = (lv_color32_t *) es->sp.img.data;
    for (int y = 0; y < h; y++) {
        const float py = (float) y + 0.5f - (float) h / 2;
        for (int x = 0; x < w; x++, px++) {
            const float pxf = (float) x + 0.5f - (float) w / 2;
            float d = sd_round_box(pxf, py, hw, hh, rr);
            if (lid) {
                d = fmaxf(d, (e->top_l + (pxf - lx0) * slope - py) * lid_norm);
            }
            if (smile) {
                d = fmaxf(d, rb - hypotf(pxf, py - smile_cy));
            }
            px->red = r;
            px->green = g;
            px->blue = b;
            px->alpha = (uint8_t) (clampf(0.5f - d, 0, 1) * 255);
        }
    }
}

/* ---- Drawing -------------------------------------------------------------- */

static void draw_disc(lv_layer_t *layer, float x, float y, float r, lv_color_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = LV_RADIUS_CIRCLE;
    const lv_area_t a = {(int32_t) (x - r), (int32_t) (y - r), (int32_t) (x + r), (int32_t) (y + r)};
    lv_draw_rect(layer, &dsc, &a);
}

static void fill_rect(lv_layer_t *layer, float x1, float y1, float x2, float y2, lv_color_t color, int32_t radius,
                      lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = radius;
    const lv_area_t a = {(int32_t) x1, (int32_t) y1, (int32_t) x2, (int32_t) y2};
    lv_draw_rect(layer, &dsc, &a);
}

static void fill_triangle(lv_layer_t *layer, float x0, float y0, float x1, float y1, float x2, float y2,
                          lv_color_t color, lv_opa_t opa)
{
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = color;
    dsc.opa = opa;
    dsc.p[0].x = (int32_t) x0;
    dsc.p[0].y = (int32_t) y0;
    dsc.p[1].x = (int32_t) x1;
    dsc.p[1].y = (int32_t) y1;
    dsc.p[2].x = (int32_t) x2;
    dsc.p[2].y = (int32_t) y2;
    lv_draw_triangle(layer, &dsc);
}

static void draw_hands(lv_layer_t *layer)
{
    const look_t *lk = look_get();
    const float d = s.body.dist; // they go away with the body
    for (int i = 0; i < 2; i++) {
        const hand_pose_t *h = &s.hand[i];
        const int32_t x = s.body.bx + (int32_t) (h->x * K * d), y = s.body.by + (int32_t) (h->y * K * d);
        look_draw(layer, &lk->hand, x, y, h->s * d, h->s * d, 0, (lv_opa_t) (255 * s.near * s.body.fog),
                  lv_color_black());
    }
}

static void draw_badge(lv_layer_t *layer, const body_vis_t *v)
{
    if (v->tint <= 0.01f) {
        return;
    }
    const lv_opa_t opa = (lv_opa_t) (255 * v->tint);
    const float x = (float) v->bx + BADGE_X * v->sx, y = (float) v->by + BADGE_Y * v->sy;
    draw_disc(layer, x, y, 17.5f * K, lv_color_black(), opa);
    draw_disc(layer, x, y, 15 * K * s.badge_scale, rgb_color(v->tint_rgb), opa);
}

/* A small heart: two round tops and a point below, centred on (x, y). */
static void draw_heart(lv_layer_t *layer, float x, float y, lv_color_t color, lv_opa_t opa)
{
    fill_rect(layer, x - 13, y - 7, x + 1, y + 7, color, LV_RADIUS_CIRCLE, opa);
    fill_rect(layer, x - 1, y - 7, x + 13, y + 7, color, LV_RADIUS_CIRCLE, opa);
    fill_triangle(layer, x - 13, y + 1, x + 13, y + 1, x, y + 16, color, opa);
}

static lv_area_t box(float x, float y, float l, float t, float r, float b);

/* The parcel: in the hands while they carry it, once they are out. */
#define PARCEL_Y 112
#define PARCEL_HALF 30

static bool parcel_up(void)
{
    return s.carry_rgb != 0 && s.gesture == EYES_GESTURE_CARRY && s.gesture_ms >= 200 && s.gesture_ms < CARRY_HOLD_MS;
}

static void parcel_box(float *x, float *y, float *half)
{
    const float d = s.body.dist;
    *x = (float) s.body.bx;
    *y = (float) s.body.by + PARCEL_Y * K * d;
    *half = PARCEL_HALF * K * d;
}

/* A little box in the errand's colour, a pale ribbon across it and a bow. */
static void draw_parcel(lv_layer_t *layer)
{
    if (!parcel_up()) {
        return;
    }
    float x, y, h;
    parcel_box(&x, &y, &h);
    const lv_opa_t opa = (lv_opa_t) (255 * s.near * s.body.fog);
    const lv_color_t ribbon = lv_color_hex(0xFFF4E0);
    fill_rect(layer, x - h, y - h * 0.8f, x + h, y + h, lv_color_hex(s.carry_rgb), (int32_t) (h * 0.3f), opa);
    fill_rect(layer, x - h * 0.16f, y - h * 0.8f, x + h * 0.16f, y + h, ribbon, 0, opa);
    fill_rect(layer, x - h, y - h * 0.05f, x + h, y + h * 0.27f, ribbon, 0, opa);
    draw_disc(layer, x - h * 0.32f, y - h * 0.95f, h * 0.3f, ribbon, opa);
    draw_disc(layer, x + h * 0.32f, y - h * 0.95f, h * 0.3f, ribbon, opa);
}

static lv_area_t parcel_area(void)
{
    float x, y, h;
    parcel_box(&x, &y, &h);
    return box(x, y, h + 3, h * 1.3f + 3, h + 3, h + 3);
}

/* Where the overlays sit: next to the sphere, never over the eyes, following
 * it wherever it floats. Offsets at the resting distance. */
#define DOTS_DY (176 * K)
#define SWEAT_DX (128 * K)
#define SWEAT_DY (-120 * K)
/* Dizzy, three little stars circle its head on a flat ellipse. */
#define STARS_DY (-118 * K)
#define STARS_RX (120 * K)
#define STARS_RY (26 * K)
/* The z's rise from the small, far-away Yoru, in screen pixels. */
#define Z_DX 14
#define Z_DY (-40)

static float body_d(void)
{
    return fmaxf(0.25f, s.body.dist);
}

static void star_pos(int i, float t, float *x, float *y)
{
    const float a = t * 4.2f + (float) i * 2.0944f, d = body_d();
    *x = (float) s.body.bx + STARS_RX * d * cosf(a);
    *y = (float) s.body.by + (STARS_DY + STARS_RY * sinf(a)) * d;
}

static void dot_pos(int i, float *x, float *y)
{
    const float d = body_d();
    *x = (float) s.body.bx + (-30 + 30 * (float) i) * d;
    *y = (float) s.body.by + DOTS_DY * d;
}

static void z_pos(int i, float t, float *p, int32_t *x, int32_t *y)
{
    *p = fmodf(t * 0.35f + (float) i / 3.0f, 1.0f);
    *x = (int32_t) ((float) (s.body.bx + Z_DX) + *p * 30 + (float) i * 4);
    *y = (int32_t) ((float) (s.body.by + Z_DY) - *p * 60);
}

static void sweat_pos(float t, float *x, float *y)
{
    const float d = body_d();
    *x = (float) s.body.bx + SWEAT_DX * d;
    *y = (float) s.body.by + SWEAT_DY * d + fmodf(t * 40, 60) * d;
}

static void heart_pos(int i, float t, float *p, float *x, float *y)
{
    const float d = body_d();
    *p = fmodf(t * 0.4f + (float) i * 0.5f, 1.0f);
    *x = (float) s.body.bx + (i == 0 ? -HEART_X : HEART_X) * d;
    *y = (float) s.body.by - 40 * K * d - *p * 90 * d;
}

/* A small four-pointed star. */
static void draw_star(lv_layer_t *layer, float x, float y, float r, lv_color_t c)
{
    fill_triangle(layer, x - r, y, x + r, y, x, y - 2.2f * r, c, LV_OPA_COVER);
    fill_triangle(layer, x - r, y, x + r, y, x, y + 2.2f * r, c, LV_OPA_COVER);
    fill_triangle(layer, x, y - r, x, y + r, x - 2.2f * r, y, c, LV_OPA_COVER);
    fill_triangle(layer, x, y - r, x, y + r, x + 2.2f * r, y, c, LV_OPA_COVER);
}

static void draw_overlays(lv_layer_t *layer)
{
    const float t = (float) s.t_ms / 1000.0f;
    if (s.mood == EYES_MOOD_DIZZY) {
        for (int i = 0; i < 3; i++) {
            float x, y;
            star_pos(i, t, &x, &y);
            draw_star(layer, x, y, 4, lv_color_hex(0xFFD45C));
        }
        return;
    }
    if (s.mood == EYES_MOOD_THINKING) {
        for (int i = 0; i < 3; i++) {
            float a = (sinf(t * 5 - (float) i * 0.9f) + 1) / 2, x, y;
            dot_pos(i, &x, &y);
            fill_rect(layer, x - 8, y - 8, x + 8, y + 8, lv_color_hex(0x3D8BFF), LV_RADIUS_CIRCLE,
                      (lv_opa_t) (70 + 185 * a));
        }
    } else if (s.mood == EYES_MOOD_ASLEEP) {
        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        dsc.color = lv_color_hex(0x8C90A6);
        for (int i = 0; i < 3; i++) {
            float p;
            int32_t x, y;
            z_pos(i, t, &p, &x, &y);
            dsc.font = i == 1 ? &lv_font_montserrat_28 : &lv_font_montserrat_14;
            // They only show once it is far away.
            dsc.opa = (lv_opa_t) (255 * (1 - p) * (1 - s.near));
            dsc.text = "z";
            const lv_area_t a = {x, y, x + 40, y + 48};
            lv_draw_label(layer, &dsc, &a);
        }
    } else if (s.mood == EYES_MOOD_HOT) {
        const lv_color_t blue = lv_color_hex(0x6FD3FF);
        float x, y;
        sweat_pos(t, &x, &y);
        fill_triangle(layer, x, y - 16, x - 9, y + 2, x + 9, y + 2, blue, LV_OPA_COVER);
        fill_rect(layer, x - 9, y - 5, x + 9, y + 13, blue, LV_RADIUS_CIRCLE, LV_OPA_COVER);
    } else if (s.mood == EYES_MOOD_LOVING) {
        // Two hearts float up beside the body and fade into the black.
        for (int i = 0; i < 2; i++) {
            float p, x, y;
            heart_pos(i, t, &p, &x, &y);
            draw_heart(layer, x, y, lv_color_hex(0xFF7AB8), (lv_opa_t) (255 * (1 - p)));
        }
    }
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const look_t *lk = look_get();
    const body_vis_t *v = &s.body;
    const lv_color_t black = lv_color_black();

    if (v->tint > 0.01f) {
        const float d = fminf(1, v->dist);
        look_draw(layer, &lk->halo, v->bx, v->by, d, d, 0, (lv_opa_t) (90 * v->tint), rgb_color(v->tint_rgb));
    }
    if (!s.hands_front) {
        draw_hands(layer);
    }
    const lv_opa_t body_opa = (lv_opa_t) (255 * v->fog);
    if (s.ball == NULL) {
        s.ball = look_ball(v->rx, v->ry, s.frame); // shown again before the next frame
    }
    if (s.ball != NULL) {
        look_draw(layer, s.ball, v->bx, v->by, 1, 1, 0, body_opa, black);
    } else {
        look_draw(layer, &lk->day, v->bx, v->by, v->sx, v->sy, 0, body_opa, black);
    }
    if (v->tint > 0.01f) {
        look_draw(layer, &lk->tint, v->bx, v->by, v->sx, v->sy, 0, (lv_opa_t) (217 * v->tint), rgb_color(v->tint_rgb));
    }
    draw_badge(layer, v);
    for (int i = 0; i < 2; i++) {
        look_draw(layer, &s.eye_sp[i].sp, (int32_t) lroundf(s.eye[i].x), (int32_t) lroundf(s.eye[i].y), 1, 1, 0,
                  LV_OPA_COVER, black);
    }
    draw_parcel(layer);
    if (s.hands_front) {
        draw_hands(layer);
    }
    draw_overlays(layer);
}

/* ---- What to redraw ------------------------------------------------------- */

static lv_area_t body_area(const body_vis_t *v)
{
    const int32_t rx = v->rx + 4, ry = v->ry + 4;
    lv_area_t a = {v->bx - rx, v->by - ry, v->bx + rx, v->by + ry};
    if (v->tint > 0.01f) {
        // The halo, a little wider than the sphere.
        const float d = fminf(1, v->dist);
        const lv_area_t h = look_area(&look_get()->halo, v->bx, v->by, d, d, false);
        a = (lv_area_t) {LV_MIN(a.x1, h.x1), LV_MIN(a.y1, h.y1), LV_MAX(a.x2, h.x2), LV_MAX(a.y2, h.y2)};
    }
    return a;
}

static lv_area_t eye_area(int i)
{
    const look_sprite_t *sp = &s.eye_sp[i].sp;
    return look_area(sp, (int32_t) lroundf(s.eye[i].x), (int32_t) lroundf(s.eye[i].y), 1, 1, false);
}

static lv_area_t hand_area(int i)
{
    const hand_pose_t *h = &s.hand[i];
    const float d = s.body.dist;
    return look_area(&look_get()->hand, s.body.bx + (int32_t) (h->x * K * d), s.body.by + (int32_t) (h->y * K * d),
                     fmaxf(h->s * d, 0.02f), fmaxf(h->s * d, 0.02f), false);
}

static lv_area_t badge_area(void)
{
    const int32_t x = s.body.bx + (int32_t) (BADGE_X * s.body.sx), y = s.body.by + (int32_t) (BADGE_Y * s.body.sy);
    return (lv_area_t) {x - 16, y - 16, x + 16, y + 16};
}

static lv_area_t box(float x, float y, float l, float t, float r, float b)
{
    return (lv_area_t) {(int32_t) (x - l), (int32_t) (y - t), (int32_t) (x + r), (int32_t) (y + b)};
}

/* The pieces of the mood's overlay this frame, one box each (with a margin for
 * antialiasing), so only what moves gets redrawn. Mirrors draw_overlays().
 * Returns how many. */
static int overlay_areas(lv_area_t out[MAX_OVERLAY_RECTS])
{
    const float t = (float) s.t_ms / 1000.0f;
    float x, y, p;
    if (s.mood == EYES_MOOD_DIZZY) {
        for (int i = 0; i < 3; i++) {
            star_pos(i, t, &x, &y);
            out[i] = box(x, y, 11, 11, 11, 11);
        }
        return 3;
    }
    if (s.mood == EYES_MOOD_THINKING) {
        for (int i = 0; i < 3; i++) {
            dot_pos(i, &x, &y);
            out[i] = box(x, y, 10, 10, 10, 10);
        }
        return 3;
    }
    if (s.mood == EYES_MOOD_ASLEEP) {
        for (int i = 0; i < 3; i++) {
            int32_t zx, zy;
            z_pos(i, t, &p, &zx, &zy);
            out[i] = (lv_area_t) {zx - 2, zy - 2, zx + 30, zy + 36};
        }
        return 3;
    }
    if (s.mood == EYES_MOOD_HOT) {
        sweat_pos(t, &x, &y);
        out[0] = box(x, y, 12, 19, 12, 16);
        return 1;
    }
    if (s.mood == EYES_MOOD_LOVING) {
        for (int i = 0; i < 2; i++) {
            heart_pos(i, t, &p, &x, &y);
            out[i] = box(x, y, 16, 10, 16, 19);
        }
        return 2;
    }
    return 0;
}

static bool areas_equal(const lv_area_t *a, const lv_area_t *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

/* Redraws where something was and where it is now: one box if the two touch
 * (a small step), two if it jumped (a z starting over at the bottom). */
static void invalidate_move(const lv_area_t *was, const lv_area_t *now)
{
    bool touch = was->x1 <= now->x2 && now->x1 <= was->x2 && was->y1 <= now->y2 && now->y1 <= was->y2;
    if (touch) {
        lv_area_t both;
        both.x1 = LV_MIN(was->x1, now->x1);
        both.y1 = LV_MIN(was->y1, now->y1);
        both.x2 = LV_MAX(was->x2, now->x2);
        both.y2 = LV_MAX(was->y2, now->y2);
        lv_obj_invalidate_area(s.obj, &both);
    } else {
        lv_obj_invalidate_area(s.obj, was);
        lv_obj_invalidate_area(s.obj, now);
    }
}

/* ---- Moving through the space ---------------------------------------------- */

/* A Catmull-Rom curve: through every point, smooth at each. */
static eyes_pos_t catmull(const eyes_pos_t *p0, const eyes_pos_t *p1, const eyes_pos_t *p2, const eyes_pos_t *p3,
                          float t)
{
    const float t2 = t * t, t3 = t2 * t;
    const float *a = &p0->x, *b = &p1->x, *c = &p2->x, *d = &p3->x;
    eyes_pos_t r;
    float *o = &r.x;
    for (int i = 0; i < 3; i++) {
        o[i] = 0.5f * (2 * b[i] + (c[i] - a[i]) * t + (2 * a[i] - 5 * b[i] + 4 * c[i] - d[i]) * t2
                       + (3 * b[i] - a[i] - 3 * c[i] + d[i]) * t3);
    }
    return r;
}

/* How far along the path at k (0..1 of the time): the dart winds up a little
 * before it goes and overshoots a little at the end. */
static float path_ease(eyes_path_t style, float k)
{
    switch (style) {
    case EYES_PATH_DRIFT:
        return 0.5f - 0.5f * cosf((float) M_PI * k);
    case EYES_PATH_DART: {
        const float c = 1.1f * 1.525f;
        return k < 0.5f ? (4 * k * k * ((c + 1) * 2 * k - c)) / 2
                        : ((2 * k - 2) * (2 * k - 2) * ((c + 1) * (2 * k - 2) + c) + 2) / 2;
    }
    case EYES_PATH_HOP:
        return k;
    default:
        return k * k * k * (k * (k * 6 - 15) + 10);
    }
}

static void mover_path(mover_t *m, const eyes_pos_t *pts, int n, uint32_t ms, eyes_path_t style)
{
    n = n > EYES_PATH_POINTS ? EYES_PATH_POINTS : n;
    m->follow = false;
    if (n <= 0) {
        m->n = 0;
        return;
    }
    if (ms == 0) {
        m->pos = pts[n - 1];
        m->vel = (eyes_pos_t) {0};
        m->n = 0;
        return;
    }
    m->pts[0] = m->pos;
    memcpy(&m->pts[1], pts, (size_t) n * sizeof(*pts));
    m->n = n + 1;
    m->ms = 0;
    m->dur_ms = (int32_t) ms;
    m->style = style;
    m->seg = -1;
}

/* One frame on; returns MOVED_* for a hop's takeoff and landing. */
static int mover_step(mover_t *m, int32_t dt_ms)
{
    const eyes_pos_t was = m->pos;
    const float dt = (float) dt_ms / 1000.0f;
    int ev = 0;
    if (m->n > 0) {
        m->ms += dt_ms;
        const float k = clampf((float) m->ms / (float) m->dur_ms, 0, 1);
        const int last = m->n - 1;
        // The dart's wind-up and overshoot run off the ends of the curve.
        const float u = path_ease(m->style, k) * (float) last;
        const int i = u < 0 ? 0 : (u >= (float) last ? last - 1 : (int) u);
        const float t = u - (float) i;
        const eyes_pos_t *p = m->pts;
        m->pos = catmull(&p[i > 0 ? i - 1 : 0], &p[i], &p[i + 1], &p[i + 2 <= last ? i + 2 : last], t);
        if (m->style == EYES_PATH_HOP) {
            // Each stretch is a hop: up and down again, the height on screen.
            const float z = fmaxf(Z_NEAR, m->pos.z);
            const float len = hypotf(p[i + 1].x - p[i].x, p[i + 1].y - p[i].y) / z;
            const float tt = clampf(t, 0, 1);
            m->pos.y -= (HOP_ARC + 0.3f * len) * z * 4 * tt * (1 - tt);
            if (i != m->seg) {
                ev |= (m->seg >= 0 ? MOVED_LANDED : 0) | MOVED_TAKEOFF;
                m->seg = i;
            }
        }
        if (k >= 1) {
            m->pos = p[last];
            m->n = 0;
            ev |= m->style == EYES_PATH_HOP ? MOVED_LANDED : 0;
        }
        if (dt > 0) {
            m->vel = (eyes_pos_t) {(m->pos.x - was.x) / dt, (m->pos.y - was.y) / dt, (m->pos.z - was.z) / dt};
        }
        if (m->n == 0) {
            m->vel = (eyes_pos_t) {0};
        }
    } else if (m->follow) {
        // A damped spring, in 5 ms slices so a slow frame doesn't make it explode.
        const float w = sqrtf(m->stiff), damp = 2 * 0.8f * w;
        float *pos = &m->pos.x, *vel = &m->vel.x;
        const float *to = &m->to.x;
        for (int32_t left = dt_ms; left > 0; left -= 5) {
            const float h = (float) LV_MIN(left, 5) / 1000.0f;
            for (int c = 0; c < 3; c++) {
                vel[c] += (m->stiff * (to[c] - pos[c]) - damp * vel[c]) * h;
                pos[c] += vel[c] * h;
            }
        }
    }
    m->pos.z = fmaxf(Z_NEAR, m->pos.z);
    return ev;
}

static bool mover_busy(const mover_t *m)
{
    if (m->n > 0) {
        return true;
    }
    if (!m->follow) {
        return false;
    }
    const float z = fmaxf(Z_NEAR, m->pos.z);
    const float off = hypotf(m->to.x - m->pos.x, m->to.y - m->pos.y) / z + 60 * fabsf(m->to.z - m->pos.z);
    return off > 4 || hypotf(m->vel.x, m->vel.y) / z > 30;
}

/* Moving, the eyes lead the way; coming up to the glass, they look at you. False when nothing calls for a look. */
static bool lead_gaze(float *gx, float *gy)
{
    const float z = body_z();
    const float vx = s.mv.vel.x / z, vy = s.mv.vel.y / z;
    const float speed = hypotf(vx, vy);
    if (s.mv.vel.z < -0.35f && speed < 150) {
        *gx = 0;
        *gy = -0.1f;
        return true;
    }
    if (speed < 70) {
        return false;
    }
    *gx = clampf(vx / 320, -1, 1) * 0.9f;
    *gy = clampf(vy / 320, -1, 1) * 0.7f;
    return true;
}

/* ---- Easing --------------------------------------------------------------- */

/* Ease every number towards the mood and the state; true if any of the
 * eye's numbers visibly moved. */
static bool step(void)
{
    const eye_shape_t *want = &s_moods[s.mood];
    eye_shape_t before = s.cur;
    float *c = (float *) &s.cur;
    const float *w = (const float *) want;
    for (size_t i = 0; i < sizeof(eye_shape_t) / sizeof(float); i++) {
        c[i] = lerp(c[i], w[i], ease(0.16f));
        if (fabsf(c[i] - w[i]) < 0.05f) {
            c[i] = w[i]; // close enough: a still face stops redrawing
        }
    }

    // Saccades: a new place to look every 1.2-3.6 s, unless held.
    if (!s.held) {
        s.next_look_ms -= s.dt_ms;
        if (s.next_look_ms <= 0) {
            s.next_look_ms = 1200 + (int32_t) (frand() * 2400);
            s.tgx = (frand() * 2 - 1) * 0.8f;
            s.tgy = (frand() * 2 - 1) * 0.5f;
            if (s.mood == EYES_MOOD_THINKING) {
                s.tgx = 0.55f + frand() * 0.2f;
                s.tgy = -0.8f;
            } else if (s.mood == EYES_MOOD_SLEEPY || s.mood == EYES_MOOD_ASLEEP || s.mood == EYES_MOOD_SAD) {
                s.tgx *= 0.3f;
                s.tgy = 0.6f;
            } else if (s.mood == EYES_MOOD_BORED) {
                // Off to the side, as if anything else were more interesting.
                s.tgx = s.tgx < 0 ? -0.75f : 0.75f;
                s.tgy = 0.1f;
            }
        }
    }
    float lgx, lgy;
    if (!s.held && s.mood != EYES_MOOD_DIZZY && s.mood != EYES_MOOD_PEEK && lead_gaze(&lgx, &lgy)) {
        s.tgx = lgx;
        s.tgy = lgy;
        s.next_look_ms = LV_MAX(s.next_look_ms, 700);
    }
    if (s.mood == EYES_MOOD_DIZZY) {
        // The eyes roll round and round.
        const float a = (float) s.t_ms / 1000.0f * 5.5f;
        s.tgx = 0.45f * cosf(a);
        s.tgy = 0.35f * sinf(a);
    } else if (s.mood == EYES_MOOD_PEEK) {
        s.tgx = 0;
        s.tgy = -1; // up at the menu
    }
    const float gk = s.held ? 0.3f : 0.2f;
    float gx = lerp(s.gx, s.tgx, ease(gk));
    float gy = lerp(s.gy, s.tgy, ease(gk));

    // Blinks every 2.2-5.2 s, one in five doubled; none while asleep.
    if (s.mood != EYES_MOOD_ASLEEP) {
        s.next_blink_ms -= s.dt_ms;
        if (s.next_blink_ms <= 0) {
            eyes_blink();
        }
    }
    float lid;
    if (s.blink_ms > 0) {
        s.blink_ms -= s.dt_ms;
        lid = lerp(s.lid, 0.06f, ease(0.55f));
    } else {
        lid = lerp(s.lid, 1.0f, ease(0.35f));
    }
    float squash = s.squash * powf(0.85f, s.frames);
    // Quick to rise with a syllable, slower to fall: it reads as talking.
    float level = lerp(s.level, s.tlevel, ease(s.tlevel > s.level ? 0.5f : 0.2f));
    if (level < 0.005f) {
        level = 0;
    }

    bool moved = fabsf(gx - s.gx) > 0.002f || fabsf(gy - s.gy) > 0.002f || fabsf(lid - s.lid) > 0.002f
                 || fabsf(squash) > 0.002f || fabsf(level - s.level) > 0.002f || memcmp(&before, &s.cur, sizeof(before)) != 0;
    s.level = level;
    s.gx = gx;
    s.gy = gy;
    s.lid = lid;
    s.squash = fabsf(squash) < 0.002f ? 0 : squash;

    // Here or far away, the tint and its colour.
    if (s.mood == EYES_MOOD_ASLEEP) {
        s.near = fmaxf(0, s.near - (float) s.dt_ms / SLEEP_AWAY_MS);
    } else {
        s.near = fminf(1, s.near + (float) s.dt_ms / SLEEP_BACK_MS);
    }
    const float want_size = s.mood == EYES_MOOD_PEEK || s.mood == EYES_MOOD_WAITING ? MENU_SIZE
                            : s.status != EYES_STATUS_IDLE                         ? STATUS_SIZE
                                                                                   : 1;
    const float size_step = (float) s.dt_ms * (1 - MENU_SIZE) / MENU_SIZE_MS;
    s.size = want_size < s.size ? fmaxf(want_size, s.size - size_step) : fminf(want_size, s.size + size_step);
    const float want_tint = s.status == EYES_STATUS_IDLE ? 0 : 1;
    s.tint = fabsf(want_tint - s.tint) < 0.01f ? want_tint : lerp(s.tint, want_tint, ease(0.12f));
    if (s.status != EYES_STATUS_IDLE) {
        const uint32_t rgb = s_status_rgb[s.status];
        const float to[3] = {(float) (rgb >> 16), (float) ((rgb >> 8) & 0xFF), (float) (rgb & 0xFF)};
        for (int i = 0; i < 3; i++) {
            s.tint_rgb[i] = fabsf(to[i] - s.tint_rgb[i]) < 2 ? to[i] : lerp(s.tint_rgb[i], to[i], ease(0.15f));
        }
    }
    // While working, the badge breathes.
    s.badge_scale = s.status == EYES_STATUS_THINKING ? 0.89f + 0.11f * cosf((float) s.t_ms / 1000.0f * 5.2f) : 1;

    // The knock: a damped spring, stepped in 5 ms slices so a slow frame
    // doesn't make it explode.
    const bool knocked = fabsf(s.kx) > 0.3f || fabsf(s.ky) > 0.3f || fabsf(s.kvx) > 0.3f || fabsf(s.kvy) > 0.3f;
    for (int32_t left = knocked ? s.dt_ms : 0; left > 0; left -= 5) {
        const float h = (float) LV_MIN(left, 5) / 1000.0f;
        s.kvx += (-260 * s.kx - 9 * s.kvx) * h;
        s.kvy += (-260 * s.ky - 9 * s.kvy) * h;
        s.kx += s.kvx * h;
        s.ky += s.kvy * h;
    }
    if (!knocked) {
        s.kx = s.ky = s.kvx = s.kvy = 0;
    }
    const float lx = lerp(s.lx, s.tlx, ease(0.12f)), ly = lerp(s.ly, s.tly, ease(0.12f));
    moved = moved || fabsf(lx - s.lx) > 0.002f || fabsf(ly - s.ly) > 0.002f;
    s.lx = fabsf(lx - s.tlx) < 0.002f ? s.tlx : lx;
    s.ly = fabsf(ly - s.tly) < 0.002f ? s.tly : ly;

    // Through the space: a hop stretches as it leaves and squashes as it
    // lands; fast, it stretches along the way it goes.
    const int ev = mover_step(&s.mv, s.dt_ms);
    if (ev & MOVED_LANDED) {
        s.squash = 0.75f;
    } else if (ev & MOVED_TAKEOFF) {
        s.squash = -0.6f;
    }
    const float z = body_z();
    const float want_vsq = clampf((fabsf(s.mv.vel.x) - fabsf(s.mv.vel.y)) / z / SPEED_STRETCH, -0.5f, 0.5f);
    s.vsq = fabsf(want_vsq - s.vsq) < 0.01f ? want_vsq : lerp(s.vsq, want_vsq, ease(0.3f));

    hands_step();
    return moved;
}

static void frame_cb(lv_timer_t *timer)
{
    const uint32_t elapsed = lv_tick_elaps(s.last_tick);
    s.last_tick = lv_tick_get();
    s.dt_ms = (int32_t) LV_CLAMP(1, elapsed, EYES_MAX_STEP_MS);
    s.frames = (float) s.dt_ms / EYES_FRAME_MS;
    s.t_ms += (uint32_t) s.dt_ms;
    const bool eyes_moved = step();

    const body_vis_t was = s.body;
    body_layout(&s.body);
    eye_layout();
    s.frame++;
    // Hidden under the menu or the dark it isn't drawn: no new sizes to
    // render (the menu draws its own Yoru meanwhile).
    s.ball = lv_obj_has_flag(s.obj, LV_OBJ_FLAG_HIDDEN) ? NULL : look_ball(s.body.rx, s.body.ry, s.frame);

    // The body: the sphere, the tint, the halo.
    const bool body_changed = !s.body_valid || memcmp(&was, &s.body, sizeof(was)) != 0;
    if (body_changed) {
        lv_area_t now = body_area(&s.body);
        if (s.body_valid) {
            invalidate_move(&s.body_drawn, &now);
        } else {
            lv_obj_invalidate_area(s.obj, &now);
        }
        s.body_drawn = now;
        s.body_valid = true;
    }

    // The eyes: a new picture only when their shape changed.
    for (int i = 0; i < 2; i++) {
        eye_sprite_t *es = &s.eye_sp[i];
        int32_t key[6];
        eye_key(&s.eye[i], key);
        const bool reshaped = memcmp(key, es->key, sizeof(key)) != 0;
        if (reshaped) {
            memcpy(es->key, key, sizeof(key));
            eye_render(es, &s.eye[i]);
        }
        lv_area_t now = eye_area(i);
        if (!es->valid || reshaped || eyes_moved || !areas_equal(&now, &es->drawn)) {
            if (es->valid) {
                invalidate_move(&es->drawn, &now);
            } else {
                lv_obj_invalidate_area(s.obj, &now);
            }
            es->drawn = now;
            es->valid = true;
        }
    }

    // The hands, each on its own.
    for (int i = 0; i < 2; i++) {
        lv_area_t now = hand_area(i);
        if (!areas_equal(&now, &s.hand_drawn[i]) || s.gesture != EYES_GESTURE_NONE) {
            invalidate_move(&s.hand_drawn[i], &now);
            s.hand_drawn[i] = now;
        }
    }

    // The parcel goes where the body goes, and once more as it disappears.
    const bool parcel = parcel_up();
    if (parcel || s.carry_shown) {
        lv_area_t now = parcel_area();
        if (s.carry_shown) {
            invalidate_move(&s.carry_drawn, &now);
        } else {
            lv_obj_invalidate_area(s.obj, &now);
        }
        s.carry_drawn = now;
        s.carry_shown = parcel;
    }

    // The badge breathes without redrawing the whole sphere.
    if (s.status == EYES_STATUS_THINKING && s.tint > 0.01f) {
        lv_area_t now = badge_area();
        invalidate_move(&s.badge_drawn, &now);
        s.badge_drawn = now;
    }

    // Each overlay piece against its own last place; a piece the mood no
    // longer has is cleared once.
    if (has_overlay(s.mood) || s.over_count > 0) {
        lv_area_t over[MAX_OVERLAY_RECTS];
        int n = overlay_areas(over);
        for (int i = 0; i < LV_MAX(n, s.over_count); i++) {
            if (i < n && i < s.over_count) {
                invalidate_move(&s.over_drawn[i], &over[i]);
            } else if (i < n) {
                lv_obj_invalidate_area(s.obj, &over[i]);
            } else {
                lv_obj_invalidate_area(s.obj, &s.over_drawn[i]);
            }
            if (i < n) {
                s.over_drawn[i] = over[i];
            }
        }
        s.over_count = n;
    }
}

lv_obj_t *eyes_create(lv_obj_t *parent)
{
    memset(&s, 0, sizeof(s));
    s.mood = EYES_MOOD_NEUTRAL;
    s.cur = s_moods[EYES_MOOD_NEUTRAL];
    s.lid = 0.02f; // opens on the first frames
    s.next_blink_ms = 2500;
    s.next_look_ms = 1500;
    s.near = 1;
    s.size = 1;
    s.mv.pos = (eyes_pos_t) {0, 0, 1};
    s.badge_scale = 1;
    s.last_tick = lv_tick_get();
    s.hand[0] = (hand_pose_t) TUCK_L;
    s.hand[1] = (hand_pose_t) TUCK_R;
    for (int i = 0; i < 2; i++) {
        look_sprite_t *sp = &s.eye_sp[i].sp;
        sp->img.header.magic = LV_IMAGE_HEADER_MAGIC;
        sp->img.header.cf = LV_COLOR_FORMAT_ARGB8888;
        sp->img.data = heap_caps_calloc(1, EYE_BUF_W * EYE_BUF_H * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s.eye_sp[i].key[0] = -1; // drawn on the first frame
        s.hand_drawn[i] = hand_area(i);
    }

    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    s.obj = lv_obj_create(parent);
    lv_obj_remove_style_all(s.obj);
    lv_obj_set_size(s.obj, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_center(s.obj);
    lv_obj_add_flag(s.obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s.obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s.obj, draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    lv_timer_create(frame_cb, EYES_FRAME_MS, NULL);
    return s.obj;
}

void eyes_set_mood(eyes_mood_t mood)
{
    if (mood >= EYES_MOOD_COUNT || mood == s.mood) {
        return;
    }
    s.mood = mood;
    s.next_look_ms = 0; // look somewhere that suits the mood
}

eyes_mood_t eyes_get_mood(void)
{
    return s.mood;
}

const char *eyes_mood_name(eyes_mood_t mood)
{
    return mood < EYES_MOOD_COUNT ? s_mood_names[mood] : "?";
}

void eyes_look_at(float x, float y)
{
    s.held = true;
    s.tgx = fmaxf(-1, fminf(1, x));
    s.tgy = fmaxf(-1, fminf(1, y));
}

void eyes_look_around(void)
{
    s.held = false;
    s.next_look_ms = 600;
}

void eyes_set_level(float level)
{
    s.tlevel = fmaxf(0, fminf(1, level));
}

void eyes_poke(void)
{
    s.squash = 1;
}

void eyes_knock(float dx, float dy)
{
    // Pushed off by up to half its size: a quick jolt that springs back.
    s.kvx += dx * R * 9;
    s.kvy += dy * R * 9;
}

void eyes_lean(float x, float y)
{
    s.tlx = clampf(x, -1, 1);
    s.tly = clampf(y, -1, 1);
}

void eyes_blink(void)
{
    s.blink_ms = 120;
    s.next_blink_ms = frand() < 0.2f ? 300 : 2200 + (int32_t) (frand() * 3000);
}

void eyes_set_status(eyes_status_t status)
{
    if (status <= EYES_STATUS_ERROR) {
        if (s.status == EYES_STATUS_IDLE && s.tint < 0.05f && status != EYES_STATUS_IDLE) {
            // From nothing: start in the new colour rather than fade from the old one.
            const uint32_t rgb = s_status_rgb[status];
            s.tint_rgb[0] = (float) (rgb >> 16);
            s.tint_rgb[1] = (float) ((rgb >> 8) & 0xFF);
            s.tint_rgb[2] = (float) (rgb & 0xFF);
        }
        s.status = status;
    }
}

eyes_status_t eyes_get_status(void)
{
    return s.status;
}

void eyes_gesture(eyes_gesture_t gesture)
{
    if (gesture >= EYES_GESTURE_COUNT) {
        return;
    }
    s.hand_from[0] = s.hand[0];
    s.hand_from[1] = s.hand[1];
    s.gesture = gesture;
    s.gesture_ms = 0;
}

void eyes_carry(uint32_t rgb)
{
    s.carry_rgb = rgb;
}

const char *eyes_gesture_name(eyes_gesture_t gesture)
{
    return gesture < EYES_GESTURE_COUNT ? s_gestures[gesture].name : "?";
}

/* ---- Moving through the space ---------------------------------------------- */

void eyes_move(const eyes_pos_t *pts, int n, uint32_t ms, eyes_path_t path)
{
    mover_path(&s.mv, pts, n, ms, path);
}

void eyes_move_to(float x, float y, float z, uint32_t ms, eyes_path_t path)
{
    const eyes_pos_t p = {x, y, z};
    eyes_move(&p, 1, ms, path);
}

void eyes_follow(float x, float y, float z, float stiffness)
{
    s.mv.n = 0;
    s.mv.follow = true;
    s.mv.to = (eyes_pos_t) {x, y, fmaxf(Z_NEAR, z)};
    s.mv.stiff = clampf(stiffness, 1, 200);
}

bool eyes_moving(void)
{
    return mover_busy(&s.mv);
}

eyes_pos_t eyes_where(void)
{
    return s.mv.pos;
}

eyes_pos_t eyes_unproject(int32_t x, int32_t y, float z)
{
    return (eyes_pos_t) {(float) (x - CX) * z, (float) (y - CY) * z, z};
}

bool eyes_hit(int32_t x, int32_t y)
{
    const float r = R * s.body.dist + 18;
    return hypotf((float) (x - s.body.bx), (float) (y - s.body.by)) <= r;
}

