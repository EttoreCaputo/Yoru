#pragma once

/* Yoru's settings, kept in NVS. They are a table, not a struct of fields,
 * because the round menu (M2) and the link (M3) both walk it: each setting
 * has a wire name, a range and a default. The setters check the range, save
 * and apply the few that need applying (the volume); the rest are read by
 * whoever uses them, when they need them. Safe from any task.
 * From DualEye's board_settings.c, without its two screens. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* New ones go at the end: the order is the menu's, the NVS key the name. */
typedef enum {
    /* Display. Percent; night hours 0..23. */
    SETTING_BRIGHTNESS,
    SETTING_NIGHT_BRIGHTNESS,
    SETTING_NIGHT_FROM,
    SETTING_NIGHT_TO,
    /* Asleep, the screen dims to almost black. */
    SETTING_AUTO_DIM,
    /* Quarter turns clockwise, 0..3. Not applied yet (M2 menu). */
    SETTING_ROTATION,
    /* Sound. Percent. */
    SETTING_VOLUME,
    SETTING_PET_SOUNDS,
    /* The privacy switch: the mic isn't read (M3). */
    SETTING_MIC_MUTED,
    /* Behaviour. Seconds without touch before sleepy and asleep. */
    SETTING_SLEEPY_AFTER_S,
    SETTING_ASLEEP_AFTER_S,
    /* Do not disturb: no whims, no reactions, no sounds. */
    SETTING_FOCUS,
    SETTING_TOUCH_REACTIONS,
    /* The IMU's tilt, shake and lift (M2). */
    SETTING_MOTION_REACTIONS,
    /* The pet reacts to what happens on the computer (pet.c). */
    SETTING_PET_REACTIONS,
    /* The pet listens to the room: bangs, claps, people talking (ears.c). */
    SETTING_PET_HEARING,
    /* Look: the resting eyes' colour as 0xRRGGBB, and the style (M5). */
    SETTING_EYE_COLOR,
    SETTING_EYE_STYLE,
    /* Voice: the wake word is listened for (M3). */
    SETTING_WAKE_WORD_ON,
    /* 0 English, 1 Italian. */
    SETTING_LANGUAGE,
    SETTING_COUNT,
} setting_t;

/** Open NVS and load the saved settings (defaults where none). */
void board_settings_init(void);

int32_t board_settings_get(setting_t id);
static inline bool board_settings_on(setting_t id)
{
    return board_settings_get(id) != 0;
}

/** Check the range, apply where needed, save. ESP_ERR_INVALID_ARG out of
 * range. */
esp_err_t board_settings_set(setting_t id, int32_t value);

/** Wire name (also the NVS key), or NULL. */
const char *board_settings_name(setting_t id);
bool board_settings_from_name(const char *name, setting_t *out);
void board_settings_range(setting_t id, int32_t *min, int32_t *max);

/** voice_wake_words() id (M3); empty for the default. */
void board_settings_wake_word(char *out, size_t len);
esp_err_t board_settings_set_wake_word(const char *id);

#ifdef __cplusplus
}
#endif
