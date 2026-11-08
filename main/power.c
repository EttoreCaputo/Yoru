#include "power.h"

#include "board.h"
#include "board_audio.h"
#include "board_pmu.h"
#include "board_settings.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "eyes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "motion.h"
#include "net.h"
#include "nvs.h"
#include "voice.h"

#include <string.h>

static const char *TAG = "power";

#define MAX_MHZ 240
/* Not lower: the APB, and with it the screen's and the I2C's clocks, stays
 * at 80 MHz. */
#define MIN_MHZ 80
#define BALANCED_BRIGHTNESS 60
#define SAVER_BRIGHTNESS 40
/* Hibernating, the chip wakes this often to look at PWR and USB. */
#define HIBERNATE_POLL_MS 1000
/* The IMU's pin going high this soon after hibernating is its own settling,
 * or a pin that doesn't work: then only the keys wake it. */
#define WOM_SETTLE_US (1500 * 1000)
/* The record of the last stretch on battery goes to the flash this often
 * while it grows, and whenever hibernating starts or ends: a flat battery
 * loses at most this much of it. */
#define RUN_SAVE_EVERY_S 600
#define NVS_NAMESPACE "yoru"
#define RUN_KEY "pwr_run"

static struct {
    power_profile_t profile;
    bool started;
    /* Held except while the screen is dark on battery. */
    esp_pm_lock_handle_t awake;
    bool awake_held;
    eyes_pace_t pace;
    motion_mode_t motion;
    int64_t dark_since_us;
    /* The record, and what the time since the last tick goes to. */
    power_run_t run;
    portMUX_TYPE run_mux;
    bool run_checked;
    int64_t run_last_us;
    int64_t run_carry_us;
    uint32_t run_saved_s;
    bool was_dark;
    bool was_offline;
    int was_held; // a power_hold_t, or -1: not kept from hibernating
} s = {
    .profile = POWER_DESK,
    .pace = EYES_PACE_SMOOTH,
    .motion = MOTION_FULL,
    .run_mux = portMUX_INITIALIZER_UNLOCKED,
    .was_held = -1,
};

static const char *const NAMES[] = {"desk", "balanced", "saver"};
static const char *const HOLDS[] = {"never", "timer", "errand", "voice", "screen"};

static void run_load(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    power_run_t run;
    size_t len = sizeof(run);
    // Another size is a record from another version: start afresh.
    if (nvs_get_blob(nvs, RUN_KEY, &run, &len) == ESP_OK && len == sizeof(run)) {
        s.run = run;
    }
    nvs_close(nvs);
}

/* From the LVGL task, whose stack is internal: the flash may be written. */
static void run_save(void)
{
    power_run_t run;
    power_run_get(&run);
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, RUN_KEY, &run, sizeof(run));
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "saving the battery record failed: %s", esp_err_to_name(err));
    }
    s.run_saved_s = run.battery_s;
}

void power_run_get(power_run_t *out)
{
    portENTER_CRITICAL(&s.run_mux);
    *out = s.run;
    portEXIT_CRITICAL(&s.run_mux);
}

const char *power_hold_name(power_hold_t hold)
{
    return hold < POWER_HOLD_COUNT ? HOLDS[hold] : "?";
}

/* Unplugged: a new record, unless this is the same stretch after a restart.
 * Plugged in: the record stays as it was. */
static void run_follow(bool battery)
{
    board_pmu_state_t st;
    board_pmu_get(&st);
    const bool first = !s.run_checked;
    s.run_checked = true;
    if (battery == s.run.running && !(first && battery)) {
        return;
    }
    portENTER_CRITICAL(&s.run_mux);
    if (!battery) {
        s.run.running = false;
    } else if (first && s.run.running) {
        s.run.restarts++;
    } else {
        memset(&s.run, 0, sizeof(s.run));
        s.run.running = true;
        s.run.start_percent = (int8_t) st.percent;
    }
    portEXIT_CRITICAL(&s.run_mux);
    s.run_last_us = esp_timer_get_time();
    s.run_carry_us = 0;
    ESP_LOGI(TAG, "battery record: %s", !battery ? "ended" : s.run.restarts > 0 && first ? "goes on after a restart" : "new");
    run_save();
}

/* The time since the last tick, in whole seconds, to what Yoru was doing
 * then. The hours hibernating arrive here as one long tick. */
static void run_count(void)
{
    const int64_t now = esp_timer_get_time();
    s.run_carry_us += now - s.run_last_us;
    s.run_last_us = now;
    const uint32_t secs = (uint32_t) (s.run_carry_us / 1000000);
    if (secs == 0) {
        return;
    }
    s.run_carry_us -= (int64_t) secs * 1000000;
    board_pmu_state_t st;
    board_pmu_get(&st);
    portENTER_CRITICAL(&s.run_mux);
    s.run.battery_s += secs;
    if (s.was_dark) {
        s.run.dark_s += secs;
    }
    if (s.was_held >= 0) {
        s.run.held_s[s.was_held] += secs;
    }
    if (s.was_offline) {
        s.run.offline_s += secs;
    }
    if (st.battery) {
        s.run.percent = (int8_t) st.percent;
        s.run.mv = (uint16_t) st.mv;
    }
    portEXIT_CRITICAL(&s.run_mux);
    if (s.run.battery_s - s.run_saved_s >= RUN_SAVE_EVERY_S) {
        run_save();
    }
}

/* Light sleep lets go of every pin (CONFIG_PM_SLP_DISABLE_GPIO), and a
 * pin let go floats: the speaker's amplifier may switch on, the panel's and
 * the touch's resets drift. These keep their level instead. */
static void hold_pins_in_sleep(void)
{
    static const gpio_num_t pins[] = {
        BOARD_AUDIO_PA_GPIO, BOARD_LCD_RST_GPIO, BOARD_LCD_CS_GPIO, BOARD_TOUCH_RST_GPIO,
    };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_sleep_sel_dis(pins[i]);
    }
}

void power_init(void)
{
    run_load();
    hold_pins_in_sleep();
    if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "awake", &s.awake) == ESP_OK) {
        esp_pm_lock_acquire(s.awake);
        s.awake_held = true;
    }
    const esp_pm_config_t cfg = {.max_freq_mhz = MAX_MHZ, .min_freq_mhz = MAX_MHZ};
    esp_pm_configure(&cfg);
}

power_profile_t power_profile(void)
{
    return s.profile;
}

const char *power_profile_name(power_profile_t profile)
{
    return profile <= POWER_SAVER ? NAMES[profile] : "?";
}

int power_brightness_cap(void)
{
    switch (s.profile) {
    case POWER_BALANCED:
        return BALANCED_BRIGHTNESS;
    case POWER_SAVER:
        return SAVER_BRIGHTNESS;
    default:
        return 100;
    }
}

/* Without a battery, or with USB, there is nothing to save. */
static power_profile_t current_profile(void)
{
    board_pmu_state_t st;
    board_pmu_get(&st);
    if (!board_pmu_present() || !st.battery || st.usb) {
        return POWER_DESK;
    }
    return board_settings_on(SETTING_POWER_SAVER) ? POWER_SAVER : POWER_BALANCED;
}

static void apply_profile(power_profile_t profile)
{
    const bool desk = profile == POWER_DESK;
    const esp_pm_config_t cfg = {
        .max_freq_mhz = MAX_MHZ,
        .min_freq_mhz = desk ? MAX_MHZ : MIN_MHZ,
        .light_sleep_enable = !desk,
    };
    const esp_err_t err = esp_pm_configure(&cfg);
    board_audio_set_lazy(!desk);
    s.profile = profile;
    ESP_LOGI(TAG, "profile %s%s", NAMES[profile], err == ESP_OK ? "" : " (no frequency scaling)");
}

static void set_light_sleep(bool allowed)
{
    if (s.awake == NULL || allowed != s.awake_held) {
        return;
    }
    if (allowed) {
        esp_pm_lock_release(s.awake);
    } else {
        esp_pm_lock_acquire(s.awake);
    }
    s.awake_held = !allowed;
}

/* Light sleep until moved, BOOT, PWR or USB. The WiFi is off meanwhile, the
 * panel already asleep. Runs in the LVGL task, so nothing draws. */
static void hibernate(void)
{
    const int64_t start = esp_timer_get_time();
    ESP_LOGI(TAG, "hibernating");
    voice_set_resting(true);
    net_hibernate(true);
    bool wom = motion_set_mode(MOTION_WAKE_ON_MOTION) == ESP_OK;
    board_pmu_wake_check(); // a press from before doesn't count
    if (wom) {
        gpio_wakeup_enable(BOARD_IMU_INT1_GPIO, GPIO_INTR_HIGH_LEVEL);
    }
    gpio_wakeup_enable(BOARD_BOOT_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup((uint64_t) HIBERNATE_POLL_MS * 1000);

    portENTER_CRITICAL(&s.run_mux);
    s.run.hibernations++;
    portEXIT_CRITICAL(&s.run_mux);
    run_save();

    const char *why = NULL;
    uint16_t *woke = NULL;
    while (why == NULL) {
        esp_light_sleep_start();
        board_pmu_wake_t pmu;
        if (wom && gpio_get_level(BOARD_IMU_INT1_GPIO) == 1) {
            if (esp_timer_get_time() - start < WOM_SETTLE_US) {
                ESP_LOGW(TAG, "the IMU's pin is high already: waking on the keys only");
                gpio_wakeup_disable(BOARD_IMU_INT1_GPIO);
                wom = false;
                s.run.motion_wake_off = true;
            } else {
                why = "moved";
                woke = &s.run.woke_moved;
            }
        } else if (gpio_get_level(BOARD_BOOT_GPIO) == 0) {
            why = "BOOT";
            woke = &s.run.woke_key;
        } else if ((pmu = board_pmu_wake_check()) != BOARD_PMU_WAKE_NONE) {
            why = pmu == BOARD_PMU_WAKE_USB ? "USB" : "PWR";
            woke = pmu == BOARD_PMU_WAKE_USB ? &s.run.woke_usb : &s.run.woke_key;
        }
        // The idle task's turn, for the watchdog.
        vTaskDelay(1);
    }

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    gpio_wakeup_disable(BOARD_IMU_INT1_GPIO);
    gpio_wakeup_disable(BOARD_BOOT_GPIO);
    motion_set_mode(MOTION_FULL);
    s.motion = MOTION_FULL;
    net_hibernate(false);
    const int64_t slept_us = esp_timer_get_time() - start;
    portENTER_CRITICAL(&s.run_mux);
    s.run.hibernated_s += (uint32_t) (slept_us / 1000000);
    (*woke)++;
    portEXIT_CRITICAL(&s.run_mux);
    ESP_LOGI(TAG, "awake after %lld min hibernating (%s)", slept_us / 60000000, why);
}

static void set_pace(eyes_pace_t pace)
{
    if (pace != s.pace) {
        s.pace = pace;
        eyes_set_pace(pace);
    }
}

static void set_motion(motion_mode_t mode)
{
    if (mode != s.motion && motion_set_mode(mode) == ESP_OK) {
        s.motion = mode;
    }
}

bool power_tick(const power_scene_t *sc)
{
    const power_profile_t profile = current_profile();
    if (!s.started || profile != s.profile) {
        s.started = true;
        apply_profile(profile);
    }
    const bool battery = profile != POWER_DESK;
    const bool saver = profile == POWER_SAVER;
    run_follow(battery);
    if (battery) {
        run_count();
    }

    net_set_power_save(!battery || sc->busy ? WIFI_PS_NONE
                       : saver && sc->dark  ? WIFI_PS_MAX_MODEM
                                            : WIFI_PS_MIN_MODEM);
    voice_set_resting(saver && sc->asleep && !sc->busy);
    // 60 frames a second only on USB: on battery half of them is plenty.
    set_pace(sc->dark                   ? EYES_PACE_STOPPED
             : battery && sc->asleep    ? EYES_PACE_SLOW
             : battery                  ? EYES_PACE_FULL
                                        : EYES_PACE_SMOOTH);
    set_motion(battery && sc->dark ? MOTION_SLOW : MOTION_FULL);
    board_pmu_set_slow(sc->dark);
    set_light_sleep(battery && sc->dark);

    const int64_t now = esp_timer_get_time();
    const int32_t after_min = board_settings_get(SETTING_HIBERNATE_MIN);
    // What the time until the next tick goes to.
    s.was_dark = sc->dark;
    s.was_held = !sc->dark ? -1 : after_min <= 0 ? POWER_HOLD_NONE : sc->hold != POWER_HOLD_NONE ? (int) sc->hold : -1;
    s.was_offline = !net_online();
    if (!battery || !sc->dark || sc->hold != POWER_HOLD_NONE || after_min <= 0) {
        s.dark_since_us = 0;
        return false;
    }
    if (s.dark_since_us == 0) {
        s.dark_since_us = now;
    }
    if (now - s.dark_since_us < (int64_t) after_min * 60 * 1000000) {
        return false;
    }
    s.dark_since_us = 0;
    hibernate();
    // The hours just gone were dark, but the WiFi was off on purpose.
    s.was_offline = false;
    run_count();
    run_save();
    return true;
}
