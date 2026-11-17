#pragma once

/* Speech from the host: PCM on `audio_down` (docs/protocol.md) goes into a
 * jitter buffer in PSRAM and out to the ES8311 from a task on core 1. The
 * host paces the stream; the board shows "speaking" while it plays, with the
 * output level on the ring, and says `playback_end` when it's done. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sounds.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Allocate the buffer and start the task. Call after board_audio_init(). */
void playback_start(void);

/** True once it runs. */
bool playback_available(void);

/** The link's callback for `audio_down` frames. */
void playback_receive(uint8_t *payload, size_t len);

/** Speech the board fetched itself (cloud.c): `n` samples of stream `id`,
 * waiting for room in the buffer; `end` with the last. False once the
 * stream was stopped (barge-in) or another took the speaker. From one task. */
bool playback_push(uint8_t id, const int16_t *pcm, uint32_t n, bool end);

/** Drop what's buffered and end the stream being played, if any. */
void playback_stop(void);

/** The same, because the wake word was heard over it: the host's stream
 * ends with `barge_in`. */
void playback_barge_in(void);

/** Play `sound` made on the board, after those queued before it, unless the
 * host's speech is playing. It doesn't show `speaking` and isn't reported
 * to the host. Safe from any task. */
void playback_sound(sound_t sound);

/** Yoru's own sounds (from SOUND_FIRST_YORU on: the eyes' scenes, hello...) are
 * played, or quietly dropped. On by default. */
void playback_set_yoru_sounds(bool on);
bool playback_yoru_sounds(void);

/** A stream is playing (or buffering to start). */
bool playback_active(void);
/** Since boot: times the speech ran out before its end came (the host or
 *  the network late), and its frames that never arrived. */
void playback_totals(uint32_t *underruns, uint32_t *lost);

#ifdef __cplusplus
}
#endif
