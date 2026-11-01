#include "battery.h"

#include "board_pmu.h"
#include "esp_log.h"
#include "eyes.h"
#include "lvgl.h"

static const char *TAG = "battery";

/* Nothing about running low while Yoru is still coming up: the boot is busy
 * enough. Plugged in and out count at once. */
#define BOOT_QUIET_MS 8000
/* Charged this much past a warning, it may come again. */
#define REARM_PCT 5
/* Charging stops a little short of 100 on some readings: that counts as full. */
#define FULL_PCT 95

static struct {
    bool started;
    bool usb;
    bool charging;
    bool told_low, told_critical;
    /* Full was said for this time on the cable (the chip tops up later). */
    bool told_full;
    int percent;
    bool low;
} s;

static eyes_battery_t eyes_state(const board_pmu_state_t *st)
{
    if (st->charging) {
        return EYES_BATTERY_CHARGING;
    }
    if (!st->battery || st->usb || st->percent < 0) {
        return EYES_BATTERY_NONE;
    }
    return st->percent <= BATTERY_CRITICAL_PCT ? EYES_BATTERY_EMPTY
           : st->percent <= BATTERY_LOW_PCT    ? EYES_BATTERY_LOW
                                               : EYES_BATTERY_NONE;
}

/* The warnings come once on the way down; charging past them again, or the
 * cable, lets them come another time. */
static battery_event_t check_low(const board_pmu_state_t *st)
{
    if (st->usb || st->percent > BATTERY_LOW_PCT + REARM_PCT) {
        s.told_low = false;
    }
    if (st->usb || st->percent > BATTERY_CRITICAL_PCT + REARM_PCT) {
        s.told_critical = false;
    }
    if (!st->battery || st->usb || st->percent < 0 || lv_tick_get() < BOOT_QUIET_MS) {
        return BATTERY_NOTHING;
    }
    if (st->percent <= BATTERY_CRITICAL_PCT && !s.told_critical) {
        s.told_critical = s.told_low = true;
        return BATTERY_CRITICAL;
    }
    if (st->percent <= BATTERY_LOW_PCT && !s.told_low) {
        s.told_low = true;
        return BATTERY_LOW;
    }
    return BATTERY_NOTHING;
}

battery_event_t battery_step(void)
{
    if (!board_pmu_present()) {
        return BATTERY_NOTHING;
    }
    board_pmu_state_t st;
    board_pmu_get(&st);
    s.percent = st.battery ? st.percent : -1;
    s.low = st.battery && !st.usb && st.percent >= 0 && st.percent <= BATTERY_LOW_PCT;
    eyes_set_battery(eyes_state(&st), st.percent);
    if (!s.started) {
        // How it booted is no news.
        s.started = true;
        s.usb = st.usb;
        s.charging = st.charging;
        s.told_full = !st.charging;
        return BATTERY_NOTHING;
    }
    // One at a time: the cable first, the rest on the next ticks.
    if (st.usb != s.usb) {
        s.usb = st.usb;
        ESP_LOGI(TAG, "%s, %d%%", st.usb ? "plugged in" : "unplugged", st.percent);
        if (!st.usb) {
            s.told_full = false;
        }
        // Without a battery the cable is the power: nothing to say.
        return !st.battery ? BATTERY_NOTHING : st.usb ? BATTERY_PLUGGED : BATTERY_UNPLUGGED;
    }
    if (st.charging != s.charging) {
        s.charging = st.charging;
        if (!st.charging && st.usb && st.battery && st.percent >= FULL_PCT && !s.told_full) {
            s.told_full = true;
            ESP_LOGI(TAG, "full");
            return BATTERY_FULL;
        }
    }
    const battery_event_t low = check_low(&st);
    if (low != BATTERY_NOTHING) {
        ESP_LOGI(TAG, "%s, %d%%", low == BATTERY_CRITICAL ? "critical" : "low", st.percent);
    }
    return low;
}

int battery_percent(void)
{
    return s.percent;
}

bool battery_low(void)
{
    return s.low;
}
