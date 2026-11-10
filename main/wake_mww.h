#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Wake words of Yoru's own, heard by a microWakeWord model (docs/wakeword.md):
 * ESP-SR's WakeNet only ships Espressif's words. The models are trained on
 * the computer (tools/wakeword) and built into the firmware; voice.c feeds
 * the one in use with what the AFE gives back, in place of WakeNet.
 */

/** How many words are built in, and each one's id (as set_wake_word takes
 * it) and the word as people say it. */
int wake_mww_count(void);
const char *wake_mww_id(int i);
const char *wake_mww_word(int i);
/** The index of `id`, or -1. */
int wake_mww_find(const char *id);

/** Load word `i` and listen for it; the one loaded before is dropped.
 * False when the model doesn't load: nothing is loaded then. */
bool wake_mww_start(int i);
/** Drop the loaded model and its memory. */
void wake_mww_stop(void);
/** 16 kHz mono from the AFE. True once when the word was heard. */
bool wake_mww_feed(const int16_t *samples, size_t count);
/** Forget what was heard so far: after an utterance, so that its last
 * sounds don't count towards the next wake. */
void wake_mww_reset(void);
/** The last second's highest probability, 0-255, and how long one step of
 * the model takes on average, in microseconds: for `debug/perf`. */
int wake_mww_peak(void);
int wake_mww_step_us(void);

#ifdef __cplusplus
}
#endif
