#include "board_settings.h"

#include <string.h>

#include "board_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NAMESPACE "yoru"
#define WAKE_WORD_KEY "wake_word"

static const char *TAG = "settings";

typedef struct {
    /* Wire name and NVS key: 15 characters at most. */
    const char *name;
    int32_t min, max, def;
} setting_info_t;

static const setting_info_t INFO[SETTING_COUNT] = {
    // Below 5% the screen looks off, and the menu would be hard to find.
    [SETTING_BRIGHTNESS] = {"brightness", 5, 100, 80},
    [SETTING_NIGHT_BRIGHTNESS] = {"night_bright", 5, 100, 20},
    [SETTING_NIGHT_FROM] = {"night_from", 0, 23, 23},
    [SETTING_NIGHT_TO] = {"night_to", 0, 23, 7},
    [SETTING_AUTO_DIM] = {"auto_dim", 0, 1, 1},
    [SETTING_ROTATION] = {"rotation", 0, 4, 0},
    [SETTING_VOLUME] = {"volume", 0, 100, 60},
    [SETTING_PET_SOUNDS] = {"pet_sounds", 0, 1, 1},
    [SETTING_MIC_MUTED] = {"mic_muted", 0, 1, 0},
    [SETTING_SLEEPY_AFTER_S] = {"sleepy_after", 10, 3600, 300},
    [SETTING_ASLEEP_AFTER_S] = {"asleep_after", 20, 7200, 600},
    [SETTING_FOCUS] = {"focus", 0, 1, 0},
    [SETTING_TOUCH_REACTIONS] = {"touch_react", 0, 1, 1},
    [SETTING_MOTION_REACTIONS] = {"motion_react", 0, 1, 1},
    [SETTING_PET_HEARING] = {"pet_hear", 0, 1, 1},
    [SETTING_EYE_COLOR] = {"eye_color", 0, 0xFFFFFF, 0x3EE6FF},
    [SETTING_EYE_STYLE] = {"eye_style", 0, 15, 0},
    [SETTING_WAKE_WORD_ON] = {"wake_on", 0, 1, 1},
    [SETTING_LANGUAGE] = {"language", 0, 1, 0},
    [SETTING_WAKE_SOUND] = {"wake_sound", 0, 1, 1},
    [SETTING_LCD_FAST] = {"lcd_80mhz", 0, 1, 0},
    [SETTING_BOOT_KEY] = {"boot_key", 0, 1, 1},
    [SETTING_COLOR_24] = {"color_24bit", 0, 1, 0},
    [SETTING_POWER_SAVER] = {"power_saver", 0, 1, 0},
    [SETTING_HIBERNATE_MIN] = {"hibernate", 0, 240, 30},
};

static SemaphoreHandle_t s_lock;
static int32_t s_values[SETTING_COUNT];
static char s_wake_word[16];
static bool s_nvs_ok;
static board_settings_cb_t s_on_change;

static void load(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return; // nothing saved yet
    }
    for (int i = 0; i < SETTING_COUNT; i++) {
        int32_t v;
        if (nvs_get_i32(nvs, INFO[i].name, &v) == ESP_OK && v >= INFO[i].min && v <= INFO[i].max) {
            s_values[i] = v;
        }
    }
    size_t len = sizeof(s_wake_word);
    if (nvs_get_str(nvs, WAKE_WORD_KEY, s_wake_word, &len) != ESP_OK) {
        s_wake_word[0] = '\0';
    }
    nvs_close(nvs);
}

/* `str` NULL: save `value` as an i32. */
static void save(const char *key, int32_t value, const char *str)
{
    if (!s_nvs_ok) {
        return;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = str != NULL ? nvs_set_str(nvs, key, str) : nvs_set_i32(nvs, key, value);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "saving %s failed: %s", key, esp_err_to_name(err));
    }
}

void board_settings_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    for (int i = 0; i < SETTING_COUNT; i++) {
        s_values[i] = INFO[i].def;
    }
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable (%s), settings won't survive a reboot", esp_err_to_name(err));
        return;
    }
    s_nvs_ok = true;
    load();
}

int32_t board_settings_get(setting_t id)
{
    if (id >= SETTING_COUNT) {
        return 0;
    }
    if (s_lock == NULL) {
        return INFO[id].def;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int32_t v = s_values[id];
    xSemaphoreGive(s_lock);
    return v;
}

/* The settings the board applies itself; the others are read when used. */
static esp_err_t apply(setting_t id, int32_t value)
{
    switch (id) {
    case SETTING_VOLUME:
        return board_audio_set_volume((int) value);
    default:
        return ESP_OK;
    }
}

esp_err_t board_settings_set(setting_t id, int32_t value)
{
    if (id >= SETTING_COUNT || value < INFO[id].min || value > INFO[id].max) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = apply(id, value);
    if (err != ESP_OK) {
        return err;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = s_values[id] != value;
    s_values[id] = value;
    xSemaphoreGive(s_lock);
    if (changed) {
        save(INFO[id].name, value, NULL);
        if (s_on_change != NULL) {
            s_on_change(id, value);
        }
    }
    return ESP_OK;
}

void board_settings_on_change(board_settings_cb_t cb)
{
    s_on_change = cb;
}

const char *board_settings_name(setting_t id)
{
    return id < SETTING_COUNT ? INFO[id].name : NULL;
}

bool board_settings_from_name(const char *name, setting_t *out)
{
    for (int i = 0; i < SETTING_COUNT; i++) {
        if (strcmp(name, INFO[i].name) == 0) {
            *out = (setting_t) i;
            return true;
        }
    }
    return false;
}

void board_settings_range(setting_t id, int32_t *min, int32_t *max)
{
    *min = id < SETTING_COUNT ? INFO[id].min : 0;
    *max = id < SETTING_COUNT ? INFO[id].max : 0;
}

void board_settings_wake_word(char *out, size_t len)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(out, s_wake_word, len);
    xSemaphoreGive(s_lock);
}

esp_err_t board_settings_set_wake_word(const char *id)
{
    if (strlen(id) >= sizeof(s_wake_word)) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = strcmp(s_wake_word, id) != 0;
    strlcpy(s_wake_word, id, sizeof(s_wake_word));
    xSemaphoreGive(s_lock);
    if (changed) {
        save(WAKE_WORD_KEY, 0, id);
    }
    return ESP_OK;
}
