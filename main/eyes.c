#include "eyes.h"

#include <math.h>
#include <string.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
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
/* The follow-up's ring: this far out from the sphere, this thick, in steps
 * of this many degrees (it redraws only when it has shrunk by one). */
#define COUNTDOWN_GAP 14
#define COUNTDOWN_W 5
#define COUNTDOWN_STEP 3
#define COUNTDOWN_RGB 0x9ECBFF
/* Charging, a thinner ring where the follow-up's goes: it fades in, fills
 * up to the charge in CHARGE_FILL_MS and a spark runs along it every
 * CHARGE_SPARK_MS, from the top round to the end of the charge. */
#define CHARGE_W 3
#define CHARGE_RGB 0x3DDC84
#define CHARGE_FADE_MS 400
#define CHARGE_FILL_MS 1200
#define CHARGE_SPARK_MS 2600
#define CHARGE_SPARK_STEP 6
/* Plugged in: a bolt from the bottom edge up into the body, in this long;
 * it zigzags this far either side, a turn every ZAP_STEP pixels, and shows
 * ZAP_TAIL pixels of itself. */
#define ZAP_MS 480
#define ZAP_RGB 0xFFE45C
#define ZAP_ZIG 12
#define ZAP_STEP 38
#define ZAP_TAIL 120
/* The low battery: by its head, opposite the badge, clear of the sphere. */
#define LOWBAT_DX (128 * K)
#define LOWBAT_DY (-128 * K)
#define LOWBAT_RGB 0xFFB340
#define LOWBAT_EMPTY_RGB 0xFF4D5E
#define LOWBAT_BLINK_MS 600
/* Where a frame's time goes (debug/perf): the frame's own logic, the
 * sprites rendered on the fly, and each layer drawn. */
typedef enum {
    PROF_LOGIC,
    PROF_BALL,
    PROF_EYE_RENDER,
    PROF_HALO,
    PROF_GLOW,
    PROF_RINGS,
    PROF_HANDS,
    PROF_BODY,
    PROF_TINT,
    PROF_BADGE,
    PROF_EYES,
    PROF_PROP,
    PROF_OVERLAYS,
    PROF_SPARKS,
    PROF_REST,
    PROF_COUNT,
} prof_id_t;

static const char *const PROF_NAMES[PROF_COUNT] = {
    "logic", "ball_render", "eye_render", "halo", "glow", "rings", "hands", "body",
    "tint", "badge", "eyes", "prop", "overlays", "sparks", "rest",
};

static struct {
    int64_t since_us;
    uint64_t us[PROF_COUNT];
    uint32_t frames, draws;
    uint64_t px;
} s_prof;

#define PROF(id, stmt)                                                     \
    do {                                                                   \
        const int64_t prof_t0_ = esp_timer_get_time();                     \
        stmt;                                                              \
        s_prof.us[id] += (uint64_t) (esp_timer_get_time() - prof_t0_);     \
    } while (0)

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
    [EYES_MOOD_DELIGHTED] = {29, 55, 0, 0, 1, 0, 1, 1},
    [EYES_MOOD_SMITTEN] = {29, 57, 0, 0, 1, 0, 1, 1},
    [EYES_MOOD_STARRY] = {30, 66, 0, 0, 0, 0, 1.04f, 1.04f},
    [EYES_MOOD_DREAMY] = {27, 57, 0.36f, -0.12f, 0.25f, 4, 1, 1},
};

static const char *const s_mood_names[EYES_MOOD_COUNT] = {
    "neutral", "happy", "curious", "thinking", "grumpy", "hot", "sleepy", "asleep", "sad", "loving", "surprised", "bored", "listening", "speaking", "dizzy", "scared", "peek", "waiting", "delighted", "smitten", "starry", "dreamy",
};

/* The status colours: blue thinking, green done, red error. */
static const uint32_t s_status_rgb[] = {0x3D8BFF, 0x3D8BFF, 0x3DDC84, 0xFF4D5E};

/* Where a hand floats, from the body's centre, and its size (0 = tucked
 * away). */
typedef struct {
    float x, y, s;
} hand_pose_t;


#define TUCK_L {-112, HAND_Y, 0}
#define TUCK_R {112, HAND_Y, 0}
/* Holding a finger: the hands a little apart round it, never further out
 * than this from the body (Rive units, like the clips' hands). */
#define CLUTCH_GAP 26
#define CLUTCH_REACH 200

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
    int32_t countdown;   // degrees of the follow-up's ring left, 0 without it
    int32_t charge;      // degrees of the charge ring, 0 without it
    int32_t charge_opa;
    int32_t spark;       // where its spark is, degrees from the top; -1 resting
} body_vis_t;

typedef struct {
    float x, y, w, h;      // centre and size in pixels
    float top_l, top_r;    // where the top lid cuts, from the centre
    float bottom;
    float spiral;          // > 0.5: a spiral, turned by `phase` (radians)
    float phase;
    float swell;           // drawn this much bigger (the voice): scaled, not rendered again
} eye_box_t;

/* An eye's picture, redrawn only when its shape changes. */
typedef struct {
    look_sprite_t sp;
    int32_t key[6];
    lv_area_t drawn;
    bool valid;
} eye_sprite_t;

/* The clips' effects. Sparkles: screen pixels from the middle, per second. */
#define FX_PARTS 24
#define FX_RATE 36.0f      // sparkles a second at spark 1
#define FX_PROP_HALF 34.0f // the prop card's half size, Rive units
#define FX_DEFAULT_RGB 0xFFD45C

typedef struct {
    float x, y, vx, vy;
    int16_t age, life; // ms
    bool white;
} fx_part_t;

static struct {
    lv_obj_t *obj;
    lv_timer_t *timer;
    bool stopped; // eyes_set_pace() hid it
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
    /* The voice's level, eased, and where it is going (set by the audio
     * tasks without the LVGL lock). */
    float level;
    volatile float tlevel;
    uint32_t t_ms;        // animation clock
    uint32_t last_tick;   // when the last frame ran
    int32_t dt_ms;        // how long since, for this frame's step
    float frames;         // the same in 33 ms frames, for the easing
    float near;
    float size;           // 1, MENU_SIZE with the menu open, STATUS_SIZE tinted
    eyes_status_t status;
    /* The follow-up's ring: how long it runs, and since when (t_ms). */
    uint32_t cd_ms, cd_from_ms;
    float tint;
    float tint_rgb[3];
    /* A finger held (eyes_clutch()), on screen. */
    bool clutch;
    int32_t clutch_x, clutch_y;
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
    /* The battery: what shows and the charge; the ring's fade (0..1) and how
     * far it has filled, in degrees; the bolt, how far in, and where it was
     * drawn; the low battery's look last drawn (-1 hidden) and where. */
    eyes_battery_t bat;
    int bat_pct;
    float charge_vis, charge_deg;
    bool zapping, zap_shown;
    int32_t zap_ms;
    lv_area_t zap_drawn;
    int32_t lowbat_key;
    lv_area_t lowbat_drawn;
    /* Cross-eyed, 0..1, eased, and where it is going. */
    float cross, tcross;
    /* The firefly (eyes_firefly()): shown, where (screen pixels from the
     * middle), and where it was drawn. */
    bool fly, fly_shown;
    float fly_x, fly_y;
    lv_area_t fly_drawn;
    /* A clip from the Rive file playing (NULL: none), how far in, and this
     * frame's pose, blended from its two nearest frames. */
    const clip_t *clip;
    int32_t clip_ms;
    bool clip_mirror; // played the other way round, left for right
    clip_frame_t cf;
    /* The clip's effects: their colour, the prop's glyph (NULL: a glowing
     * orb), the sparkles flying (they outlive the clip), how many are due,
     * and the box they were last drawn in. */
    uint32_t fx_rgb;
    const char *fx_glyph;
    fx_part_t fx[FX_PARTS];
    float fx_due;
    bool fx_shown;
    lv_area_t fx_drawn;
    /* A bridge: what was on screen when a clip started, stopped or gave way
     * to another, eased into what comes next so nothing jumps. */
    int32_t br_ms, br_len; // how far in and how long; br_ms >= br_len: none
    body_vis_t br_body;
    eye_box_t br_eye[2];
    hand_pose_t br_hand[2];
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

/* ---- Clips ---------------------------------------------------------------- */

/* A clip is keyed at 30 frames a second; between two of them the pose is
 * blended, so it plays at the same speed whatever the frame rate. It starts
 * and ends in the rest pose, so the engine takes over unseen. */
#define CLIP_FRAME_MS (1000.0f / 30.0f)
/* A jump this big between two frames is a cut, not a move (Rive units). */
#define CLIP_CUT 300.0f
/* Bridges between what was showing and what comes next: into a clip (from
 * a mood's face, a lean, a held finger), out of one (into the mood it leaves)
 * and from one clip cut short into the next. Short enough to keep a clip's
 * anticipation, long enough that nothing pops. */
#define BRIDGE_IN_MS 160
#define BRIDGE_OUT_MS 260
#define BRIDGE_CUT_MS 200

/* Start easing from what is on screen now into whatever comes next. */
static void bridge_from_screen(int32_t ms)
{
    if (!s.body_valid) {
        return; // nothing drawn yet
    }
    s.br_body = s.body;
    memcpy(s.br_eye, s.eye, sizeof(s.br_eye));
    memcpy(s.br_hand, s.hand, sizeof(s.br_hand));
    s.br_ms = 0;
    s.br_len = ms;
}

static bool bridging(void)
{
    return s.br_ms < s.br_len;
}

/* The same frame the other way round: left for right. */
static void clip_mirror(clip_frame_t *f)
{
    const clip_frame_t m = *f;
    f->bx = -m.bx;
    f->rot = -m.rot;
    f->fx = -m.fx;
    f->lx = -m.rx, f->ly = m.ry, f->ls = m.rs;
    f->rx = -m.lx, f->ry = m.ly, f->rs = m.ls;
    f->lex = -m.rex, f->lew = m.rew, f->leh = m.reh, f->ltl = m.rtr, f->ltr = m.rtl, f->lbot = m.rbot;
    f->rex = -m.lex, f->rew = m.lew, f->reh = m.leh, f->rtl = m.ltr, f->rtr = m.ltl, f->rbot = m.lbot;
    f->px = -m.px;
}

/* A clip that ends away from where it started (it pounced on something)
 * leaves Yoru there: its last offset becomes where it is. */
static void clip_end(void)
{
    clip_frame_t last;
    clips_frame(s.clip, s.clip->n - 1, &last);
    if (s.clip_mirror) {
        clip_mirror(&last);
    }
    s.mv.pos.x += last.bx * K * s.size;
    s.mv.pos.y += last.by * K * s.size;
    if (s.mv.follow) {
        s.mv.to = s.mv.pos;
    }
    s.clip = NULL;
}

static bool clip_step(void)
{
    if (s.clip == NULL) {
        return false;
    }
    s.clip_ms += s.dt_ms;
    float at = (float) s.clip_ms / CLIP_FRAME_MS;
    if (at >= (float) (s.clip->n - 1)) {
        if (!s.clip->loop) {
            bridge_from_screen(BRIDGE_OUT_MS);
            clip_end();
            return false;
        }
        // A loop's last frame is its first again.
        s.clip_ms %= (int32_t) ((float) (s.clip->n - 1) * CLIP_FRAME_MS);
        at = (float) s.clip_ms / CLIP_FRAME_MS;
    }
    const int i = LV_MIN((int) at, s.clip->n - 2);
    clip_frame_t fa, fb;
    clips_frame(s.clip, i, &fa);
    clips_frame(s.clip, i + 1, &fb);
    const float *a = (const float *) &fa, *b = (const float *) &fb;
    // A cut (off one edge, back in at the other) holds instead of sweeping across.
    const float k = fabsf(fb.bx - fa.bx) > CLIP_CUT ? 0 : at - (float) i;
    float *o = (float *) &s.cf;
    for (size_t j = 0; j < sizeof(clip_frame_t) / sizeof(float); j++) {
        o[j] = lerp(a[j], b[j], k);
    }
    if (s.clip_mirror) {
        clip_mirror(&s.cf);
    }
    return true;
}

/* ---- Hands ---------------------------------------------------------------- */

static hand_pose_t pose_lerp(hand_pose_t a, hand_pose_t b, float k)
{
    return (hand_pose_t) {lerp(a.x, b.x, k), lerp(a.y, b.y, k), lerp(a.s, b.s, k)};
}

/* Both hands round the finger, wherever the body is, squeezing it now and
 * then. They ease there, so they reach out rather than jump. */
static void clutch_step(void)
{
    const float d = fmaxf(s.body.dist, 0.05f);
    float x = (float) (s.clutch_x - s.body.bx) / (K * d), y = (float) (s.clutch_y - s.body.by) / (K * d);
    const float len = hypotf(x, y);
    if (len > CLUTCH_REACH) {
        x *= CLUTCH_REACH / len;
        y *= CLUTCH_REACH / len;
    }
    const float squeeze = 1.1f + 0.06f * sinf((float) s.t_ms * 0.005f);
    const hand_pose_t l = {x - CLUTCH_GAP, y + 4, squeeze}, r = {x + CLUTCH_GAP, y + 4, squeeze};
    s.hand[0] = pose_lerp(s.hand[0], l, ease(0.3f));
    s.hand[1] = pose_lerp(s.hand[1], r, ease(0.3f));
    s.hands_front = true;
}

/* Where the hands are: round a held finger, where the clip puts them, or
 * tucked away behind the body. */
static void hands_step(void)
{
    if (s.clutch) {
        clutch_step();
        return;
    }
    if (s.clip != NULL) {
        s.hand[0] = (hand_pose_t) {s.cf.lx, s.cf.ly, s.cf.ls};
        s.hand[1] = (hand_pose_t) {s.cf.rx, s.cf.ry, s.cf.rs};
        s.hands_front = s.cf.front > 0.5f;
        return;
    }
    s.hand[0] = (hand_pose_t) TUCK_L;
    s.hand[1] = (hand_pose_t) TUCK_R;
    s.hands_front = false;
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
    if (s.clip != NULL) {
        v->dist *= s.cf.near; // the clip comes nearer or goes back
    }
    const float near1 = fminf(1, v->dist);
    // Breathing: a slow bob of a few pixels. Not asleep (far away and still),
    // not with the tint on (the halo covers the screen), not under the menu.
    // A clip moves the body itself.
    const bool still = s.near < 0.5f || s.tint > 0.01f || s.size < 0.999f || s.clip != NULL;
    const float bob = still ? 0 : 3 * sinf(t * 2.1f) * near1;
    // Scared, it trembles; knocked, it is pushed and springs back; leaning,
    // it sags towards the low side.
    const float shiver = s.mood == EYES_MOOD_SCARED && s.near > 0.5f ? 2.2f * sinf(t * 70) : 0;
    v->bx = CX + (int32_t) lroundf(s.mv.pos.x / zf + (s.kx + shiver + s.lx * 8 * s.near) * near1);
    v->by = CY + (int32_t) lroundf(s.mv.pos.y / zf + bob + (s.ky + s.ly * 6 * s.near) * near1);
    if (s.clip != NULL) {
        // The clip's own squash and stretch, and its offset in the Rive
        // file's units.
        v->bx += (int32_t) lroundf(s.cf.bx * K * v->dist);
        v->by += (int32_t) lroundf(s.cf.by * K * v->dist);
        v->rx = ball_radius(R * s.cf.bsx * v->dist);
        v->ry = ball_radius(R * s.cf.bsy * v->dist);
    } else {
        const float sq = clampf(s.squash + s.vsq, -1, 1);
        v->rx = ball_radius(R * (1 + LOOK_SQUASH * sq) * v->dist);
        v->ry = ball_radius(R * (1 - LOOK_SQUASH * sq) * v->dist);
    }
    v->sx = (float) v->rx / R;
    v->sy = (float) v->ry / R;
    v->near = s.near;
    v->fog = FOG_FLOOR + (1 - FOG_FLOOR) / (1 + FOG * fmaxf(0, zf - 1));
    v->tint = s.tint;
    memcpy(v->tint_rgb, s.tint_rgb, sizeof(v->tint_rgb));
    const uint32_t gone = s.t_ms - s.cd_from_ms;
    if (s.cd_ms > 0 && gone < s.cd_ms) {
        const float left = 360.0f * (1 - (float) gone / (float) s.cd_ms);
        v->countdown = COUNTDOWN_STEP * (int32_t) ceilf(left / COUNTDOWN_STEP);
    }
    // The follow-up's ring goes over the charge's.
    if (v->countdown == 0 && s.charge_vis > 0.01f) {
        v->charge = LV_MAX(COUNTDOWN_STEP, COUNTDOWN_STEP * (int32_t) lroundf(s.charge_deg / COUNTDOWN_STEP));
        v->charge_opa = 16 * (int32_t) lroundf(s.charge_vis * 15 * v->fog);
        const float p = (float) (s.t_ms % CHARGE_SPARK_MS) / (CHARGE_SPARK_MS * 0.7f);
        v->spark = p < 1 ? CHARGE_SPARK_STEP * (int32_t) (p * (float) v->charge / CHARGE_SPARK_STEP) : -1;
    }
}

/* A clip draws the eyes as the Rive file does: the neutral pills, each
 * scaled and lidded on its own, on a face as far off and as narrow as the
 * clip says, turned with the body's tilt. */
static void clip_eye_layout(void)
{
    const eye_shape_t *n = &s_moods[EYES_MOOD_NEUTRAL];
    const float a = s.cf.rot * (float) M_PI / 180.0f;
    const float ca = cosf(a), sa = sinf(a);
    for (int i = 0; i < 2; i++) {
        // ex, ew, eh, top lid left and right, bottom lid
        const float *p = i == 0 ? &s.cf.lex : &s.cf.rex;
        eye_box_t *e = &s.eye[i];
        e->w = n->w * K * s.cf.turn * p[1] * s.body.sx;
        e->h = n->h * K * p[2] * s.body.sy;
        const float dx = (p[0] * s.cf.turn + s.cf.fx) * K * s.body.sx;
        const float dy = (FACE_Y + s.cf.fy * K) * s.body.sy;
        e->x = (float) s.body.bx + dx * ca - dy * sa;
        e->y = (float) s.body.by + dx * sa + dy * ca;
        const float hh = e->h < 2 ? 2 : e->h;
        e->top_l = -hh / 2 + p[3] * hh;
        e->top_r = -hh / 2 + p[4] * hh;
        e->bottom = p[5];
        e->spiral = 0;
        e->phase = 0;
        e->swell = 1;
    }
}

static void eye_layout(void)
{
    if (s.clip != NULL) {
        clip_eye_layout();
        return;
    }
    // Near the edge of the ball the eyes get narrower: it seems to turn.
    const float nx = 1 - 0.3f * s.gx * s.gx;
    const float ny = 1 - 0.25f * s.gy * s.gy;
    // The voice swells the drawn sprite, not the shape: rendering the eyes
    // again every frame while Yoru talks cost a seventh of a core (0.20.11).
    const float swell = 1 + 0.14f * s.level;
    const float w = s.cur.w * K * nx * s.body.sx;
    const float h = s.cur.h * K * ny * s.body.sy * s.lid;
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
        e->swell = swell;
        e->x = (float) s.body.bx + ((float) side * (EYE_SPACING * nx - 0.45f * GAZE_X * s.cross) + ox) * s.body.sx;
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

/* The follow-up's ring, from the top round clockwise, shorter as the time
 * to answer runs out. */
static void draw_countdown(lv_layer_t *layer, const body_vis_t *v)
{
    if (v->countdown <= 0) {
        return;
    }
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = lv_color_hex(COUNTDOWN_RGB);
    dsc.opa = (lv_opa_t) (200 * v->fog);
    dsc.width = COUNTDOWN_W;
    dsc.rounded = 1;
    dsc.center.x = v->bx;
    dsc.center.y = v->by;
    dsc.radius = (uint16_t) (LV_MAX(v->rx, v->ry) + COUNTDOWN_GAP);
    // A whole turn would start and end at the same angle: nothing drawn.
    const int32_t deg = LV_MIN(v->countdown, 359);
    dsc.start_angle = 270;
    dsc.end_angle = (270 + deg) % 360;
    lv_draw_arc(layer, &dsc);
}

static lv_area_t box(float x, float y, float l, float t, float r, float b);

/* The charge ring: a faint track all round, the charge from the top round
 * clockwise, and the spark running along it into Yoru. */
static void draw_charge(lv_layer_t *layer, const body_vis_t *v)
{
    if (v->charge <= 0 || v->charge_opa <= 0) {
        return;
    }
    const lv_color_t green = lv_color_hex(CHARGE_RGB);
    const int32_t radius = LV_MAX(v->rx, v->ry) + COUNTDOWN_GAP;
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = green;
    dsc.width = CHARGE_W;
    dsc.rounded = 1;
    dsc.center.x = v->bx;
    dsc.center.y = v->by;
    dsc.radius = (uint16_t) radius;
    dsc.opa = (lv_opa_t) (v->charge_opa / 5);
    dsc.start_angle = 0;
    dsc.end_angle = 360;
    lv_draw_arc(layer, &dsc);
    dsc.opa = (lv_opa_t) (v->charge_opa * 3 / 4);
    dsc.start_angle = 270;
    dsc.end_angle = (270 + LV_MIN(v->charge, 359)) % 360;
    lv_draw_arc(layer, &dsc);
    if (v->spark >= 0) {
        const float a = (float) (v->spark - 90) * (float) M_PI / 180.0f;
        const float x = (float) v->bx + (float) radius * cosf(a), y = (float) v->by + (float) radius * sinf(a);
        draw_disc(layer, x, y, 6, green, (lv_opa_t) (v->charge_opa / 3));
        draw_disc(layer, x, y, 3, lv_color_hex(0xE8FFF0), (lv_opa_t) v->charge_opa);
    }
}

/* The bolt's x at height y: a zigzag up from the bottom edge, round the
 * body's x. */
static float zap_x(float y)
{
    const float u = ((float) BOARD_LCD_V_RES - y) / ZAP_STEP;
    const int k = (int) floorf(u);
    const float side = (k & 1) ? ZAP_ZIG : -ZAP_ZIG;
    return (float) s.body.bx + lerp(side, -side, u - (float) k);
}

/* Where the bolt's head is: from below the edge into the body, faster as
 * it goes. */
static float zap_head(void)
{
    const float k = clampf((float) s.zap_ms / ZAP_MS, 0, 1);
    return lerp((float) BOARD_LCD_V_RES + 10, (float) (s.body.by + s.body.ry / 2), k * k);
}

static void zap_line(lv_layer_t *layer, float y1, float y2, int32_t w, lv_opa_t opa)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(ZAP_RGB);
    dsc.width = w;
    dsc.opa = opa;
    dsc.round_start = 1;
    dsc.round_end = 1;
    dsc.p1.x = (lv_value_precise_t) zap_x(y1);
    dsc.p1.y = (lv_value_precise_t) y1;
    dsc.p2.x = (lv_value_precise_t) zap_x(y2);
    dsc.p2.y = (lv_value_precise_t) y2;
    lv_draw_line(layer, &dsc);
}

/* Behind the body, so it disappears into Yoru: a glow and a bright core,
 * one straight piece per zig. */
static void draw_zap(lv_layer_t *layer)
{
    if (!s.zapping) {
        return;
    }
    const float head = zap_head();
    const float tail = fminf(head + ZAP_TAIL, (float) BOARD_LCD_V_RES);
    for (int pass = 0; pass < 2; pass++) {
        float y = head;
        while (y < tail) {
            // The next turn below y, or the tail.
            const float u = ((float) BOARD_LCD_V_RES - y) / ZAP_STEP;
            float next = (float) BOARD_LCD_V_RES - (ceilf(u) - 1) * ZAP_STEP;
            if (next <= y + 0.5f) {
                next += ZAP_STEP;
            }
            next = fminf(next, tail);
            zap_line(layer, y, next, pass == 0 ? 12 : 4, pass == 0 ? 70 : LV_OPA_COVER);
            y = next;
        }
    }
}

static lv_area_t zap_area(void)
{
    const int32_t m = ZAP_ZIG + 10;
    return (lv_area_t) {s.body.bx - m, s.body.by, s.body.bx + m, BOARD_LCD_V_RES - 1};
}

static void lowbat_pos(float *x, float *y)
{
    const float d = fmaxf(0.25f, s.body.dist);
    *x = (float) s.body.bx + LOWBAT_DX * d;
    *y = (float) s.body.by + LOWBAT_DY * d;
}

/* How it looks this frame, as a number that changes only when it should be
 * redrawn: its opacity in 16 steps and the charge. -1: not shown. Empty, it
 * blinks; asleep, it goes with the body into the dark. */
static int32_t lowbat_key(void)
{
    if (s.bat != EYES_BATTERY_LOW && s.bat != EYES_BATTERY_EMPTY) {
        return -1;
    }
    float o = s.near * s.body.fog;
    if (s.bat == EYES_BATTERY_EMPTY && (s.t_ms / LOWBAT_BLINK_MS) % 2) {
        o *= 0.25f;
    }
    const int32_t step = (int32_t) lroundf(o * 15);
    return step <= 0 ? -1 : step * 1000 + LV_CLAMP(0, s.bat_pct, 100) * 2 + (s.bat == EYES_BATTERY_EMPTY);
}

/* A little battery: an outline, a nub and what's left in it. */
static void draw_lowbat(lv_layer_t *layer)
{
    const int32_t key = lowbat_key();
    if (key < 0) {
        return;
    }
    const lv_opa_t opa = (lv_opa_t) LV_MIN(255, (key / 1000) * 17);
    const lv_color_t c = lv_color_hex(s.bat == EYES_BATTERY_EMPTY ? LOWBAT_EMPTY_RGB : LOWBAT_RGB);
    float x, y;
    lowbat_pos(&x, &y);
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_TRANSP;
    dsc.border_color = c;
    dsc.border_opa = opa;
    dsc.border_width = 2;
    dsc.radius = 4;
    const lv_area_t a = {(int32_t) x - 14, (int32_t) y - 8, (int32_t) x + 11, (int32_t) y + 8};
    lv_draw_rect(layer, &dsc, &a);
    fill_rect(layer, x + 12, y - 3, x + 14, y + 3, c, 1, opa);
    const float inner = 21;
    const float full = fmaxf(3, inner * (float) LV_CLAMP(0, s.bat_pct, 100) / 100);
    fill_rect(layer, x - 11, y - 5, x - 11 + full, y + 5, c, 1, opa);
}

static lv_area_t lowbat_area(void)
{
    float x, y;
    lowbat_pos(&x, &y);
    return box(x, y, 17, 11, 17, 11);
}

/* A small heart: two round tops and a point below, centred on (x, y). */
static void draw_heart(lv_layer_t *layer, float x, float y, lv_color_t color, lv_opa_t opa)
{
    fill_rect(layer, x - 13, y - 7, x + 1, y + 7, color, LV_RADIUS_CIRCLE, opa);
    fill_rect(layer, x - 1, y - 7, x + 13, y + 7, color, LV_RADIUS_CIRCLE, opa);
    fill_triangle(layer, x - 13, y + 1, x + 13, y + 1, x, y + 16, color, opa);
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

#define FLY_RGB 0xD8FF6A
#define FLY_GLOW 14

static lv_area_t fly_area(void)
{
    return box(CX + s.fly_x, CY + s.fly_y, FLY_GLOW + 2, FLY_GLOW + 2, FLY_GLOW + 2, FLY_GLOW + 2);
}

/* A firefly: a soft glow with a bright heart, flickering. */
static void draw_firefly(lv_layer_t *layer)
{
    if (!s.fly) {
        return;
    }
    const float t = (float) s.t_ms / 1000.0f;
    const float flick = 0.65f + 0.35f * sinf(t * 11) * sinf(t * 3.7f);
    const lv_color_t c = lv_color_hex(FLY_RGB);
    const float x = CX + s.fly_x, y = CY + s.fly_y;
    draw_disc(layer, x, y, FLY_GLOW * (0.8f + 0.2f * flick), c, (lv_opa_t) (60 * flick));
    draw_disc(layer, x, y, 7, c, (lv_opa_t) (110 * flick));
    draw_disc(layer, x, y, 3.5f, lv_color_hex(0xFFFFE0), (lv_opa_t) (255 * flick));
}

/* ---- The clips' effects --------------------------------------------------- */

static bool fx_on(void)
{
    return s.clip != NULL && (s.cf.glow > 0.01f || s.cf.ps > 0.02f);
}

static void fx_prop_at(float *x, float *y, float *half)
{
    const float d = s.body.dist;
    *x = (float) s.body.bx + s.cf.px * K * d;
    *y = (float) s.body.by + s.cf.py * K * d;
    *half = FX_PROP_HALF * K * d * s.cf.ps;
}

/* Sparkles fly off the prop while it shows, off the top of the body if not,
 * as fast as the clip's spark says; the old ones fall and fade. */
static void fx_step(void)
{
    const float dt = (float) s.dt_ms / 1000.0f;
    for (int i = 0; i < FX_PARTS; i++) {
        fx_part_t *p = &s.fx[i];
        if (p->age >= p->life) {
            continue;
        }
        p->age = (int16_t) LV_MIN(p->age + s.dt_ms, INT16_MAX);
        p->vy += 120 * dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
    }
    if (s.clip == NULL || s.cf.spark <= 0.01f) {
        s.fx_due = 0;
        return;
    }
    s.fx_due += s.cf.spark * FX_RATE * dt;
    while (s.fx_due >= 1) {
        s.fx_due -= 1;
        int slot = -1;
        for (int i = 0; i < FX_PARTS && slot < 0; i++) {
            if (s.fx[i].age >= s.fx[i].life) {
                slot = i;
            }
        }
        if (slot < 0) {
            return;
        }
        float x, y, half;
        fx_prop_at(&x, &y, &half);
        const float a = frand() * 2 * (float) M_PI;
        if (s.cf.ps <= 0.02f) {
            // Off the upper half of the sphere.
            const float up = -(float) M_PI * (0.1f + 0.8f * frand());
            x = (float) s.body.bx + cosf(up) * (float) s.body.rx;
            y = (float) s.body.by + sinf(up) * (float) s.body.ry;
            half = 0;
        }
        const float v = 60 + 90 * frand();
        s.fx[slot] = (fx_part_t) {x - CX + cosf(a) * half, y - CY + sinf(a) * half, cosf(a) * v, sinf(a) * v - 70, 0,
                                  (int16_t) (450 + 450 * frand()), frand() < 0.35f};
    }
}

/* The glow behind the body: the halo in the effects' colour. */
static void draw_fx_glow(lv_layer_t *layer, const body_vis_t *v)
{
    if (s.clip == NULL || s.cf.glow <= 0.01f) {
        return;
    }
    const float d = fminf(1, v->dist);
    look_draw(layer, &look_get()->halo, v->bx, v->by, d, d, 0, (lv_opa_t) (150 * s.cf.glow), lv_color_hex(s.fx_rgb));
}

/* The same colour washed over the sphere, lighter than a tint. */
static void draw_fx_wash(lv_layer_t *layer, const body_vis_t *v)
{
    if (s.clip == NULL || s.cf.glow <= 0.01f) {
        return;
    }
    look_draw(layer, &look_get()->tint, v->bx, v->by, v->sx, v->sy, 0, (lv_opa_t) (90 * s.cf.glow),
              lv_color_hex(s.fx_rgb));
}

/* The prop: a card in the colour with its glyph, or a glowing orb. */
static void draw_fx_prop(lv_layer_t *layer)
{
    if (s.clip == NULL || s.cf.ps <= 0.02f) {
        return;
    }
    float x, y, h;
    fx_prop_at(&x, &y, &h);
    const lv_color_t c = lv_color_hex(s.fx_rgb);
    const lv_opa_t opa = (lv_opa_t) (255 * s.body.fog);
    if (s.fx_glyph == NULL) {
        draw_disc(layer, x, y, h * 1.5f, c, (lv_opa_t) (opa / 4));
        draw_disc(layer, x, y, h, c, opa);
        draw_disc(layer, x - h * 0.2f, y - h * 0.2f, h * 0.45f, lv_color_mix(lv_color_white(), c, 170), opa);
        return;
    }
    draw_disc(layer, x, y, h * 1.45f, c, (lv_opa_t) (opa / 5));
    fill_rect(layer, x - h, y - h, x + h, y + h, c, (int32_t) (h * 0.35f), opa);
    fill_rect(layer, x - h * 0.8f, y - h * 0.85f, x + h * 0.8f, y - h * 0.55f, lv_color_mix(lv_color_white(), c, 90),
              (int32_t) (h * 0.15f), (lv_opa_t) (opa / 3));
    if (h > 10) {
        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        dsc.font = h > 22 ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
        dsc.color = lv_color_white();
        dsc.opa = opa;
        dsc.align = LV_TEXT_ALIGN_CENTER;
        dsc.text = s.fx_glyph;
        const int32_t lh = lv_font_get_line_height(dsc.font);
        const lv_area_t a = {(int32_t) (x - h), (int32_t) y - lh / 2, (int32_t) (x + h), (int32_t) y + lh / 2};
        lv_draw_label(layer, &dsc, &a);
    }
}

static void draw_fx_sparks(lv_layer_t *layer)
{
    for (int i = 0; i < FX_PARTS; i++) {
        const fx_part_t *p = &s.fx[i];
        if (p->age >= p->life) {
            continue;
        }
        const float k = (float) p->age / p->life;
        const lv_color_t c = p->white ? lv_color_white() : lv_color_hex(s.fx_rgb);
        const float r = 4.5f * (1 - k * 0.6f);
        const lv_opa_t opa = (lv_opa_t) (255 * (1 - k));
        fill_triangle(layer, CX + p->x - r, CY + p->y, CX + p->x + r, CY + p->y, CX + p->x, CY + p->y - 2.2f * r, c, opa);
        fill_triangle(layer, CX + p->x - r, CY + p->y, CX + p->x + r, CY + p->y, CX + p->x, CY + p->y + 2.2f * r, c, opa);
        fill_triangle(layer, CX + p->x, CY + p->y - r, CX + p->x, CY + p->y + r, CX + p->x - 2.2f * r, CY + p->y, c, opa);
        fill_triangle(layer, CX + p->x, CY + p->y - r, CX + p->x, CY + p->y + r, CX + p->x + 2.2f * r, CY + p->y, c, opa);
    }
}

static lv_area_t area_union(lv_area_t a, lv_area_t b)
{
    return (lv_area_t) {LV_MIN(a.x1, b.x1), LV_MIN(a.y1, b.y1), LV_MAX(a.x2, b.x2), LV_MAX(a.y2, b.y2)};
}

/* One box round all of it: the glow, the prop and every sparkle. */
static bool fx_area(lv_area_t *out)
{
    bool any = false;
    lv_area_t a = {0};
    const body_vis_t *v = &s.body;
    if (fx_on()) {
        const float d = fminf(1, v->dist);
        a = look_area(&look_get()->halo, v->bx, v->by, d, d, false);
        if (s.cf.ps > 0.02f) {
            float x, y, h;
            fx_prop_at(&x, &y, &h);
            const lv_area_t p = box(x, y, h * 1.5f + 3, h * 1.5f + 3, h * 1.5f + 3, h * 1.5f + 3);
            a = area_union(a, p);
        }
        any = true;
    }
    for (int i = 0; i < FX_PARTS; i++) {
        const fx_part_t *p = &s.fx[i];
        if (p->age >= p->life) {
            continue;
        }
        const lv_area_t b = box(CX + p->x, CY + p->y, 12, 12, 12, 12);
        if (any) {
            a = area_union(a, b);
        } else {
            a = b;
            any = true;
        }
    }
    *out = a;
    return any;
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const look_t *lk = look_get();
    const body_vis_t *v = &s.body;
    const lv_color_t black = lv_color_black();
    s_prof.draws++;
    s_prof.px += (uint64_t) lv_area_get_size(&layer->_clip_area);

    if (v->tint > 0.01f) {
        const float d = fminf(1, v->dist);
        PROF(PROF_HALO, look_draw(layer, &lk->halo, v->bx, v->by, d, d, 0, (lv_opa_t) (90 * v->tint),
                                  rgb_color(v->tint_rgb)));
    }
    PROF(PROF_GLOW, draw_fx_glow(layer, v));
    PROF(PROF_RINGS, {
        draw_countdown(layer, v);
        draw_charge(layer, v);
        draw_zap(layer);
    });
    if (!s.hands_front) {
        PROF(PROF_HANDS, draw_hands(layer));
    }
    const lv_opa_t body_opa = (lv_opa_t) (255 * v->fog);
    if (s.ball == NULL) {
        PROF(PROF_BALL, s.ball = look_ball(v->rx, v->ry, s.frame)); // shown again before the next frame
    }
    PROF(PROF_BODY, {
        if (s.ball != NULL) {
            look_draw(layer, s.ball, v->bx, v->by, 1, 1, 0, body_opa, black);
        } else {
            look_draw(layer, &lk->day, v->bx, v->by, v->sx, v->sy, 0, body_opa, black);
        }
    });
    if (v->tint > 0.01f) {
        PROF(PROF_TINT, look_draw(layer, &lk->tint, v->bx, v->by, v->sx, v->sy, 0, (lv_opa_t) (217 * v->tint),
                                  rgb_color(v->tint_rgb)));
    }
    PROF(PROF_BADGE, {
        draw_fx_wash(layer, v);
        draw_badge(layer, v);
    });
    PROF(PROF_EYES, {
        for (int i = 0; i < 2; i++) {
            look_draw(layer, &s.eye_sp[i].sp, (int32_t) lroundf(s.eye[i].x), (int32_t) lroundf(s.eye[i].y),
                      s.eye[i].swell, s.eye[i].swell, 0, LV_OPA_COVER, black);
        }
    });
    PROF(PROF_PROP, draw_fx_prop(layer));
    if (s.hands_front) {
        PROF(PROF_HANDS, draw_hands(layer));
    }
    PROF(PROF_OVERLAYS, draw_overlays(layer));
    PROF(PROF_SPARKS, draw_fx_sparks(layer));
    PROF(PROF_REST, {
        draw_lowbat(layer);
        draw_firefly(layer);
    });
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
    if (v->countdown > 0 || v->charge > 0) {
        // The charge's spark is a little wider than the follow-up's ring.
        const int32_t r = LV_MAX(v->rx, v->ry) + COUNTDOWN_GAP + LV_MAX(COUNTDOWN_W, 12) + 2;
        a = (lv_area_t) {LV_MIN(a.x1, v->bx - r), LV_MIN(a.y1, v->by - r), LV_MAX(a.x2, v->bx + r),
                         LV_MAX(a.y2, v->by + r)};
    }
    return a;
}

static lv_area_t eye_area(int i)
{
    const look_sprite_t *sp = &s.eye_sp[i].sp;
    return look_area(sp, (int32_t) lroundf(s.eye[i].x), (int32_t) lroundf(s.eye[i].y), s.eye[i].swell, s.eye[i].swell,
                     false);
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
/* The charge ring fades in and fills; the bolt runs up, and when it reaches
 * Yoru the jolt: stretched tall, pushed up, eyes wide. */
static void battery_step(void)
{
    const float fade = (float) s.dt_ms / CHARGE_FADE_MS;
    s.charge_vis = s.bat == EYES_BATTERY_CHARGING ? fminf(1, s.charge_vis + fade) : fmaxf(0, s.charge_vis - fade);
    if (s.charge_vis <= 0) {
        s.charge_deg = 0; // it fills up again next time
    }
    const float want = 360.0f * (float) LV_CLAMP(0, s.bat_pct, 100) / 100;
    const float fill = 360.0f * (float) s.dt_ms / CHARGE_FILL_MS;
    s.charge_deg = want > s.charge_deg ? fminf(want, s.charge_deg + fill) : want;
    if (!s.zapping) {
        return;
    }
    s.zap_ms += s.dt_ms;
    if (s.zap_ms >= ZAP_MS) {
        s.zapping = false;
        s.squash = -0.8f;
        s.kvy -= R * 5;
        s.level = 1; // eased back down like the voice's
    }
}

static bool step(void)
{
    const bool clipping = clip_step();
    fx_step();
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

    // Blinks every 2.2-5.2 s, one in five doubled; none while asleep, and
    // none during a clip, which blinks when it means to.
    if (s.mood != EYES_MOOD_ASLEEP && !clipping) {
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
    const float cross = lerp(s.cross, s.tcross, ease(0.2f));
    const bool crossed = fabsf(cross - s.cross) > 0.002f;
    s.cross = fabsf(cross - s.tcross) < 0.002f ? s.tcross : cross;

    float squash = s.squash * powf(0.85f, s.frames);
    // Quick to rise with a syllable, slower to fall: it reads as talking.
    float level = lerp(s.level, s.tlevel, ease(s.tlevel > s.level ? 0.5f : 0.2f));
    if (level < 0.005f) {
        level = 0;
    }

    bool moved = fabsf(gx - s.gx) > 0.002f || fabsf(gy - s.gy) > 0.002f || fabsf(lid - s.lid) > 0.002f
                 || fabsf(squash) > 0.002f || fabsf(level - s.level) > 0.002f || memcmp(&before, &s.cur, sizeof(before)) != 0
                 || crossed || clipping;
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

    battery_step();
    hands_step();
    return moved;
}

/* Ease this frame's layout from the snapshot the bridge started with; true
 * while it does. The sphere's radius snaps like any other size. */
static bool bridge_step(void)
{
    if (!bridging()) {
        return false;
    }
    s.br_ms += s.dt_ms;
    const float k = smooth(fminf(1, (float) s.br_ms / (float) s.br_len));
    body_vis_t *b = &s.body;
    const body_vis_t *f = &s.br_body;
    b->bx = (int32_t) lroundf(lerp((float) f->bx, (float) b->bx, k));
    b->by = (int32_t) lroundf(lerp((float) f->by, (float) b->by, k));
    b->rx = ball_radius(lerp((float) f->rx, (float) b->rx, k));
    b->ry = ball_radius(lerp((float) f->ry, (float) b->ry, k));
    b->sx = (float) b->rx / R;
    b->sy = (float) b->ry / R;
    for (int i = 0; i < 2; i++) {
        eye_box_t *e = &s.eye[i];
        const eye_box_t *g = &s.br_eye[i];
        e->x = lerp(g->x, e->x, k);
        e->y = lerp(g->y, e->y, k);
        e->w = lerp(g->w, e->w, k);
        e->h = lerp(g->h, e->h, k);
        e->top_l = lerp(g->top_l, e->top_l, k);
        e->top_r = lerp(g->top_r, e->top_r, k);
        e->bottom = lerp(g->bottom, e->bottom, k);
        // A spiral can't be half a pill: it turns into one halfway.
        if (k < 0.5f) {
            e->spiral = g->spiral;
            e->phase = g->phase;
        }
        s.hand[i] = pose_lerp(s.br_hand[i], s.hand[i], k);
    }
    return true;
}

static void frame_cb(lv_timer_t *timer)
{
    const uint32_t elapsed = lv_tick_elaps(s.last_tick);
    s.last_tick = lv_tick_get();
    s.dt_ms = (int32_t) LV_CLAMP(1, elapsed, EYES_MAX_STEP_MS);
    s.frames = (float) s.dt_ms / EYES_FRAME_MS;
    s.t_ms += (uint32_t) s.dt_ms;
    s_prof.frames++;
    const int64_t logic_t0 = esp_timer_get_time();
    const bool eyes_moved = step();

    const body_vis_t was = s.body;
    body_layout(&s.body);
    eye_layout();
    const bool bridged = bridge_step();
    s_prof.us[PROF_LOGIC] += (uint64_t) (esp_timer_get_time() - logic_t0);
    s.frame++;
    // The firefly flickers and flies: its box, where it was and where it is.
    if (s.fly || s.fly_shown) {
        const lv_area_t now = fly_area();
        if (s.fly_shown) {
            invalidate_move(&s.fly_drawn, &now);
        } else {
            lv_obj_invalidate_area(s.obj, &now);
        }
        s.fly_drawn = now;
        s.fly_shown = s.fly;
    }
    // Hidden under the menu or the dark it isn't drawn: no new sizes to
    // render (the menu draws its own Yoru meanwhile).
    PROF(PROF_BALL, s.ball = lv_obj_has_flag(s.obj, LV_OBJ_FLAG_HIDDEN) ? NULL
                                                                         : look_ball(s.body.rx, s.body.ry, s.frame));

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
            PROF(PROF_EYE_RENDER, eye_render(es, &s.eye[i]));
        }
        lv_area_t now = eye_area(i);
        if (!es->valid || reshaped || eyes_moved || bridged || !areas_equal(&now, &es->drawn)) {
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
        if (!areas_equal(&now, &s.hand_drawn[i]) || s.clip != NULL || bridged) {
            invalidate_move(&s.hand_drawn[i], &now);
            s.hand_drawn[i] = now;
        }
    }


    // The clip's effects, as one box, and once more as they go.
    lv_area_t fx_now;
    const bool fx = fx_area(&fx_now);
    if (fx || s.fx_shown) {
        if (s.fx_shown && fx) {
            invalidate_move(&s.fx_drawn, &fx_now);
        } else if (fx) {
            lv_obj_invalidate_area(s.obj, &fx_now);
        } else {
            lv_obj_invalidate_area(s.obj, &s.fx_drawn);
        }
        if (fx) {
            s.fx_drawn = fx_now;
        }
        s.fx_shown = fx;
    }

    // The bolt: its column while it runs, and once more as it goes.
    if (s.zapping || s.zap_shown) {
        lv_area_t now = zap_area();
        if (s.zap_shown) {
            invalidate_move(&s.zap_drawn, &now);
        } else {
            lv_obj_invalidate_area(s.obj, &now);
        }
        s.zap_drawn = now;
        s.zap_shown = s.zapping;
    }

    // The low battery, when it moves, blinks or goes.
    const int32_t lowbat = lowbat_key();
    if (lowbat >= 0 || s.lowbat_key >= 0) {
        lv_area_t now = lowbat_area();
        if (lowbat != s.lowbat_key || !areas_equal(&now, &s.lowbat_drawn)) {
            if (s.lowbat_key >= 0) {
                invalidate_move(&s.lowbat_drawn, &now);
            } else {
                lv_obj_invalidate_area(s.obj, &now);
            }
            s.lowbat_drawn = now;
            s.lowbat_key = lowbat;
        }
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
    s.lowbat_key = -1;
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

    s.timer = lv_timer_create(frame_cb, EYES_FRAME_MS, NULL);
    return s.obj;
}

void eyes_set_pace(eyes_pace_t pace)
{
    if (s.timer == NULL) {
        return;
    }
    if (pace == EYES_PACE_STOPPED) {
        lv_timer_pause(s.timer);
        lv_obj_add_flag(s.obj, LV_OBJ_FLAG_HIDDEN);
        s.stopped = true;
        return;
    }
    if (s.stopped) {
        s.stopped = false;
        lv_obj_remove_flag(s.obj, LV_OBJ_FLAG_HIDDEN);
        s.last_tick = lv_tick_get(); // no jump for the time it was stopped
    }
    lv_timer_set_period(s.timer, pace == EYES_PACE_SLOW ? EYES_FRAME_MS * 2 : EYES_FRAME_MS);
    lv_timer_resume(s.timer);
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

void eyes_countdown(uint32_t ms)
{
    s.cd_ms = ms;
    s.cd_from_ms = s.t_ms;
}

void eyes_set_battery(eyes_battery_t state, int percent)
{
    s.bat = state;
    s.bat_pct = percent;
}

void eyes_zap(void)
{
    s.zapping = true;
    s.zap_ms = 0;
    s.charge_deg = 0; // the ring fills up with the energy
}

cJSON *eyes_profile(bool reset)
{
    const int64_t now = esp_timer_get_time();
    cJSON *r = cJSON_CreateObject();
    const double secs = s_prof.since_us > 0 ? (double) (now - s_prof.since_us) / 1e6 : 0;
    if (secs > 0) {
        cJSON_AddNumberToObject(r, "fps", s_prof.frames / secs);
        cJSON_AddNumberToObject(r, "draws_per_s", s_prof.draws / secs);
        cJSON_AddNumberToObject(r, "kpx_per_s", (double) s_prof.px / 1000.0 / secs);
        cJSON *ms = cJSON_AddObjectToObject(r, "ms_per_s");
        for (int i = 0; i < PROF_COUNT; i++) {
            cJSON_AddNumberToObject(ms, PROF_NAMES[i], (double) s_prof.us[i] / 1000.0 / secs);
        }
    }
    if (reset || s_prof.since_us == 0) {
        memset(&s_prof, 0, sizeof(s_prof));
        s_prof.since_us = now;
    }
    return r;
}

void eyes_set_level(float level)
{
    s.tlevel = fmaxf(0, fminf(1, level));
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

void eyes_play(clip_id_t clip, bool mirror)
{
    eyes_play_fx(clip, mirror, FX_DEFAULT_RGB, NULL);
}

void eyes_play_fx(clip_id_t clip, bool mirror, uint32_t rgb, const char *glyph)
{
    const clip_t *c = clips_get(clip);
    if (c == NULL || c->n < 2) {
        return;
    }
    // From another clip cut short, or from the engine's own pose.
    bridge_from_screen(s.clip != NULL ? BRIDGE_CUT_MS : BRIDGE_IN_MS);
    s.fx_rgb = rgb;
    s.fx_glyph = glyph;
    s.clip_mirror = mirror;
    // The clip has the hands from its first frame, and its own blinks.
    s.clutch = false;
    s.blink_ms = 0;
    s.lid = 1;
    s.clip = c;
    s.clip_ms = 0;
    clips_frame(c, 0, &s.cf);
    if (mirror) {
        clip_mirror(&s.cf);
    }
}

bool eyes_playing(void)
{
    return s.clip != NULL;
}

clip_id_t eyes_clip(void)
{
    for (int i = CLIP_NONE + 1; i < CLIP_COUNT && s.clip != NULL; i++) {
        if (clips_get((clip_id_t) i) == s.clip) {
            return (clip_id_t) i;
        }
    }
    return CLIP_NONE;
}

void eyes_stop_clip(void)
{
    if (s.clip != NULL) {
        bridge_from_screen(BRIDGE_OUT_MS);
        clip_end();
    }
}

void eyes_clutch(int32_t x, int32_t y)
{
    s.clutch = true;
    s.clutch_x = x;
    s.clutch_y = y;
}

void eyes_let_go(void)
{
    if (!s.clutch) {
        return;
    }
    s.clutch = false;
    // From wherever they held it, the hands ease back behind the body.
    bridge_from_screen(BRIDGE_OUT_MS);
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


void eyes_cross(float amount)
{
    s.tcross = clampf(amount, 0, 1);
}

void eyes_firefly(bool on, float x, float y)
{
    s.fly = on;
    if (on) {
        s.fly_x = x;
        s.fly_y = y;
    }
}
