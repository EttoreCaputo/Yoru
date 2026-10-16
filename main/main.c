#include <string.h>

#include "audio_selftest.h"
#include "behaviour.h"
#include "board_audio.h"
#include "board_display.h"
#include "board_settings.h"
#include "board_touch.h"
#include "ears.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "eyes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "link.h"
#include "look.h"
#include "lvgl_port.h"
#include "metrics_io.h"
#include "metrics_model.h"
#include "net.h"
#include "pet.h"
#include "playback.h"
#include "rpc.h"
#include "voice.h"

static const char *TAG = "yoru";

static board_lcd_t s_lcd;
static lv_display_t *s_display;

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
    case SETTING_PET_SOUNDS:
    case SETTING_FOCUS:
        apply_pet_sounds();
        break;
    case SETTING_PET_REACTIONS:
        pet_set_reactions(value != 0);
        break;
    case SETTING_PET_HEARING:
        ears_set_on(value != 0);
        break;
    default:
        break;
    }
}

/* The pet's mood moves once a second on the host's snapshots; touch and
 * sounds nudge it in between. */
static void pet_task(void *arg)
{
    metrics_snapshot_t snap;
    metrics_ui_state_t was = METRICS_UI_WAITING;
    while (true) {
        metrics_model_get(&snap);
        pet_update(&snap);
        if (snap.state == METRICS_UI_LIVE && was != METRICS_UI_LIVE) {
            // The host is here (again): say hi.
            playback_sound(SOUND_HELLO);
        }
        was = snap.state;
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
    pet_init();
    pet_set_reactions(board_settings_on(SETTING_PET_REACTIONS));
    ears_set_on(board_settings_on(SETTING_PET_HEARING));

    ESP_ERROR_CHECK(board_display_init(&s_lcd));
    if (board_touch_init() != ESP_OK) {
        ESP_LOGE(TAG, "touch init failed, continuing without touch");
    }
    ESP_ERROR_CHECK(lvgl_port_init(&s_lcd, &s_display));

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

    // The voice runs on core 1, as in DualEye; the UI and the pet on core 0.
    start_audio();
    xTaskCreatePinnedToCore(pet_task, "pet", 4096, NULL, 3, NULL, 0);
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
