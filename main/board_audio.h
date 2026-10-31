#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ES8311 (speaker, behind an NS4150B amplifier) and ES7210 (two mics plus
 * the speaker's loopback) share one I2S port in full duplex and the board's
 * I2C bus. Pins from Waveshare's BSP and schematic. */
#define BOARD_AUDIO_I2S_MCLK_GPIO 16
#define BOARD_AUDIO_I2S_BCLK_GPIO 9
#define BOARD_AUDIO_I2S_WS_GPIO 45
#define BOARD_AUDIO_I2S_DIN_GPIO 10  /* ES7210 -> ESP32 */
#define BOARD_AUDIO_I2S_DOUT_GPIO 8  /* ESP32 -> ES8311 */
#define BOARD_AUDIO_PA_GPIO 46

/** ESP-SR wants 16 kHz, so everything runs at it. */
#define BOARD_AUDIO_SAMPLE_RATE 16000
/** ES7210 TDM slots, read interleaved: MIC1, MIC3, MIC2, MIC4 (xiaozhi's
 * order for this board). MIC3 is the speaker loopback, the AEC reference;
 * MIC4 is not connected. Checked on the board (0.3.0, audio_selftest.py rec
 * with a tone): slot 1 carries the tone with no room noise, slots 0 and 2
 * hear the room, slot 3 is silent. */
#define BOARD_AUDIO_IN_CHANNELS 4
#define BOARD_AUDIO_MIC_SLOT 0
#define BOARD_AUDIO_REF_SLOT 1
#define BOARD_AUDIO_MIC2_SLOT 2

/** Bring up I2S and both codecs on the shared I2C bus. Output starts muted. */
esp_err_t board_audio_init(void);

/** Blocking write of mono 16-bit samples to the speaker. */
esp_err_t board_audio_write(const int16_t *samples, size_t count);

/** Blocking read of `frames` frames of BOARD_AUDIO_IN_CHANNELS interleaved samples. */
esp_err_t board_audio_read(int16_t *frames_out, size_t frames);

/** 0..100. */
esp_err_t board_audio_set_volume(int volume);
esp_err_t board_audio_set_mute(bool mute);
/** Analog gain for every input channel, in dB (ES7210: 0..37.5). At init
 * the mic gets the maximum and the loopback less, so it can't clip. */
esp_err_t board_audio_set_in_gain(float db);

/** Lazy: the speaker's codec and amplifier power down while it's muted, and
 * come back when it's unmuted (a few I2C writes). On battery (power.c). */
void board_audio_set_lazy(bool lazy);

/** The mics' codec on or off. Off, board_audio_read() fails: only once no one
 * reads (voice.c). On the ESP32-S3 the I2S port stays on while the speaker's
 * half is. */
esp_err_t board_audio_set_mic(bool on);

#ifdef __cplusplus
}
#endif
