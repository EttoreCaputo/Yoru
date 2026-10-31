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
#include "voice.h"

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

static struct {
    power_profile_t profile;
    bool started;
    /* Held except while the screen is dark on battery. */
    esp_pm_lock_handle_t awake;
    bool awake_held;
    eyes_pace_t pace;
    motion_mode_t motion;
    int64_t dark_since_us;
} s = {
    .profile = POWER_DESK,
    .pace = EYES_PACE_FULL,
    .motion = MOTION_FULL,
};

static const char *const NAMES[] = {"desk", "balanced", "saver"};

void power_init(void)
{
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

    const char *why = NULL;
    while (why == NULL) {
        esp_light_sleep_start();
        if (wom && gpio_get_level(BOARD_IMU_INT1_GPIO) == 1) {
            if (esp_timer_get_time() - start < WOM_SETTLE_US) {
                ESP_LOGW(TAG, "the IMU's pin is high already: waking on the keys only");
                gpio_wakeup_disable(BOARD_IMU_INT1_GPIO);
                wom = false;
            } else {
                why = "moved";
            }
        } else if (gpio_get_level(BOARD_BOOT_GPIO) == 0) {
            why = "BOOT";
        } else if (board_pmu_wake_check()) {
            why = "PWR or USB";
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
    ESP_LOGI(TAG, "awake after %lld min hibernating (%s)", (esp_timer_get_time() - start) / 60000000, why);
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

    net_set_power_save(!battery || sc->busy ? WIFI_PS_NONE
                       : saver && sc->dark  ? WIFI_PS_MAX_MODEM
                                            : WIFI_PS_MIN_MODEM);
    voice_set_resting(saver && sc->asleep && !sc->busy);
    set_pace(sc->dark ? EYES_PACE_STOPPED : battery && sc->asleep ? EYES_PACE_SLOW : EYES_PACE_FULL);
    set_motion(battery && sc->dark ? MOTION_SLOW : MOTION_FULL);
    board_pmu_set_slow(sc->dark);
    set_light_sleep(battery && sc->dark);

    const int64_t now = esp_timer_get_time();
    const int32_t after_min = board_settings_get(SETTING_HIBERNATE_MIN);
    if (!battery || !sc->dark || !sc->can_hibernate || after_min <= 0) {
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
    return true;
}
