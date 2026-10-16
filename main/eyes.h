#pragma once

/* Yoru on screen (docs/CONCEPT.md): a matte sphere with two small dark eyes
 * and two floating hands, in the middle of the black disc. A mood is a set
 * of targets (eye shape, lids, tilt) the eyes ease towards; blinks, saccades,
 * squash, gestures, the state tint, drifting away to sleep and the overlays
 * (thinking dots, z's, a sweat drop, hearts) run on top. The pictures come
 * from look.c; this module only places and moves them. pet.c decides what Yoru feels and
 * behaviour.c turns it into calls here. All calls with the LVGL lock held. */

#include <stdbool.h>

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
    EYES_GESTURE_COUNT,
} eyes_gesture_t;

/** Build the face on `parent` (a full screen). Returns the object that
 * covers the screen, for touch events. The eyes open from closed. Needs
 * look_init() first. */
lv_obj_t *eyes_create(lv_obj_t *parent);

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

/** Play a gesture now, replacing the one playing. */
void eyes_gesture(eyes_gesture_t gesture);
const char *eyes_gesture_name(eyes_gesture_t gesture);

#ifdef __cplusplus
}
#endif
