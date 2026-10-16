#pragma once

/* Which side a sudden sound came from, from the two mics (TDM slots 0 and 2,
 * docs/ROADMAP.md M3): when a chunk is much louder than the room, the delay
 * between the mics says left or right, and Yoru turns its eyes there. The
 * mics are a few centimetres apart, so it is only ever "left", "ahead" or
 * "right". Only the direction is kept. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** -1 left .. 1 right, on the screen as you look at it. From the voice's
 * feed task: keep it short. */
typedef void (*ears_dir_cb_t)(float x);

void ears_dir_on(ears_dir_cb_t cb);

/** A chunk of raw interleaved frames as the ES7210 gives them, before the
 * AFE. From the voice's feed task. */
void ears_dir_feed(const int16_t *frames, int count, int channels);

#ifdef __cplusplus
}
#endif
