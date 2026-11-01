#pragma once

/* What the battery means for Yoru: the moments worth a reaction (the cable
 * plugged in or pulled out, fully charged, running low) out of the PMU's
 * readings, and what the eyes show meanwhile (the charge ring, the low
 * battery). behaviour.c decides how Yoru reacts. With the LVGL lock held. */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BATTERY_NOTHING,
    BATTERY_PLUGGED,
    BATTERY_UNPLUGGED,
    BATTERY_FULL,
    /* On battery, down to BATTERY_LOW_PCT, then BATTERY_CRITICAL_PCT. Once
     * each, until it has been charged a little. */
    BATTERY_LOW,
    BATTERY_CRITICAL,
} battery_event_t;

#define BATTERY_LOW_PCT 20
#define BATTERY_CRITICAL_PCT 8

/** Every tick of behaviour.c: what happened since the last one (one at a
 * time), and the eyes' charge ring or low battery set to match. */
battery_event_t battery_step(void);

/** The charge, 0..100, or -1 without a battery or before the gauge knows. */
int battery_percent(void);

/** On battery and low: Yoru is tired. */
bool battery_low(void);

#ifdef __cplusplus
}
#endif
