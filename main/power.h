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
 * behaviour.c tells it what Yoru is doing every tick.
 *
 * It also keeps a record of the last stretch on battery (0.20.3), saved in
 * the flash so a restart or a flat battery doesn't lose it: where the time
 * went, what kept it from hibernating, what woke it. */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POWER_DESK,
    POWER_BALANCED,
    POWER_SAVER,
} power_profile_t;

/** What keeps Yoru from hibernating, the first that applies. */
typedef enum {
    POWER_HOLD_NONE,
    POWER_HOLD_TIMER,
    POWER_HOLD_ERRAND,
    /* Listening, thinking or talking. */
    POWER_HOLD_VOICE,
    /* A card, the menu or a view is up. */
    POWER_HOLD_SCREEN,
    POWER_HOLD_COUNT,
} power_hold_t;

/** What Yoru is doing, from behaviour.c's tick. */
typedef struct {
    bool asleep;
    /* The screen is dark (long asleep). */
    bool dark;
    /* A conversation: the radio keeps up and the mic stays on. */
    bool busy;
    /* POWER_HOLD_NONE: nothing would be missed while hibernating. */
    power_hold_t hold;
} power_scene_t;

/** The last stretch on battery: from unplugging USB to plugging it in
 * again (then it stays as it was until the next unplug). Seconds. */
typedef struct {
    /* On battery now: the record is still growing. */
    bool running;
    uint32_t battery_s;
    /* Of battery_s: with the screen dark, and of that hibernating. */
    uint32_t dark_s;
    uint32_t hibernated_s;
    /* Of dark_s: kept from hibernating, by cause (index 0, hibernate set
     * to never). */
    uint32_t held_s[POWER_HOLD_COUNT];
    /* Of battery_s, not hibernating: the WiFi not joined. */
    uint32_t offline_s;
    uint16_t hibernations;
    uint16_t woke_moved;
    uint16_t woke_key;
    uint16_t woke_usb;
    /* The board started again in the middle of it. */
    uint16_t restarts;
    /* The IMU's pin was stuck high: only the keys woke it. */
    bool motion_wake_off;
    int8_t start_percent;
    int8_t percent;
    uint16_t mv;
} power_run_t;

/** Set up frequency scaling. Before the voice and the WiFi start. */
void power_init(void);

/** Every tick of behaviour.c, with the LVGL lock held. True when it
 * hibernated and is back: Yoru wakes up. */
bool power_tick(const power_scene_t *scene);

power_profile_t power_profile(void);
/** A copy of the record of the last stretch on battery. */
void power_run_get(power_run_t *out);

/** "timer", "errand", "voice", "screen" ("never" for POWER_HOLD_NONE). */
const char *power_hold_name(power_hold_t hold);

/** "desk", "balanced", "saver". */
const char *power_profile_name(power_profile_t profile);

/** The most the awake screen may glow, in percent. */
int power_brightness_cap(void);

#ifdef __cplusplus
}
#endif
