#pragma once

/* The bridge between Yoru's idle life (life.c) and its eyes (eyes.c), and how
 * it answers touch, the senses and the coding agents:
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
 * - the coding agents on the computer (agents.c): a nod when a turn ends, a
 *   wince when one fails, a blue tint while one works; while one waits for
 *   you it knocks on the glass, and a touch then brings its terminal up;
 *   their permission requests and questions (approval.c, question.c) wake it
 *   and it waits with you; a swipe up from the bottom edge, or BOOT, opens
 *   the agents view (agents_ui.c). */

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



/** A short press of PWR (board_pmu.c's callback): to sleep and back, or
 * out of the menu. Takes the LVGL lock. */
void behaviour_power_key(void);

/** BOOT pressed (board_button.c): the terminal of the agent waiting on you,
 * else the agents view; held, the host's "do not disturb agents". With the
 * LVGL lock held. */
void behaviour_boot_key(bool long_press);

/** Where the host's pointer is, -1..1 each way (rpc.c's eyes/gaze). Safe
 * from any task. */
void behaviour_gaze(float x, float y);

#ifdef __cplusplus
}
#endif
