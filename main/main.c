#include <string.h>

#include "agents.h"
#include "remote.h"
#include "audio_selftest.h"
#include "behaviour.h"
#include "board_audio.h"
#include "board_display.h"
#include "board_pmu.h"
#include "board_settings.h"
#include "board_touch.h"
#include "ears_dir.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "eyes.h"
#include "life.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "link.h"
#include "look.h"
#include "lvgl_port.h"
#include "metrics_io.h"
#include "metrics_model.h"
#include "motion.h"
#include "net.h"
#include "nvs.h"
#include "playback.h"
#include "rpc.h"
#include "voice.h"

static const char *TAG = "yoru";

static board_lcd_t s_lcd;
static lv_display_t *s_display;

/* The 80 MHz screen bus is a test, and it may leave the screen unreadable,
 * the menu with it. The way back without it: restart three times, each
 * within LCD_FAST_SAFE_MS of the last boot, and it's 40 MHz again. */
#define LCD_FAST_SAFE_MS 20000
#define LCD_FAST_TRIES 3
#define LCD_FAST_KEY "lcd_fast_boots"

static int32_t fast_boots(int32_t set)
{
    nvs_handle_t nvs;
    int32_t n = 0;
    if (nvs_open("yoru", NVS_READWRITE, &nvs) == ESP_OK) {
        if (set >= 0) {
            nvs_set_i32(nvs, LCD_FAST_KEY, set);
            nvs_commit(nvs);
            n = set;
        } else {
            nvs_get_i32(nvs, LCD_FAST_KEY, &n);
        }
        nvs_close(nvs);
    }
    return n;
}

static void fast_safe_cb(TimerHandle_t t)
{
    fast_boots(0); // it ran long enough: the screen works
}

/* Whether the screen's bus goes at 80 MHz this boot. */
static bool lcd_fast(void)
{
    if (!board_settings_on(SETTING_LCD_FAST)) {
        return false;
    }
    const int32_t n = fast_boots(-1) + 1;
    if (n >= LCD_FAST_TRIES) {
        ESP_LOGW(TAG, "restarted %d times quickly at 80 MHz: back to 40", (int) n);
        fast_boots(0);
        board_settings_set(SETTING_LCD_FAST, 0);
        return false;
    }
    fast_boots(n);
    xTimerStart(xTimerCreate("lcd_fast", pdMS_TO_TICKS(LCD_FAST_SAFE_MS), pdFALSE, NULL, fast_safe_cb), 0);
    return true;
}

static void restart_cb(TimerHandle_t t)
{
    esp_restart();
}

/* Focus mode silences the pet's sounds too; chimes still play. */
static void apply_pet_sounds(void)
{
    playback_set_pet_sounds(board_settings_on(SETTING_PET_SOUNDS) && !board_settings_on(SETTING_FOCUS));
}

/* Settings that other modules keep a copy of, from the menu or the host. */
static void on_setting(setting_t id, int32_t value)
{
    switch (id) {
    case SETTING_MIC_MUTED:
        voice_set_muted(value != 0);
        break;
    case SETTING_WAKE_SOUND:
        voice_set_wake_sound(value != 0);
        break;
    case SETTING_WAKE_WORD_ON:
        voice_set_wake_on(value != 0);
        break;
    case SETTING_PET_SOUNDS:
    case SETTING_FOCUS:
        apply_pet_sounds();
        break;
    case SETTING_LCD_FAST:
        if (s_display == NULL) {
            break; // turned off at boot by lcd_fast(), before the bus is up
        }
        // The bus is set up at boot. A moment first, so the switch shows.
        fast_boots(0);
        xTimerStart(xTimerCreate("restart", pdMS_TO_TICKS(400), pdFALSE, NULL, restart_cb), 0);
        break;
    case SETTING_COLOR_24:
        // The panel's format and the frame buffer are set up at boot too.
        xTimerStart(xTimerCreate("restart", pdMS_TO_TICKS(400), pdFALSE, NULL, restart_cb), 0);
        break;
    default:
        break;
    }
}

/* M1 measurement: how much of the CPU drawing the eyes takes. */
static void stats_task(void *arg)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(LVGL_PORT_STATS_WINDOW_MS));
        lvgl_port_stats_t st;
        lvgl_port_get_stats(&st);
        lvgl_port_lock();
        const char *eyes = eyes_mood_name(eyes_get_mood());
        lvgl_port_unlock();
        ESP_LOGI(TAG, "lvgl %.1f%% busy, longest %lu us, eyes %s, %s", st.busy_pct, (unsigned long) st.max_us,
                 eyes, life_activity());
    }
}

static void start_audio(void)
{
    if (board_audio_init() != ESP_OK) {
        ESP_LOGE(TAG, "audio init failed, continuing without audio");
        return;
    }
    board_audio_set_volume((int) board_settings_get(SETTING_VOLUME));
    audio_selftest_start();
    char word[16];
    board_settings_wake_word(word, sizeof(word));
    voice_set_wake_sound(board_settings_on(SETTING_WAKE_SOUND));
    voice_set_wake_on(board_settings_on(SETTING_WAKE_WORD_ON));
    ears_dir_on(behaviour_heard_from);
    voice_start(board_settings_on(SETTING_MIC_MUTED), word);
    playback_start();
    apply_pet_sounds();
    playback_sound(SOUND_BOOT);
}

void app_main(void)
{
    // First, so that everything logged from here on reaches the host in frames.
    ESP_ERROR_CHECK(link_init());
    ESP_LOGI(TAG, "Yoru %s", esp_app_get_description()->version);
    board_settings_init();
    board_settings_on_change(on_setting);
    metrics_model_init();
    agents_init();
    remote_init();

    const bool deep = board_settings_on(SETTING_COLOR_24);
    ESP_ERROR_CHECK(board_display_init(&s_lcd, lcd_fast(), deep));
    if (board_touch_init() != ESP_OK) {
        ESP_LOGE(TAG, "touch init failed, continuing without touch");
    }
    ESP_ERROR_CHECK(lvgl_port_init(&s_lcd, deep, &s_display));

    // The sphere and the hands, rendered once into PSRAM.
    ESP_ERROR_CHECK(look_init());
    lvgl_port_lock();
    lv_obj_t *face = eyes_create(lv_display_get_screen_active(s_display));
    lvgl_port_unlock();
    // Let the first black frame reach the panel before it lights up.
    vTaskDelay(pdMS_TO_TICKS(100));
    lvgl_port_lock();
    behaviour_start(face);
    lvgl_port_unlock();

    // The case and the power chip, on the same I2C bus as the touch.
    if (motion_init(behaviour_motion) != ESP_OK) {
        ESP_LOGW(TAG, "no IMU, continuing without motion");
    }
    if (board_pmu_init(behaviour_power_key) != ESP_OK) {
        ESP_LOGW(TAG, "no PMU, continuing without battery and PWR");
    }

    // The voice runs on core 1, as in DualEye; the UI on core 0.
    start_audio();
    // Its stack can live in PSRAM: it never writes the flash (0.3.1).
    xTaskCreatePinnedToCoreWithCaps(stats_task, "stats", 3072, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    rpc_init();
    link_on_receive(LINK_CHAN_CTRL, rpc_handle);
    link_on_receive(LINK_CHAN_METRICS, metrics_io_handle);
    link_on_receive(LINK_CHAN_AUDIO_DOWN, playback_receive);
    // Joins the WiFi if it's set up; nothing of it runs otherwise.
    net_init();
    link_start();
    // A host already listening (e.g. right after flashing) learns the board rebooted.
    rpc_announce();
    ESP_LOGI(TAG, "awake, waiting for the host");
}
