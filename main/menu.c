#include "menu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "board.h"
#include "board_pmu.h"
#include "board_settings.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link.h"
#include "lvgl_port.h"
#include "net.h"
#include "nvs_flash.h"
#include "voice.h"

static const char *TAG = "menu";

#define CX (BOARD_LCD_H_RES / 2)
#define CY (BOARD_LCD_V_RES / 2)
/* The ring of icons sits this far out, clear of Yoru and its hands. */
#define RING_R 176
#define ICON_D 66
/* Closes by itself after this long untouched: shorter on the ring, where
 * there is little to read. */
#define IDLE_RING_MS 6000
#define IDLE_PAGE_MS 15000
#define ROW_W 360
#define ROW_H 76
#define MAX_ROWS 12

#define COL_BG lv_color_hex(0x000000)
#define COL_ROW lv_color_hex(0x16171D)
#define COL_ROW_PRESSED lv_color_hex(0x262833)
#define COL_TEXT lv_color_hex(0xF2F2F5)
#define COL_DIM lv_color_hex(0x8C90A6)
#define COL_ACCENT lv_color_hex(0x7FA7FF)
#define COL_DANGER lv_color_hex(0xFF6B7A)

/* ---- What's in it ---------------------------------------------------------- */

typedef enum {
    K_SWITCH, // a setting, on or off: tap the row
    K_ARC,    // a range: tap for the arc
    K_CHOICE, // a few values: tap for the next
    K_WAKE,   // the wake word: tap for the next
    K_INFO,   // read only, refreshed every second
    K_TAP,    // an action on a tap
    K_HOLD,   // an action on a long press, for the ones that can't be undone
} kind_t;

typedef enum { F_NONE, F_PCT, F_HOUR, F_TIME, F_ROTATION, F_LANG } fmt_t;

typedef void (*info_fn_t)(char *out, size_t len);
typedef void (*action_fn_t)(void);

typedef struct {
    const char *en, *it;
    kind_t kind;
    setting_t setting;
    fmt_t fmt;
    info_fn_t info;
    action_fn_t action;
} item_t;

typedef struct {
    const char *en, *it;
    const char *icon;
    const item_t *items;
    int n;
} category_t;

/* The waits, as the arc offers them: a few round values beat 3590 steps. */
static const int32_t TIMES[] = {10, 20, 30, 45, 60, 90, 120, 180, 300, 600, 900, 1200, 1800, 3600, 7200};
#define TIMES_N ((int) (sizeof(TIMES) / sizeof(TIMES[0])))

static void info_battery(char *out, size_t len);
static void info_power(char *out, size_t len);
static void info_voltage(char *out, size_t len);
static void info_link(char *out, size_t len);
static void info_wifi(char *out, size_t len);
static void info_ip(char *out, size_t len);
static void info_paired(char *out, size_t len);
static void info_version(char *out, size_t len);
static void info_memory(char *out, size_t len);
static void info_flash(char *out, size_t len);
static void info_load(char *out, size_t len);
static void info_hold(char *out, size_t len);
static void act_power_off(void);
static void act_restart(void);
static void act_factory_reset(void);

#define SET(en, it, kind, id, fmt) {en, it, kind, id, fmt, NULL, NULL}
#define INFO(en, it, fn) {en, it, K_INFO, SETTING_COUNT, F_NONE, fn, NULL}
#define ACT(en, it, kind, fn, info) {en, it, kind, SETTING_COUNT, F_NONE, info, fn}

/* Italian without accents: the built-in fonts only have ASCII. */
static const item_t DISPLAY_ITEMS[] = {
    SET("Brightness", "Luce", K_ARC, SETTING_BRIGHTNESS, F_PCT),
    SET("Night brightness", "Luce di notte", K_ARC, SETTING_NIGHT_BRIGHTNESS, F_PCT),
    SET("Night from", "Notte dalle", K_ARC, SETTING_NIGHT_FROM, F_HOUR),
    SET("Night until", "Notte fino alle", K_ARC, SETTING_NIGHT_TO, F_HOUR),
    SET("Dim when asleep", "Attenua dormendo", K_SWITCH, SETTING_AUTO_DIM, F_NONE),
    SET("Rotation", "Rotazione", K_CHOICE, SETTING_ROTATION, F_ROTATION),
};
static const item_t SOUND_ITEMS[] = {
    SET("Volume", "Volume", K_ARC, SETTING_VOLUME, F_PCT),
    SET("Yoru's sounds", "Suoni di Yoru", K_SWITCH, SETTING_PET_SOUNDS, F_NONE),
    SET("Mic off", "Microfono spento", K_SWITCH, SETTING_MIC_MUTED, F_NONE),
    SET("Listening chime", "Suono d'ascolto", K_SWITCH, SETTING_WAKE_SOUND, F_NONE),
};
static const item_t BEHAVIOUR_ITEMS[] = {
    SET("Sleepy after", "Assonnato dopo", K_ARC, SETTING_SLEEPY_AFTER_S, F_TIME),
    SET("Asleep after", "Dorme dopo", K_ARC, SETTING_ASLEEP_AFTER_S, F_TIME),
    SET("Do not disturb", "Non disturbare", K_SWITCH, SETTING_FOCUS, F_NONE),
    SET("Touch reactions", "Reazioni al tocco", K_SWITCH, SETTING_TOUCH_REACTIONS, F_NONE),
    SET("Motion reactions", "Reazioni al moto", K_SWITCH, SETTING_MOTION_REACTIONS, F_NONE),
    SET("Computer reactions", "Reazioni al PC", K_SWITCH, SETTING_PET_REACTIONS, F_NONE),
    SET("Hears the room", "Ascolta la stanza", K_SWITCH, SETTING_PET_HEARING, F_NONE),
};
static const item_t VOICE_ITEMS[] = {
    SET("Wake word", "Parola d'attivazione", K_SWITCH, SETTING_WAKE_WORD_ON, F_NONE),
    {"Which word", "Quale parola", K_WAKE, SETTING_COUNT, F_NONE, NULL, NULL},
    SET("Language", "Lingua", K_CHOICE, SETTING_LANGUAGE, F_LANG),
};
static const item_t CONNECTION_ITEMS[] = {
    INFO("Host", "Computer", info_link),
    INFO("WiFi", "WiFi", info_wifi),
    INFO("Address", "Indirizzo", info_ip),
    INFO("Paired", "Associato", info_paired),
};
static const item_t BATTERY_ITEMS[] = {
    INFO("Charge", "Carica", info_battery),
    INFO("Power", "Alimentazione", info_power),
    INFO("Voltage", "Tensione", info_voltage),
    ACT("Power off", "Spegni", K_HOLD, act_power_off, info_hold),
};
static const item_t ABOUT_ITEMS[] = {
    INFO("Firmware", "Firmware", info_version),
    INFO("Free memory", "Memoria libera", info_memory),
    INFO("Flash", "Flash", info_flash),
    INFO("Screen load", "Carico schermo", info_load),
    ACT("Restart", "Riavvia", K_TAP, act_restart, NULL),
    ACT("Factory reset", "Ripristino", K_HOLD, act_factory_reset, info_hold),
};

#define CAT(en, it, icon, items) {en, it, icon, items, sizeof(items) / sizeof(items[0])}
static const category_t CATEGORIES[] = {
    CAT("Display", "Schermo", LV_SYMBOL_IMAGE, DISPLAY_ITEMS),
    CAT("Sound", "Suono", LV_SYMBOL_VOLUME_MAX, SOUND_ITEMS),
    CAT("Behaviour", "Carattere", LV_SYMBOL_EYE_OPEN, BEHAVIOUR_ITEMS),
    CAT("Voice", "Voce", LV_SYMBOL_AUDIO, VOICE_ITEMS),
    CAT("Connection", "Connessione", LV_SYMBOL_WIFI, CONNECTION_ITEMS),
    CAT("Battery", "Batteria", LV_SYMBOL_BATTERY_FULL, BATTERY_ITEMS),
    CAT("About", "Info", LV_SYMBOL_SETTINGS, ABOUT_ITEMS),
};
#define CAT_N ((int) (sizeof(CATEGORIES) / sizeof(CATEGORIES[0])))

/* ---- State ------------------------------------------------------------------ */

static struct {
    lv_obj_t *face;
    menu_cb_t cb;
    /* One of the three is up at a time; the ring stays under the others. */
    lv_obj_t *ring, *page, *editor;
    lv_obj_t *hint;
    int cat;
    const item_t *edit;
    lv_obj_t *edit_value;
    /* The rows whose value is shown, to refresh. */
    lv_obj_t *values[MAX_ROWS];
    const item_t *value_items[MAX_ROWS];
    int nvalues;
    lv_timer_t *timer;
    bool open;
} s;

static bool italian(void)
{
    return board_settings_get(SETTING_LANGUAGE) == 1;
}

static const char *tr(const char *en, const char *it)
{
    return italian() ? it : en;
}

/* ---- Values as text ----------------------------------------------------------- */

static void format(const item_t *it, int32_t v, char *out, size_t len)
{
    switch (it->fmt) {
    case F_PCT:
        snprintf(out, len, "%ld%%", (long) v);
        break;
    case F_HOUR:
        snprintf(out, len, "%02ld:00", (long) v);
        break;
    case F_TIME:
        if (v < 60) {
            snprintf(out, len, "%ld s", (long) v);
        } else if (v < 3600) {
            snprintf(out, len, v % 60 ? "%ld:%02ld min" : "%ld min", (long) (v / 60), (long) (v % 60));
        } else {
            snprintf(out, len, "%ld h", (long) (v / 3600));
        }
        break;
    case F_ROTATION:
        if (v >= 4) {
            snprintf(out, len, "%s", tr("Automatic", "Automatica"));
        } else {
            snprintf(out, len, "%ld°", (long) (v * 90));
        }
        break;
    case F_LANG:
        snprintf(out, len, "%s", v == 1 ? "Italiano" : "English");
        break;
    default:
        snprintf(out, len, "%ld", (long) v);
        break;
    }
}

static void row_value(const item_t *it, char *out, size_t len)
{
    out[0] = '\0';
    if (it->info != NULL) {
        it->info(out, len);
    } else if (it->kind == K_WAKE) {
        snprintf(out, len, "%s", voice_available() ? voice_wake_word() : tr("No voice", "Voce assente"));
    } else if (it->setting < SETTING_COUNT && it->kind != K_SWITCH) {
        format(it, board_settings_get(it->setting), out, len);
    }
}

/* ---- Info ------------------------------------------------------------------------ */

static void info_battery(char *out, size_t len)
{
    board_pmu_state_t st;
    board_pmu_get(&st);
    if (!board_pmu_present()) {
        snprintf(out, len, "%s", tr("Unknown", "Sconosciuta"));
    } else if (!st.battery) {
        snprintf(out, len, "%s", tr("No battery", "Nessuna batteria"));
    } else if (st.percent < 0) {
        snprintf(out, len, "...");
    } else {
        snprintf(out, len, "%d%%", st.percent);
    }
}

static void info_power(char *out, size_t len)
{
    board_pmu_state_t st;
    board_pmu_get(&st);
    if (st.charging) {
        snprintf(out, len, "%s", tr("USB, charging", "USB, in carica"));
    } else if (st.usb) {
        snprintf(out, len, "%s", "USB");
    } else {
        snprintf(out, len, "%s", tr("Battery", "Batteria"));
    }
}

static void info_voltage(char *out, size_t len)
{
    board_pmu_state_t st;
    board_pmu_get(&st);
    snprintf(out, len, "%d.%02d V", st.mv / 1000, (st.mv % 1000) / 10);
}

static void info_link(char *out, size_t len)
{
    snprintf(out, len, "%s", link_active() == LINK_WIFI ? "WiFi" : "USB");
}

/* net_status() as the host sees it; NULL fields come back empty. */
static void wifi_field(const char *key, char *out, size_t len)
{
    cJSON *st = net_status();
    const cJSON *v = cJSON_GetObjectItem(st, key);
    if (cJSON_IsString(v)) {
        snprintf(out, len, "%s", v->valuestring);
    } else if (cJSON_IsBool(v)) {
        snprintf(out, len, "%s", cJSON_IsTrue(v) ? tr("Yes", "Si") : "No");
    } else {
        out[0] = '\0';
    }
    cJSON_Delete(st);
}

static void info_wifi(char *out, size_t len)
{
    char state[24], ssid[34];
    wifi_field("state", state, sizeof(state));
    wifi_field("ssid", ssid, sizeof(ssid));
    if (ssid[0] == '\0') {
        snprintf(out, len, "%s", tr("Not set up", "Non configurato"));
    } else {
        snprintf(out, len, "%s, %s", ssid, state);
    }
}

static void info_ip(char *out, size_t len)
{
    wifi_field("ip", out, len);
    if (out[0] == '\0') {
        snprintf(out, len, "-");
    }
}

static void info_paired(char *out, size_t len)
{
    wifi_field("paired", out, len);
}

static void info_version(char *out, size_t len)
{
    snprintf(out, len, "%s", esp_app_get_description()->version);
}

static void info_memory(char *out, size_t len)
{
    snprintf(out, len, "%u KB + %u KB PSRAM", (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

static void info_flash(char *out, size_t len)
{
    uint32_t size = 0;
    esp_flash_get_size(NULL, &size);
    snprintf(out, len, "%u MB", (unsigned) (size / (1024 * 1024)));
}

static void info_load(char *out, size_t len)
{
    lvgl_port_stats_t st;
    lvgl_port_get_stats(&st);
    snprintf(out, len, "%.0f%%", st.busy_pct);
}

static void info_hold(char *out, size_t len)
{
    snprintf(out, len, "%s", tr("Hold to confirm", "Tieni premuto"));
}

/* ---- Actions ---------------------------------------------------------------------- */

static void restart_cb(lv_timer_t *t)
{
    esp_restart();
}

static void act_restart(void)
{
    // A moment, so the row's press shows.
    lv_timer_set_repeat_count(lv_timer_create(restart_cb, 300, NULL), 1);
}

static void act_power_off(void)
{
    board_pmu_power_off();
}

static void act_factory_reset(void)
{
    ESP_LOGW(TAG, "factory reset");
    // Settings, mood, WiFi and pairing all live in NVS.
    nvs_flash_erase();
    act_restart();
}

static void wake_word_task(void *arg)
{
    const char *id = arg;
    if (voice_set_wake_word(id) == ESP_OK) {
        board_settings_set_wake_word(voice_wake_word_id());
    }
    vTaskDelete(NULL);
}

/* The next wake word. Rebuilding the AFE takes a moment: not on the UI's task. */
static void next_wake_word(void)
{
    if (!voice_available()) {
        return;
    }
    const char *ids[8];
    const int n = voice_wake_words(ids, 8);
    const char *cur = voice_wake_word_id();
    int i = 0;
    while (i < n && strcmp(ids[i], cur) != 0) {
        i++;
    }
    if (n > 1) {
        xTaskCreate(wake_word_task, "wake_word", 4096, (void *) ids[(i + 1) % n], 3, NULL);
    }
}

/* ---- Building blocks ---------------------------------------------------------------- */

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *full_screen(bool opaque)
{
    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_center(o);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    if (opaque) {
        lv_obj_set_style_bg_color(o, COL_BG, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    }
    return o;
}

static void drop(lv_obj_t **o)
{
    if (*o != NULL) {
        lv_obj_delete_async(*o);
        *o = NULL;
    }
}

static void show_face(bool show)
{
    if (s.face == NULL) {
        return;
    }
    if (show) {
        lv_obj_remove_flag(s.face, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s.face, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_page(int cat);
static void build_ring(void);

/* ---- The arc ------------------------------------------------------------------------------ */

static int32_t time_index(int32_t v)
{
    int best = 0;
    for (int i = 0; i < TIMES_N; i++) {
        if (labs(TIMES[i] - v) < labs(TIMES[best] - v)) {
            best = i;
        }
    }
    return best;
}

static void arc_changed_cb(lv_event_t *e)
{
    lv_obj_t *arc = lv_event_get_user_data(e);
    const item_t *it = s.edit;
    int32_t v = lv_arc_get_value(arc);
    if (it->fmt == F_TIME) {
        int32_t lo, hi;
        board_settings_range(it->setting, &lo, &hi);
        v = LV_CLAMP(lo, TIMES[v], hi);
    }
    board_settings_set(it->setting, v);
    char text[24];
    format(it, v, text, sizeof(text));
    lv_label_set_text(s.edit_value, text);
}

static void editor_back_cb(lv_event_t *e)
{
    drop(&s.editor);
    build_page(s.cat);
}

static void build_editor(const item_t *it)
{
    s.edit = it;
    drop(&s.page);
    s.editor = full_screen(true);
    lv_obj_add_event_cb(s.editor, editor_back_cb, LV_EVENT_CLICKED, NULL);

    int32_t lo, hi;
    board_settings_range(it->setting, &lo, &hi);
    int32_t v = board_settings_get(it->setting);
    lv_obj_t *arc = lv_arc_create(s.editor);
    lv_obj_set_size(arc, BOARD_LCD_H_RES - 26, BOARD_LCD_V_RES - 26);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    if (it->fmt == F_TIME) {
        lv_arc_set_range(arc, 0, TIMES_N - 1);
        lv_arc_set_value(arc, time_index(v));
    } else {
        lv_arc_set_range(arc, lo, hi);
        lv_arc_set_value(arc, v);
    }
    lv_obj_set_style_arc_width(arc, 24, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x22232B), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 24, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(arc, COL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(arc, arc_changed_cb, LV_EVENT_VALUE_CHANGED, arc);

    lv_obj_t *name = label(s.editor, &lv_font_montserrat_24, COL_DIM, tr(it->en, it->it));
    lv_obj_align(name, LV_ALIGN_CENTER, 0, -64);
    char text[24];
    format(it, v, text, sizeof(text));
    s.edit_value = label(s.editor, &lv_font_montserrat_40, COL_TEXT, text);
    lv_obj_center(s.edit_value);
    lv_obj_t *hint = label(s.editor, &lv_font_montserrat_20, COL_DIM, tr("Tap to go back", "Tocca per tornare"));
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 64);
}

/* ---- A page of rows ------------------------------------------------------------------------ */

/* Rows fade out as they near the rim, like a list on a dial. */
static void list_scroll_cb(lv_event_t *e)
{
    lv_obj_t *list = lv_event_get_user_data(e);
    const uint32_t n = lv_obj_get_child_count(list);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_get_child(list, (int32_t) i);
        lv_area_t a;
        lv_obj_get_coords(row, &a);
        const int32_t d = LV_ABS((a.y1 + a.y2) / 2 - CY);
        const int32_t opa = d < 110 ? 255 : LV_MAX(0, 255 - (d - 110) * 255 / 110);
        lv_obj_set_style_opa(row, (lv_opa_t) opa, 0);
    }
}

static void refresh_values(void)
{
    for (int i = 0; i < s.nvalues; i++) {
        char text[48];
        row_value(s.value_items[i], text, sizeof(text));
        if (strcmp(lv_label_get_text(s.values[i]), text) != 0) {
            lv_label_set_text(s.values[i], text);
        }
    }
}

static void row_cb(lv_event_t *e)
{
    const item_t *it = lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *row = lv_event_get_current_target(e);
    if (code == LV_EVENT_LONG_PRESSED) {
        if (it->kind == K_HOLD && it->action != NULL) {
            it->action();
        }
        return;
    }
    switch (it->kind) {
    case K_SWITCH: {
        const bool on = !board_settings_on(it->setting);
        board_settings_set(it->setting, on);
        lv_obj_t *sw = lv_obj_get_child(row, -1);
        if (on) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
        }
        break;
    }
    case K_ARC:
        build_editor(it);
        break;
    case K_CHOICE: {
        int32_t lo, hi;
        board_settings_range(it->setting, &lo, &hi);
        int32_t v = board_settings_get(it->setting) + 1;
        board_settings_set(it->setting, v > hi ? lo : v);
        if (it->setting == SETTING_LANGUAGE) {
            build_page(s.cat); // every label changes
            return;
        }
        refresh_values();
        break;
    }
    case K_WAKE:
        next_wake_word();
        break;
    case K_TAP:
        if (it->action != NULL) {
            it->action();
        }
        break;
    default:
        break;
    }
}

static lv_obj_t *add_row(lv_obj_t *list, const item_t *it)
{
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, ROW_W, ROW_H);
    lv_obj_set_style_radius(row, ROW_H / 2, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, COL_ROW, 0);
    lv_obj_set_style_bg_color(row, COL_ROW_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(row, 30, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    if (it->kind != K_INFO) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, row_cb, it->kind == K_HOLD ? LV_EVENT_LONG_PRESSED : LV_EVENT_CLICKED, (void *) it);
    }

    const bool danger = it->kind == K_HOLD;
    lv_obj_t *name = label(row, &lv_font_montserrat_24, danger ? COL_DANGER : COL_TEXT, tr(it->en, it->it));
    if (it->kind == K_SWITCH) {
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, ROW_W - 60 - 70);
        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_set_size(sw, 58, 32);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_remove_flag(sw, LV_OBJ_FLAG_CLICKABLE); // the row is the switch
        lv_obj_set_style_bg_color(sw, COL_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (board_settings_on(it->setting)) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        return row;
    }
    char text[48];
    row_value(it, text, sizeof(text));
    if (text[0] == '\0') {
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
        return row;
    }
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 10);
    lv_obj_t *value = label(row, &lv_font_montserrat_20, it->kind == K_INFO ? COL_DIM : COL_ACCENT, text);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_width(value, ROW_W - 60);
    lv_obj_align(value, LV_ALIGN_BOTTOM_LEFT, 0, -10);
    if (s.nvalues < MAX_ROWS) {
        s.values[s.nvalues] = value;
        s.value_items[s.nvalues] = it;
        s.nvalues++;
    }
    return row;
}

static void page_back_cb(lv_event_t *e)
{
    drop(&s.page);
    s.nvalues = 0;
    build_ring();
}

static void build_page(int cat)
{
    const category_t *c = &CATEGORIES[cat];
    s.cat = cat;
    s.nvalues = 0;
    drop(&s.page);
    drop(&s.ring);
    show_face(false);
    s.page = full_screen(true);

    // The title is the way back.
    char title[40];
    snprintf(title, sizeof(title), LV_SYMBOL_LEFT "  %s", tr(c->en, c->it));
    lv_obj_t *back = label(s.page, &lv_font_montserrat_28, COL_ACCENT, title);
    lv_obj_align(back, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(back, 24);
    lv_obj_add_event_cb(back, page_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *list = lv_obj_create(s.page);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, BOARD_LCD_H_RES, BOARD_LCD_V_RES - 86);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 10, 0);
    // Room above and below, so the first and the last row reach the middle.
    lv_obj_set_style_pad_top(list, 50, 0);
    lv_obj_set_style_pad_bottom(list, 170, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(list, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(list, list_scroll_cb, LV_EVENT_SCROLL, list);
    for (int i = 0; i < c->n; i++) {
        add_row(list, &c->items[i]);
    }
    lv_obj_update_layout(list);
    lv_obj_send_event(list, LV_EVENT_SCROLL, NULL);
}

/* ---- The ring ------------------------------------------------------------------------------ */

static void icon_cb(lv_event_t *e)
{
    const int cat = (int) (intptr_t) lv_event_get_user_data(e);
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        lv_label_set_text(s.hint, tr(CATEGORIES[cat].en, CATEGORIES[cat].it));
        return;
    }
    build_page(cat);
}

static void ring_gesture_cb(lv_event_t *e)
{
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_TOP) {
        menu_close();
    }
}

static void build_ring(void)
{
    show_face(true);
    // See-through: Yoru stays in the middle, looking up at the icons.
    s.ring = full_screen(false);
    lv_obj_add_event_cb(s.ring, ring_gesture_cb, LV_EVENT_GESTURE, NULL);
    for (int i = 0; i < CAT_N; i++) {
        const float a = (-90.0f + 360.0f * (float) i / CAT_N) * 3.14159265f / 180.0f;
        lv_obj_t *b = lv_obj_create(s.ring);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, ICON_D, ICON_D);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, COL_ROW, 0);
        lv_obj_set_style_bg_color(b, COL_ROW_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(b, lv_color_hex(0x2C2E3A), 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_pos(b, CX + (int32_t) (RING_R * cosf(a)) - ICON_D / 2, CY + (int32_t) (RING_R * sinf(a)) - ICON_D / 2);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(b, icon_cb, LV_EVENT_CLICKED, (void *) (intptr_t) i);
        lv_obj_add_event_cb(b, icon_cb, LV_EVENT_PRESSED, (void *) (intptr_t) i);
        lv_obj_t *icon = label(b, &lv_font_montserrat_28, COL_TEXT, CATEGORIES[i].icon);
        lv_obj_center(icon);
    }
    s.hint = label(s.ring, &lv_font_montserrat_20, COL_DIM, tr("Swipe up to close", "Scorri su per chiudere"));
    lv_obj_align(s.hint, LV_ALIGN_CENTER, 0, 128);
}

/* ---- Open and close -------------------------------------------------------------------------- */

static void idle_cb(lv_timer_t *t)
{
    const uint32_t idle = lv_display_get_inactive_time(NULL);
    if (idle >= (s.ring != NULL ? IDLE_RING_MS : IDLE_PAGE_MS)) {
        menu_close();
        return;
    }
    if (s.page != NULL) {
        refresh_values();
    }
}

void menu_init(lv_obj_t *face, menu_cb_t cb)
{
    s.face = face;
    s.cb = cb;
}

void menu_open(void)
{
    if (s.open) {
        return;
    }
    s.open = true;
    lv_display_trigger_activity(NULL);
    build_ring();
    lv_obj_fade_in(s.ring, 180, 0);
    s.timer = lv_timer_create(idle_cb, 1000, NULL);
    ESP_LOGI(TAG, "open");
    if (s.cb != NULL) {
        s.cb(true);
    }
}

void menu_close(void)
{
    if (!s.open) {
        return;
    }
    s.open = false;
    drop(&s.ring);
    drop(&s.page);
    drop(&s.editor);
    s.nvalues = 0;
    show_face(true);
    if (s.timer != NULL) {
        lv_timer_delete(s.timer);
        s.timer = NULL;
    }
    ESP_LOGI(TAG, "closed");
    if (s.cb != NULL) {
        s.cb(false);
    }
}

bool menu_open_page(const char *name)
{
    for (int i = 0; i < CAT_N; i++) {
        if (strcasecmp(name, CATEGORIES[i].en) == 0) {
            menu_open();
            build_page(i);
            return true;
        }
    }
    return false;
}

bool menu_is_open(void)
{
    return s.open;
}
