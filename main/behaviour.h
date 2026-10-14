#pragma once

/* What Yoru does on its own and how it answers touch, until the mood
 * engine and the senses arrive (M2, M5):
 * - it looks around and blinks; now and then it gets curious or lost in
 *   thought for a few seconds;
 * - left alone it gets sleepy, then falls asleep and dims the screen;
 * - a tap pokes it: happy once or twice, grumpy at the third in a row;
 * - a finger on the screen is watched, and stroking it makes it happy;
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
