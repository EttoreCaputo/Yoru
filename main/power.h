#pragma once

/* Power profiles (0.12.0). On USB the board is at its best: Desk, as before.
 * On battery it saves what it can while it's idle:
 * - Balanced: the WiFi's radio sleeps between beacons (not in a
 *   conversation), the CPU slows down between frames and when no one
 *   listens, the speaker's amplifier is off between sounds, the screen at
 *   most POWER_BALANCED_BRIGHTNESS, the eyes at half the frames asleep;
 * - Saver (setting `power_saver`): as Balanced, the screen dimmer, and
 *   asleep the mics are off (no wake word: PWR still talks), the radio
 *   wakes for every third beacon while the screen is dark;
 * - on either, with the screen dark the panel sleeps, the IMU watches only
 *   for face down and up, and the chip may light-sleep between its jobs;
 * - Hibernate (setting `hibernate`, minutes): dark that long with nothing
 *   waiting (no timer running, no errand), the WiFi stops and the chip
 *   light-sleeps until it's moved (the IMU's wake on motion), BOOT or PWR
 *   is pressed, or USB is plugged in.
 * behaviour.c tells it what Yoru is doing every tick. */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POWER_DESK,
    POWER_BALANCED,
    POWER_SAVER,
} power_profile_t;

/** What Yoru is doing, from behaviour.c's tick. */
typedef struct {
    bool asleep;
    /* The screen is dark (long asleep). */
    bool dark;
    /* A conversation: the radio keeps up and the mic stays on. */
    bool busy;
    /* Nothing would be missed while hibernating. */
    bool can_hibernate;
} power_scene_t;

/** Set up frequency scaling. Before the voice and the WiFi start. */
void power_init(void);

/** Every tick of behaviour.c, with the LVGL lock held. True when it
 * hibernated and is back: Yoru wakes up. */
bool power_tick(const power_scene_t *scene);

power_profile_t power_profile(void);
/** "desk", "balanced", "saver". */
const char *power_profile_name(power_profile_t profile);

/** The most the awake screen may glow, in percent. */
int power_brightness_cap(void);

#ifdef __cplusplus
}
#endif
