#pragma once

/* Yoru's face: two glowing eyes on black, nothing else (docs/CONCEPT.md).
 * A mood is a set of targets (shape, lids, tilt, colour) the eyes ease
 * towards; blinks, saccades, squash and the small overlays (thinking dots,
 * z's, a sweat drop, hearts) run on top. This module only draws: pet.c
 * decides what Yoru feels and behaviour.c turns it into moods here. All calls with the LVGL
 * lock held. */

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
    EYES_MOOD_COUNT,
} eyes_mood_t;

/** Build the face on `parent` (a full screen). Returns the object that
 * covers the screen, for touch events. The eyes open from closed. */
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

/** A poke: the eyes squash and spring back. */
void eyes_poke(void);
void eyes_blink(void);

#ifdef __cplusplus
}
#endif
