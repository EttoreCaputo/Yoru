#pragma once

/* What the eyes show of the voice state (listening, thinking, speaking,
 * error). voice.c and playback.c call it as they do in DualEye, where it
 * draws a ring or eyes over the watch faces; here it hands the state to
 * behaviour.c and the level to the eyes. */

#include "voice.h"

#ifdef __cplusplus
extern "C" {
#endif

/** With the LVGL lock held. */
void ui_voice_show(voice_state_t state);

/** Mic level while listening, speaker level while speaking, 0..1. With the
 * LVGL lock held. */
void ui_voice_set_level(float level);

/** A ring running out over `ms`: how long the board still listens without
 * the wake word. 0 takes it away. With the LVGL lock held. */
void ui_voice_countdown(uint32_t ms);

#ifdef __cplusplus
}
#endif
