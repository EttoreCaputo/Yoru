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
 * - in a conversation the eyes listen, think and speak, and nothing else
 *   plays; the wake word wakes it up;
 * - the host's mouse pointer, when it sends it, is followed;
 * - a long press puts it to sleep, a tap wakes it up;
 * - its status (thinking, done, error) tints the sphere during a conversation. */

#include "lvgl.h"
#include "voice.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start on the face from eyes_create(). With the LVGL lock held. */
void behaviour_start(lv_obj_t *face);

/** The voice state changed (ui_voice.c). With the LVGL lock held. */
void behaviour_voice(voice_state_t state);

/** Play one of the scenes (pet.c's names, as in DualEye) now, waking it if
 * asleep; false for an unknown one. With the LVGL lock held. */
bool behaviour_play(const char *name);
/** The scenes' names, at most `max`; returns how many. */
int behaviour_scenes(const char **names, int max);

/** Where the host's pointer is, -1..1 each way (rpc.c's eyes/gaze). Safe
 * from any task. */
void behaviour_gaze(float x, float y);

#ifdef __cplusplus
}
#endif
