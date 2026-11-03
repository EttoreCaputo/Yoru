#pragma once

/* What Yoru understands without a host: a few phrases in English that
 * MultiNet hears on the board (voice.c), after the wake word or with PWR
 * held: timers, the time, sleep, the apps and the volume. With a host
 * there, everything goes to it as before. The list is in docs/README.md. */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Hand voice.c the phrases. After voice_start(). */
void commands_start(void);
/** A tag from the brain in the cloud (cloud.c), without its brackets:
 * "timer 300", "pomodoro", "stop", "pause", "resume", "volume up",
 * "open games", "sleep", "face happy". False for one it doesn't know. Takes
 * the LVGL lock. */
bool commands_tag(const char *tag);

#ifdef __cplusplus
}
#endif
