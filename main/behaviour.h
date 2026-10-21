#pragma once

/* The bridge between Yoru's idle life (life.c) and its eyes (eyes.c), and how
 * it answers touch, the senses and what comes for you:
 * - when nothing else has its attention, life.c runs it: it floats about;
 * - left alone (nobody touching, talking or at the computer) it gets
 *   sleepy, then naps and dims the screen (the times and the brightness are
 *   settings);
 * - a tap on Yoru pokes it: happy once or twice, grumpy at the third in a
 *   row;
 * - a finger on the screen is watched, and stroking it makes it happy;
 * - focus mode: no reactions;
 * - in a conversation the eyes listen, think and speak, and nothing else
 *   plays; the wake word wakes it up;
 * - the host's mouse pointer, when it sends it, is followed;
 * - a long press puts it to sleep, a tap wakes it up;
 * - its status (thinking, done, error) tints the sphere during a conversation;
 * - a quick flick across the face is a slap: it is knocked aside, and the
 *   third pushes you away;
 * - a swipe down from the top edge opens the menu (menu.c), which Yoru
 *   peeks at;
 * - the case is an input too (motion.c): lifted it wakes and looks at you,
 *   shaken or spun it gets dizzy, knocked it's poked, two knocks call it,
 *   rocked it gets drowsy, dropped it panics, face down it sleeps with the
 *   mic off; it leans with the case, and turns to a sudden sound;
 * - PWR puts it to sleep and wakes it; at night the screen is dimmer;
 * - the inbox (inbox.c): a chirp as a notification comes, a knock on the
 *   glass for a high one (a touch then opens the inbox), the alarm and a
 *   lit screen for an urgent one;
 * - a timer or reminder that rings (timers.c): it jumps like an alarm
 *   clock, flapping its hands;
 * - the host's assistant calling a connector: it runs the errand
 *   (errand.c), diving into a portal and coming back with a parcel. */

#include "board_pmu.h"
#include "lvgl.h"
#include "motion.h"
#include "voice.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start on the face from eyes_create(). With the LVGL lock held. */
void behaviour_start(lv_obj_t *face);

/** The voice state changed (ui_voice.c). With the LVGL lock held. */
void behaviour_voice(voice_state_t state);

/** Play one of the scenes (names as in DualEye) now, waking it if
 * asleep; false for an unknown one. With the LVGL lock held. */
bool behaviour_play(const char *name);
/** The scenes' names, at most `max`; returns how many. */
int behaviour_scenes(const char **names, int max);

/** A gesture of the case (motion.c's callback). Takes the LVGL lock. */
void behaviour_motion(motion_event_t event);

/** A sudden sound from one side, -1 left .. 1 right (ears_dir.c's
 * callback): the eyes turn there for a moment. Takes the LVGL lock. */
void behaviour_heard_from(float x);



/** PWR (board_pmu.c's callback). A tap: to sleep and back, out of the menu
 * or the view, and the only way to light a dark screen again. Held:
 * push-to-talk to Yoru. Takes the LVGL lock. */
void behaviour_power_key(board_pmu_key_t ev);

/** BOOT pressed (board_button.c): the inbox, or out of the menu or the
 * inbox when one is up; held, nothing. With the LVGL lock held. */
void behaviour_boot_key(bool long_press);

/** The host's assistant called a connector (`show_fetch`): `state` is
 * `start`, `done` or `failed`; `id` pairs a start with its end. A start
 * shows only while Yoru is awake, on the screen and free or thinking.
 * Returns whether Yoru is on the errand. With the LVGL lock held. */
bool behaviour_fetch(const char *id, const char *source, const char *state);

/** Where the host's pointer is, -1..1 each way (rpc.c's eyes/gaze). Safe
 * from any task. */
void behaviour_gaze(float x, float y);

#ifdef __cplusplus
}
#endif
