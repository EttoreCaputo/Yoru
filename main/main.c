#include <math.h>
#include <stdlib.h>

#include "behaviour.h"
#include "board_audio.h"
#include "board_display.h"
#include "board_settings.h"
#include "ears.h"
#include "board_touch.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "eyes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl_port.h"
#include "metrics_model.h"
#include "pet.h"

static const char *TAG = "yoru";

static board_lcd_t s_lcd;
static lv_display_t *s_display;

/* Two rising notes: Yoru's "hello". The pet's real sound language is M5. */
static void boot_chirp(void)
{
    const int rate = BOARD_AUDIO_SAMPLE_RATE;
    const int note = rate * 90 / 1000;
    const float freqs[2] = {988.0f, 1480.0f};
    int16_t *buf = malloc(sizeof(int16_t) * note * 2);
    if (buf == NULL) {
        return;
    }
    for (int n = 0; n < 2; n++) {
        for (int i = 0; i < note; i++) {
            float env = sinf((float) M_PI * (float) i / (float) note);
            buf[n * note + i] = (int16_t) (9000.0f * env * sinf(2 * (float) M_PI * freqs[n] * (float) i / (float) rate));
        }
    }
    board_audio_set_mute(false);
    board_audio_write(buf, note * 2);
    vTaskDelay(pdMS_TO_TICKS(250));
    board_audio_set_mute(true);
    free(buf);
}

static void audio_task(void *arg)
{
    if (board_audio_init() == ESP_OK) {
        board_audio_set_volume((int) board_settings_get(SETTING_VOLUME));
        if (board_settings_on(SETTING_PET_SOUNDS) && !board_settings_on(SETTING_FOCUS)) {
            boot_chirp();
        }
    } else {
        ESP_LOGE(TAG, "audio init failed, continuing without audio");
    }
    vTaskDelete(NULL);
}

/* The pet's mood moves once a second; touch and sounds nudge it in between.
 * Until the link (M3) the snapshot stays empty and only those count. */
static void pet_task(void *arg)
{
    metrics_snapshot_t snap;
    while (true) {
        metrics_model_get(&snap);
        pet_update(&snap);
        vTaskDelay(pdMS_TO_TICKS(200));
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
        ESP_LOGI(TAG, "lvgl %.1f%% busy, longest %lu us, eyes %s, pet %s", st.busy_pct, (unsigned long) st.max_us,
                 eyes, pet_mood_name(pet_mood()));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Yoru %s", esp_app_get_description()->version);
    board_settings_init();
    metrics_model_init();
    pet_init();
    pet_set_reactions(board_settings_on(SETTING_PET_REACTIONS));
    ears_set_on(board_settings_on(SETTING_PET_HEARING));

    ESP_ERROR_CHECK(board_display_init(&s_lcd));
    if (board_touch_init() != ESP_OK) {
        ESP_LOGE(TAG, "touch init failed, continuing without touch");
    }
    ESP_ERROR_CHECK(lvgl_port_init(&s_lcd, &s_display));

    lvgl_port_lock();
    lv_obj_t *face = eyes_create(lv_display_get_screen_active(s_display));
    lvgl_port_unlock();
    // Let the first black frame reach the panel before it lights up.
    vTaskDelay(pdMS_TO_TICKS(100));
    lvgl_port_lock();
    behaviour_start(face);
    lvgl_port_unlock();

    // Core 1 is for audio, as in DualEye.
    xTaskCreatePinnedToCore(audio_task, "audio_init", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(pet_task, "pet", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(stats_task, "stats", 3072, NULL, 2, NULL, 0);
    ESP_LOGI(TAG, "awake");
}
