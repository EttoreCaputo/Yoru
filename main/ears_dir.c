#include "ears_dir.h"

#include <math.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "playback.h"

static const char *TAG = "ears_dir";

/* The slots of the two mics in the ES7210's frames. */
#define MIC_A 0
#define MIC_B 2
/* Mic A is on the right as you look at the screen: +1. Flip if the eyes
 * turn away from a clap. */
#define MIC_A_SIDE 1
/* A few centimetres at 16 kHz: about 3 samples at most between the mics,
 * looked for a little further out. */
#define MAX_LAG 5
#define FULL_LAG 3.0f
/* A chunk this much louder than the room (20 dB: a clap, a door, a word
 * said close by), and loud anyway. */
#define ONSET_RATIO 100.0f
#define MIN_ENERGY 2.0e5f
#define COOL_US (800 * 1000)
/* Nearer the middle than this is "ahead": the eyes stay where they are. */
#define AHEAD 0.25f

static ears_dir_cb_t s_cb;
static float s_floor = 1.0e5f;
static int64_t s_last_us;

void ears_dir_on(ears_dir_cb_t cb)
{
    s_cb = cb;
}

/* Correlation of A against B moved by `lag`, over the whole chunk. */
static float xcorr(const int16_t *f, int count, int ch, int lag)
{
    float sum = 0;
    for (int i = MAX_LAG; i < count - MAX_LAG; i++) {
        sum += (float) f[i * ch + MIC_A] * (float) f[(i + lag) * ch + MIC_B];
    }
    return sum;
}

void ears_dir_feed(const int16_t *frames, int count, int channels)
{
    if (s_cb == NULL || channels <= MIC_B || count < 8 * MAX_LAG) {
        return;
    }
    float e = 0;
    for (int i = 0; i < count; i++) {
        const float v = frames[i * channels + MIC_A];
        e += v * v;
    }
    e /= (float) count;
    const bool onset = e > MIN_ENERGY && e > s_floor * ONSET_RATIO;
    // The room's level, slow to rise so a bang doesn't become the room.
    s_floor += (e - s_floor) * (e > s_floor ? 0.01f : 0.1f);
    const int64_t now = esp_timer_get_time();
    if (!onset || now - s_last_us < COOL_US || playback_active()) {
        return;
    }
    s_last_us = now;

    int best = 0;
    float best_v = -INFINITY, prev = 0, next = 0;
    float c[2 * MAX_LAG + 1];
    for (int lag = -MAX_LAG; lag <= MAX_LAG; lag++) {
        c[lag + MAX_LAG] = xcorr(frames, count, channels, lag);
        if (c[lag + MAX_LAG] > best_v) {
            best_v = c[lag + MAX_LAG];
            best = lag;
        }
    }
    // Between samples: the top of a parabola through the best three.
    float lag = (float) best;
    if (best > -MAX_LAG && best < MAX_LAG) {
        prev = c[best + MAX_LAG - 1];
        next = c[best + MAX_LAG + 1];
        const float den = prev - 2 * best_v + next;
        if (den < 0) {
            lag += 0.5f * (prev - next) / den;
        }
    }
    // B hears it later (lag > 0): it came from A's side.
    float x = (float) MIC_A_SIDE * lag / FULL_LAG;
    x = x > 1 ? 1 : (x < -1 ? -1 : x);
    ESP_LOGD(TAG, "onset %.0f dB over the room, lag %.2f, x %.2f", 10 * log10f(e / s_floor), lag, x);
    if (fabsf(x) >= AHEAD) {
        s_cb(x);
    }
}
