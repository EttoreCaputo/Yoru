#pragma once

/* Yoru on screen (docs/CONCEPT.md): a matte sphere with two small dark eyes
 * and two floating hands, floating in the black disc. A mood is a set of
 * targets (eye shape, lids, tilt) the eyes ease towards; blinks, saccades,
 * squash, gestures, the state tint, drifting away to sleep and the overlays
 * (thinking dots, z's, a sweat drop, hearts) run on top.
 *
 * Yoru lives in a space seen in perspective: x and y are pixels at the
 * resting distance, z the distance (1 resting, 3 three times as far, 0.6 up
 * at the glass). Far away it is smaller, dimmer and drawn towards the middle
 * of the screen; near it fills the disc. It moves along smooth paths through
 * that space, or follows a point like a spring, leaning into the motion with
 * its eyes leading the way. The pictures come from look.c; this module only
 * places and moves them. behaviour.c and life.c decide what Yoru does and
 * turn it into calls here. All calls with the LVGL lock held. */

#include <stdbool.h>

#include "clips.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EYES_MOOD_NEUTRAL,
    EYES_MOOD_HAPPY,
    EYES_MOOD_CURIOUS,
    EYES_MOOD_THINKING,
    EYES_MOOD_GRUMPY,
    EYES_MOOD_HOT,
    EYES_MOOD_SLEEPY,
    EYES_MOOD_ASLEEP,
    EYES_MOOD_SAD,
    EYES_MOOD_LOVING,
    EYES_MOOD_SURPRISED,
    EYES_MOOD_BORED,
    /* A conversation: attentive while it hears you, warm while it talks. */
    EYES_MOOD_LISTENING,
    EYES_MOOD_SPEAKING,
    /* Shaken or spun: spiral eyes rolling round, stars circling its head. */
    EYES_MOOD_DIZZY,
    /* Dropped or startled: tall worried eyes, the body trembling. */
    EYES_MOOD_SCARED,
    /* The menu is open: small eyes, up at its edge. */
    EYES_MOOD_PEEK,
    /* A permission request is up: small, head tilted, waiting for an answer. */
    EYES_MOOD_WAITING,
    /* Joy, love and wonder: the happy, loving and surprised pills. */
    EYES_MOOD_DELIGHTED,
    EYES_MOOD_SMITTEN,
    EYES_MOOD_STARRY,
    /* Half-lidded and soft, looking up: lost in a daydream. */
    EYES_MOOD_DREAMY,
    EYES_MOOD_COUNT,
} eyes_mood_t;

/* What the tint, the halo and the badge say. */
typedef enum {
    EYES_STATUS_IDLE,
    EYES_STATUS_THINKING, // blue
    EYES_STATUS_DONE,     // green
    EYES_STATUS_ERROR,    // red
} eyes_status_t;

/* The hands' gestures. Each plays once: the hands pop out, do it and tuck
 * back behind the body. */
typedef enum {
    EYES_GESTURE_NONE,
    EYES_GESTURE_WAVE,
    EYES_GESTURE_POINT,
    EYES_GESTURE_COVER,   // both hands over the eyes, then one slides down to peek
    EYES_GESTURE_THINK,   // a hand under the chin
    EYES_GESTURE_CLAP,
    EYES_GESTURE_STRETCH, // hands far out, the body stretched tall
    EYES_GESTURE_HOLD,    // both hands at the sides of its head, wobbling (dizzy)
    EYES_GESTURE_SHOO,    // one hand pushes you away, twice (annoyed)
    EYES_GESTURE_FLAIL,   // both hands up, flapping (falling)
    EYES_GESTURE_DIVE,    // both hands together over its head, a diver about to jump
    EYES_GESTURE_CARRY,   // both hands low in front, holding what eyes_carry() gave it
    EYES_GESTURE_COUNT,
} eyes_gesture_t;

/* How a move goes. */
typedef enum {
    EYES_PATH_GLIDE, // eases in and out
    EYES_PATH_DRIFT, // slow and soft, floating
    EYES_PATH_DART,  // a small wind-up, a dash and a little overshoot
    EYES_PATH_HOP,   // a hop for each stretch, squashing as it lands
} eyes_path_t;

typedef struct {
    float x, y, z;
} eyes_pos_t;

/* A path through at most this many points, besides where it starts. */
#define EYES_PATH_POINTS 8

/** Build the face on `parent` (a full screen). Returns the object that
 * covers the screen, for touch events. The eyes open from closed. Needs
 * look_init() first. */
lv_obj_t *eyes_create(lv_obj_t *parent);

typedef enum {
    EYES_PACE_FULL,    // 30 frames a second
    EYES_PACE_SLOW,    // 15: asleep on battery, the dot and its z's
    EYES_PACE_STOPPED, // hidden and not stepped: the screen is dark
} eyes_pace_t;

/** How often the eyes move and draw (power.c). With the LVGL lock held. */
void eyes_set_pace(eyes_pace_t pace);

/** The same mood again changes nothing, so it can be set every tick. */
void eyes_set_mood(eyes_mood_t mood);
eyes_mood_t eyes_get_mood(void);
const char *eyes_mood_name(eyes_mood_t mood);

/** Look at a point, -1..1 on each axis (0, 0 = straight ahead), until
 * eyes_look_around(). */
void eyes_look_at(float x, float y);
/** Back to looking around on its own. */
void eyes_look_around(void);

/** The voice's level, 0..1 (the mic while listening, the speaker while
 * speaking): the eyes swell a little with it. 0 when nobody talks. */
void eyes_set_level(float level);

/** A ring round the body running out over `ms`: how long it still listens
 * without the wake word. 0 takes it away. */
void eyes_countdown(uint32_t ms);

/* The battery beside Yoru (battery.c). */
typedef enum {
    EYES_BATTERY_NONE,
    /* A thin ring round the body, filled as far as the charge, with a spark
     * of energy running along it into Yoru. It shows round the far dot
     * asleep too; the follow-up's ring goes over it. */
    EYES_BATTERY_CHARGING,
    /* A little battery by its head, amber; red and blinking when EMPTY. Not
     * asleep. */
    EYES_BATTERY_LOW,
    EYES_BATTERY_EMPTY,
} eyes_battery_t;

/** What the battery shows, and the charge (0..100). The same again changes
 * nothing, so it can be set every tick. */
void eyes_set_battery(eyes_battery_t state, int percent);
/** Plugged in: a bolt shoots up from the bottom edge into Yoru, who jolts
 * with the energy, eyes wide for a moment; the charge ring fills up from
 * empty to the charge. */
void eyes_zap(void);

/** A poke: the body squashes and springs back. */
void eyes_poke(void);
/** A knock from the side (a slap, a knock on the case, a landing): the body
 * is pushed by (dx, dy), about -1..1 of its size, and springs back with a
 * wobble. */
void eyes_knock(float dx, float dy);
/** How the case leans, -1..1 each way (motion.c): the eyes roll towards the
 * low side, as if they had weight, and the body sags a little. */
void eyes_lean(float x, float y);
void eyes_blink(void);

/** Fade the tint, halo and badge to a status. */
void eyes_set_status(eyes_status_t status);
eyes_status_t eyes_get_status(void);

/** Play a gesture now, replacing the one playing (and any clip). */
void eyes_gesture(eyes_gesture_t gesture);

/** Play a clip keyed in the Rive file (docs/animations.md): for its length
 * it moves the body, the hands and the eyes itself, then the engine takes
 * over from the rest pose it ends in (where it ends, if it moved away).
 * `mirror` plays it left for right. Moves, knocks and the mood go on
 * underneath; a gesture stops it. A `_loop` clip plays until stopped. */
void eyes_play(clip_id_t clip, bool mirror);
/** The same, with the clip's effects (its glow, sparkles and prop) in `rgb`,
 * the prop a card showing `glyph` (an LV_SYMBOL or a few letters, kept by
 * the caller), or a glowing orb when NULL. eyes_play() uses gold and the orb. */
void eyes_play_fx(clip_id_t clip, bool mirror, uint32_t rgb, const char *glyph);
bool eyes_playing(void);
/** The clip playing, or CLIP_NONE. */
clip_id_t eyes_clip(void);
void eyes_stop_clip(void);
const char *eyes_gesture_name(eyes_gesture_t gesture);

/** A finger held on the screen at (x, y): both hands reach out and hold it,
 * in front of the body, following it as it moves (call again with the new
 * point), until eyes_let_go() or a gesture. */
void eyes_clutch(int32_t x, int32_t y);
/** The finger lifted: the hands let go and tuck away behind the body. */
void eyes_let_go(void);

/** What the hands hold during EYES_GESTURE_CARRY: a little parcel in `rgb`
 * (0xRRGGBB) with a ribbon, brought back from an errand (errand.c). 0: none,
 * and the parcel goes at once. */
void eyes_carry(uint32_t rgb);

/** Move along a smooth curve through `pts` (from where it is) in `ms`;
 * 0 ms jumps there. Replaces any move or follow. */
void eyes_move(const eyes_pos_t *pts, int n, uint32_t ms, eyes_path_t path);
void eyes_move_to(float x, float y, float z, uint32_t ms, eyes_path_t path);
/** Follow a point like a spring, `stiffness` about 5 (lazy) .. 80 (snappy),
 * until the next move. */
void eyes_follow(float x, float y, float z, float stiffness);
/** A move is under way (a follow counts until it has nearly arrived). */
bool eyes_moving(void);
eyes_pos_t eyes_where(void);
/** The point in space at distance `z` that shows at screen point (x, y). */
eyes_pos_t eyes_unproject(int32_t x, int32_t y, float z);
/** Screen point (x, y) is on Yoru's body. */
bool eyes_hit(int32_t x, int32_t y);

/** Shut one eye (0 left, 1 right) for `ms` (0: a quick wink). */
void eyes_wink(int eye, uint32_t ms);
/** Cross-eyed, 0 (not) .. 1: the eyes turn in towards each other. */
void eyes_cross(float amount);
/** A firefly at (x, y), screen pixels from the middle, flickering; `on`
 * false takes it away. Move it by calling again. */
void eyes_firefly(bool on, float x, float y);

#ifdef __cplusplus
}
#endif
