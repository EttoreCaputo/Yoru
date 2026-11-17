#pragma once

/* Talking to Yoru without the Mac (docs/protocol.md, Cloud voice): with no
 * host, the WiFi up and the services Yoru Hub handed over (`cloud/set`), what
 * is said after the wake word (or with PWR held) is recorded in PSRAM and
 * sent from the board itself: to a speech-to-text service, to the brain
 * (OpenAI's Chat Completions or Anthropic's Messages, with who Yoru is and
 * what the board knows now), and the answer to a voice that the speaker
 * plays as it arrives. Tags in the answer act on the board (a timer, an app,
 * the volume...). Then it listens for a follow-up, as with the host. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The longest utterance kept for the services; the rest is cut. */
#define CLOUD_REC_MAX_MS 15000

/** Read the services from NVS and start the task. After voice_start() and
 * playback_start(). */
void cloud_init(void);

/** The host's `cloud/set`: the services and their keys, or `{"clear":true}`.
 * Kept in NVS. False, with why in `err`, for a bad one. */
bool cloud_set(const cJSON *params, char *err, size_t len);

/** What is set, without the keys: for get_state and `cloud/get`. */
cJSON *cloud_status(void);

/** It has services and the WiFi is up: without a host, what is said goes to
 * them. */
bool cloud_ready(void);

/** From the voice task: a new utterance is for the cloud. Stops an answer
 * being spoken. False when it can't take one now (no services, no WiFi, the
 * last one still being sent). */
bool cloud_begin(void);
/** Its audio, 16 kHz mono s16. */
void cloud_feed(const int16_t *pcm, size_t samples);
/** It ended with speech: send it, and answer. */
void cloud_submit(void);
/** It ended without speech: nothing to send. */
void cloud_drop(void);

/** For tests (`debug/cloud`): answer `text` as if it had been heard. */
bool cloud_ask(const char *text);

#ifdef __cplusplus
}
#endif
