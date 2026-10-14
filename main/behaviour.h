#pragma once

/* The bridge between what Yoru feels (pet.c) and its eyes (eyes.c), and
 * how it answers touch, until the senses arrive (M2):
 * - the eyes rest in a look that suits the pet's mood (bored, grumpy, sad,
 *   sleepy, hot, or neutral when it's fine) and play its reactions;
 * - now and then a whim that suits the mood, more often when it's lively;
 * - left alone it gets sleepy, then falls asleep and dims the screen (the
 *   times and the brightness are settings);
 * - a tap pokes it: happy once or twice, grumpy at the third in a row;
 * - a finger on the screen is watched, and stroking it makes it happy;
 * - pokes and strokes move the pet's mood, so it remembers being petted;
 * - focus mode: no whims and no reactions;
 * - a long press puts it to sleep, a tap wakes it up. */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start on the face from eyes_create(). With the LVGL lock held. */
void behaviour_start(lv_obj_t *face);

#ifdef __cplusplus
}
#endif
