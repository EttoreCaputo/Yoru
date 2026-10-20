#include "menu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "agents.h"
#include "agents_ui.h"
#include "board.h"
#include "board_pmu.h"
#include "board_settings.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "eyes.h"
#include "habits.h"
#include "habits_ui.h"
#include "inbox.h"
#include "inbox_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link.h"
#include "lvgl_port.h"
#include "net.h"
#include "nvs_flash.h"
#include "playback.h"
#include "timers.h"
#include "timers_ui.h"
#include "voice.h"

static const char *TAG = "menu";

#define CX (BOARD_LCD_H_RES / 2)
#define CY (BOARD_LCD_V_RES / 2)
/* The home: round apps in a honeycomb around Yoru, as on a watch. Six
 * around the middle, where Yoru is, each this big and this far out. */
#define HEX_R 152
#define ICON_D 112
#define BADGE_D 34
/* The apps fly out of Yoru one after the other. */
#define POP_MS 420
#define POP_STAGGER_MS 40
/* A tapped app grows from its place to the whole screen as it opens. */
#define ZOOM_MS 300
/* Closes by itself after this long untouched: shorter on the home, where
 * there is little to read. */
#define IDLE_HOME_MS 8000
#define IDLE_PAGE_MS 15000
#define IDLE_APP_MS 30000
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
    uint32_t rgb;
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
static void info_agents(char *out, size_t len);
static void info_quiet(char *out, size_t len);
static void act_agents(void);
static void act_quiet(void);
static void act_power_off(void);
static void act_restart(void);
static void act_factory_reset(void);

#define SET(en, it, kind, id, fmt) {en, it, kind, id, fmt, NULL, NULL}
#define INFO(en, it, fn) {en, it, K_INFO, SETTING_COUNT, F_NONE, fn, NULL}
#define ACT(en, it, kind, fn, info) {en, it, kind, SETTING_COUNT, F_NONE, info, fn}

/* Italian without accents: the built-in fonts only have ASCII. */
/* What the coding agents on the computer do, on Yoru (agents_ui.c). */
static const item_t AGENTS_ITEMS[] = {
    ACT("Open the agents", "Apri gli agenti", K_TAP, act_agents, info_agents),
    ACT("Quiet for 1 hour", "Silenzio per 1 ora", K_TAP, act_quiet, info_quiet),
    SET("Dots on the rim", "Puntini sul bordo", K_SWITCH, SETTING_AGENTS_RING, F_NONE),
    SET("Steps at the bottom", "Passi in basso", K_SWITCH, SETTING_AGENTS_TICKER, F_NONE),
    SET("Agent sounds", "Suoni degli agenti", K_SWITCH, SETTING_AGENTS_SOUNDS, F_NONE),
    SET("BOOT button", "Tasto BOOT", K_SWITCH, SETTING_BOOT_KEY, F_NONE),
};
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
    SET("Turns to sounds", "Si gira ai suoni", K_SWITCH, SETTING_PET_HEARING, F_NONE),
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
    SET("80 MHz bus (test)", "Bus 80 MHz (test)", K_SWITCH, SETTING_LCD_FAST, F_NONE),
    ACT("Restart", "Riavvia", K_TAP, act_restart, NULL),
    ACT("Factory reset", "Ripristino", K_HOLD, act_factory_reset, info_hold),
};

/* Under Settings, each with its round icon in its colour, as a watch's. */
#define CAT(en, it, icon, rgb, items) {en, it, icon, rgb, items, sizeof(items) / sizeof(items[0])}
static const category_t CATEGORIES[] = {
    CAT("Agents", "Agenti", LV_SYMBOL_KEYBOARD, 0x5E5CE6, AGENTS_ITEMS),
    CAT("Display", "Schermo", LV_SYMBOL_IMAGE, 0x0A84FF, DISPLAY_ITEMS),
    CAT("Sound", "Suono", LV_SYMBOL_VOLUME_MAX, 0xFF375F, SOUND_ITEMS),
    CAT("Behaviour", "Carattere", LV_SYMBOL_EYE_OPEN, 0xFF9F0A, BEHAVIOUR_ITEMS),
    CAT("Voice", "Voce", LV_SYMBOL_AUDIO, 0xFF453A, VOICE_ITEMS),
    CAT("Connection", "Connessione", LV_SYMBOL_WIFI, 0x30B0C7, CONNECTION_ITEMS),
    CAT("Battery", "Batteria", LV_SYMBOL_BATTERY_FULL, 0x30D158, BATTERY_ITEMS),
    CAT("About", "Info", LV_SYMBOL_SETTINGS, 0x8E8E93, ABOUT_ITEMS),
};
#define CAT_N ((int) (sizeof(CATEGORIES) / sizeof(CATEGORIES[0])))

/* ---- State ------------------------------------------------------------------ */

/* The apps on the home, clockwise from the top. */
typedef enum { APP_INBOX, APP_TIMERS, APP_HABITS, APP_SETTINGS, APP_FOCUS, APP_AGENTS, APP_COUNT } app_t;

typedef struct {
    const char *en, *it;
    /* The icon's gradient, top to bottom. */
    uint32_t top, bottom;
    const char *glyph;
} app_def_t;

static const app_def_t APPS[APP_COUNT] = {
    [APP_INBOX] = {"Notifications", "Notifiche", 0xFF6B6B, 0xE0262F, LV_SYMBOL_ENVELOPE},
    [APP_TIMERS] = {"Timers", "Timer", 0xFFB340, 0xF07800, NULL},
    [APP_HABITS] = {"Habits", "Abitudini", 0x2A2D36, 0x101114, NULL},
    [APP_SETTINGS] = {"Settings", "Impostazioni", 0x9A9AA2, 0x55555D, LV_SYMBOL_SETTINGS},
    [APP_FOCUS] = {"Do not disturb", "Non disturbare", 0x8E8CF8, 0x4B48D6, NULL},
    [APP_AGENTS] = {"Agents", "Agenti", 0x5AB0FF, 0x0A6CDF, LV_SYMBOL_KEYBOARD},
};

/* What each app's icon shows live: a badge, a ring, the moon. */
typedef struct {
    lv_obj_t *icon, *badge, *badge_text;
    lv_obj_t *arcs[3];
    lv_obj_t *glyph;
} app_icon_t;

static struct {
    lv_obj_t *face;
    menu_cb_t cb;
    /* One of these is up at a time: the home (see-through, Yoru in the
     * middle), Settings, a category's page, the arc, or an app's page. */
    lv_obj_t *home, *settings, *page, *editor, *app;
    lv_obj_t *hint;
    app_icon_t icons[APP_COUNT];
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

static void info_agents(char *out, size_t len)
{
    agents_summary_t sum;
    agents_summary(&sum);
    if (sum.n == 0) {
        snprintf(out, len, "%s", tr("None running", "Nessuno al lavoro"));
    } else if (sum.waiting > 0) {
        snprintf(out, len, tr("%d, %d waiting for you", "%d, %d ti aspettano"), sum.n, sum.waiting);
    } else {
        snprintf(out, len, tr("%d, %d working", "%d, %d al lavoro"), sum.n, sum.working);
    }
}

static void info_quiet(char *out, size_t len)
{
    agents_summary_t sum;
    agents_summary(&sum);
    if (sum.quiet_min > 0) {
        snprintf(out, len, tr("On, %d min left: tap to end", "Attivo, %d min: tocca per finire"), sum.quiet_min);
    } else {
        out[0] = '\0';
    }
}

static void act_agents(void)
{
    // The menu goes as the view comes (agents_ui_open closes it).
    agents_ui_open();
}

static void act_quiet(void)
{
    agents_ui_shortcut("quiet", NULL);
}

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
    // The swipes stop here: bubbled on, they'd reach the top layer, which
    // nobody listens to.
    lv_obj_remove_flag(o, LV_OBJ_FLAG_GESTURE_BUBBLE);
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
static void build_home(bool pop);
static void build_settings(void);

/* The direction of a swipe, once: the release after it is not a tap too. */
static lv_dir_t swipe_dir(void)
{
    lv_indev_t *indev = lv_indev_active();
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    lv_indev_wait_release(indev);
    return dir;
}

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
    if (lv_event_get_code(e) == LV_EVENT_GESTURE && swipe_dir() != LV_DIR_RIGHT) {
        return;
    }
    drop(&s.editor);
    build_page(s.cat);
}

static void build_editor(const item_t *it)
{
    s.edit = it;
    drop(&s.page);
    s.editor = full_screen(true);
    lv_obj_add_event_cb(s.editor, editor_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s.editor, editor_back_cb, LV_EVENT_GESTURE, NULL);

    int32_t lo, hi;
    board_settings_range(it->setting, &lo, &hi);
    int32_t v = board_settings_get(it->setting);
    lv_obj_t *arc = lv_arc_create(s.editor);
    lv_obj_set_size(arc, BOARD_LCD_H_RES - 26, BOARD_LCD_V_RES - 26);
    lv_obj_center(arc);
    // Only the ring is the arc: a tap inside it is the way back, not a jump
    // of the value. Dragging it is no swipe either.
    lv_obj_add_flag(arc, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_GESTURE_BUBBLE);
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
    lv_obj_t *hint = label(s.editor, &lv_font_montserrat_20, COL_DIM, tr("Tap here to go back", "Tocca qui per tornare"));
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

/* A column of rows under the title that scrolls, snapping a row to the
 * middle, its rows fading towards the rim. */
static lv_obj_t *round_list(lv_obj_t *parent)
{
    lv_obj_t *list = lv_obj_create(parent);
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
    return list;
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
    if (lv_event_get_code(e) == LV_EVENT_GESTURE && swipe_dir() != LV_DIR_RIGHT) {
        return;
    }
    drop(&s.page);
    s.nvalues = 0;
    build_settings();
}

static void build_page(int cat)
{
    const category_t *c = &CATEGORIES[cat];
    s.cat = cat;
    s.nvalues = 0;
    drop(&s.page);
    drop(&s.settings);
    drop(&s.home);
    show_face(false);
    s.page = full_screen(true);
    lv_obj_add_event_cb(s.page, page_back_cb, LV_EVENT_GESTURE, NULL);

    // The title is the way back.
    char title[40];
    snprintf(title, sizeof(title), LV_SYMBOL_LEFT "  %s", tr(c->en, c->it));
    lv_obj_t *back = label(s.page, &lv_font_montserrat_28, COL_ACCENT, title);
    lv_obj_align(back, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(back, 24);
    lv_obj_add_event_cb(back, page_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *list = round_list(s.page);
    for (int i = 0; i < c->n; i++) {
        add_row(list, &c->items[i]);
    }
    lv_obj_update_layout(list);
    lv_obj_send_event(list, LV_EVENT_SCROLL, NULL);
}

/* ---- Settings -------------------------------------------------------------------- */

static lv_obj_t *circle(lv_obj_t *parent, int32_t d, uint32_t rgb)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(rgb), 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void category_cb(lv_event_t *e)
{
    build_page((int) (intptr_t) lv_event_get_user_data(e));
}

static void settings_back_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_GESTURE && swipe_dir() != LV_DIR_RIGHT) {
        return;
    }
    drop(&s.settings);
    build_home(true);
}

/* The categories, each a row with its round icon, as a watch's Settings. */
static void build_settings(void)
{
    drop(&s.home);
    drop(&s.page);
    drop(&s.settings);
    s.nvalues = 0;
    show_face(false);
    s.settings = full_screen(true);
    lv_obj_add_event_cb(s.settings, settings_back_cb, LV_EVENT_GESTURE, NULL);

    char title[40];
    snprintf(title, sizeof(title), LV_SYMBOL_LEFT "  %s", tr("Settings", "Impostazioni"));
    lv_obj_t *back = label(s.settings, &lv_font_montserrat_28, COL_ACCENT, title);
    lv_obj_align(back, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(back, 24);
    lv_obj_add_event_cb(back, settings_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *list = round_list(s.settings);
    for (int i = 0; i < CAT_N; i++) {
        const category_t *c = &CATEGORIES[i];
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, ROW_W, ROW_H);
        lv_obj_set_style_radius(row, ROW_H / 2, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, COL_ROW, 0);
        lv_obj_set_style_bg_color(row, COL_ROW_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_pad_hor(row, 14, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_add_event_cb(row, category_cb, LV_EVENT_CLICKED, (void *) (intptr_t) i);
        lv_obj_t *dot = circle(row, 50, c->rgb);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_center(label(dot, &lv_font_montserrat_24, COL_TEXT, c->icon));
        lv_obj_t *name = label(row, &lv_font_montserrat_24, COL_TEXT, tr(c->en, c->it));
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 66, 0);
        lv_obj_t *more = label(row, &lv_font_montserrat_20, COL_DIM, LV_SYMBOL_RIGHT);
        lv_obj_align(more, LV_ALIGN_RIGHT_MID, -12, 0);
    }
    lv_obj_update_layout(list);
    lv_obj_send_event(list, LV_EVENT_SCROLL, NULL);
}

/* ---- The home ------------------------------------------------------------------- */

/* Where the i-th app sits: the top-left corner of its icon. */
static void icon_pos(int i, int32_t *x, int32_t *y)
{
    const float a = (-90.0f + 60.0f * (float) i) * 3.14159265f / 180.0f;
    *x = CX + (int32_t) lroundf(HEX_R * cosf(a)) - ICON_D / 2;
    *y = CY + (int32_t) lroundf(HEX_R * sinf(a)) - ICON_D / 2;
}

/* A ring that fills up, inside an icon (the timer's, the habits'). */
static lv_obj_t *ring_arc(lv_obj_t *parent, int32_t d, int32_t w, lv_color_t color, lv_color_t track)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, d, d);
    lv_obj_center(arc);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 1000);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_width(arc, w, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, track, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    return arc;
}

static void set_arc(lv_obj_t *arc, float k)
{
    const int32_t v = (int32_t) lroundf(LV_CLAMP(0.0f, k, 1.0f) * 1000);
    if (lv_arc_get_value(arc) != v) {
        lv_arc_set_value(arc, v);
    }
}

static void set_badge(app_icon_t *ic, int n, lv_color_t color)
{
    if (n <= 0) {
        lv_obj_add_flag(ic->badge, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char text[8];
    snprintf(text, sizeof(text), n > 9 ? "9+" : "%d", n);
    if (strcmp(lv_label_get_text(ic->badge_text), text) != 0) {
        lv_label_set_text(ic->badge_text, text);
    }
    lv_obj_set_style_bg_color(ic->badge, color, 0);
    lv_obj_remove_flag(ic->badge, LV_OBJ_FLAG_HIDDEN);
}

/* The moon: a pale disc with a bite out of it in the icon's own colour. */
static void paint_focus(app_icon_t *ic)
{
    const bool on = board_settings_on(SETTING_FOCUS);
    const lv_color_t bg = lv_color_hex(on ? 0x5E5CE6 : 0x2C2C34);
    lv_obj_set_style_bg_color(ic->icon, bg, 0);
    lv_obj_set_style_bg_grad_dir(ic->icon, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_color(ic->arcs[0], on ? COL_TEXT : lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_bg_color(ic->arcs[1], bg, 0);
}

/* What the icons say now: what is unread, waiting, running, done today. */
static void refresh_home(void)
{
    app_icon_t *ic = s.icons;
    set_badge(&ic[APP_INBOX], inbox_unread(NULL), lv_color_hex(0xFF3B30));

    agents_summary_t sum;
    agents_summary(&sum);
    set_badge(&ic[APP_AGENTS], sum.waiting, lv_color_hex(0xFF9F0A));

    timer_view_t tv;
    const int nt = timers_count();
    const bool timer = timers_at(0, &tv);
    set_arc(ic[APP_TIMERS].arcs[0], timer ? tv.left_s / tv.total_s : 0);
    set_badge(&ic[APP_TIMERS], nt > 1 ? nt : 0, lv_color_hex(0xFF3B30));

    for (int i = 0; i < 3; i++) {
        const habit_t *h = habits_at(i);
        set_arc(ic[APP_HABITS].arcs[i], h != NULL ? (float) h->done / (float) h->goal : 0);
    }
    paint_focus(&ic[APP_FOCUS]);
}

static void pop_cb(void *var, int32_t v)
{
    lv_obj_t *o = var;
    int32_t x, y;
    icon_pos((int) (intptr_t) lv_obj_get_user_data(o), &x, &y);
    const int32_t x0 = CX - ICON_D / 2, y0 = CY - ICON_D / 2;
    lv_obj_set_pos(o, x0 + (x - x0) * v / 1024, y0 + (y - y0) * v / 1024);
}

static void pop_start_cb(lv_anim_t *a)
{
    lv_obj_remove_flag(a->var, LV_OBJ_FLAG_HIDDEN);
}

/* Out of Yoru, one after the other, with a little bounce. */
static void pop_out(lv_obj_t *o, int i)
{
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    pop_cb(o, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, pop_cb);
    lv_anim_set_values(&a, 0, 1024);
    lv_anim_set_duration(&a, POP_MS);
    lv_anim_set_delay(&a, (uint32_t) i * POP_STAGGER_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_set_start_cb(&a, pop_start_cb);
    lv_anim_start(&a);
}

/* ---- Opening an app: it grows from its icon to the whole screen ---------- */

static struct {
    lv_obj_t *obj;
    int32_t x, y; // where it starts, its centre
} z;

static void zoom_cb(void *var, int32_t v)
{
    const int32_t full = BOARD_LCD_H_RES * 3 / 2; // past the corners
    const int32_t d = ICON_D + (full - ICON_D) * v / 1024;
    const int32_t x = z.x + (CX - z.x) * v / 1024, y = z.y + (CY - z.y) * v / 1024;
    lv_obj_set_size(var, d, d);
    lv_obj_set_pos(var, x - d / 2, y - d / 2);
    // Solid as it grows, then it melts into the page under it.
    lv_obj_set_style_bg_opa(var, (lv_opa_t) (v < 400 ? 255 : 255 - (v - 400) * 255 / 624), 0);
}

static void zoom_done_cb(lv_anim_t *a)
{
    lv_obj_delete(a->var);
    if (z.obj == a->var) {
        z.obj = NULL;
    }
}

static void zoom(int32_t x, int32_t y, uint32_t rgb)
{
    if (z.obj != NULL) {
        lv_anim_delete(z.obj, zoom_cb);
        lv_obj_delete(z.obj);
    }
    z.x = x;
    z.y = y;
    z.obj = circle(lv_layer_top(), ICON_D, rgb);
    zoom_cb(z.obj, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, z.obj);
    lv_anim_set_exec_cb(&a, zoom_cb);
    lv_anim_set_values(&a, 0, 1024);
    lv_anim_set_duration(&a, ZOOM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, zoom_done_cb);
    lv_anim_start(&a);
}

static void app_back(void)
{
    drop(&s.app);
    build_home(true);
}

static void open_app(lv_obj_t *(*build)(void (*back)(void)))
{
    drop(&s.home);
    show_face(false);
    s.app = build(app_back);
}

static void toggle_focus(void)
{
    const bool on = !board_settings_on(SETTING_FOCUS);
    board_settings_set(SETTING_FOCUS, on);
    playback_sound(on ? SOUND_YAWN : SOUND_CHIRP);
    paint_focus(&s.icons[APP_FOCUS]);
    lv_label_set_text(s.hint, on ? tr("Shh... do not disturb", "Shh... non disturbare")
                                 : tr("Back to normal", "Si torna a parlare"));
    // A little bounce, so the tap is felt.
    pop_out(s.icons[APP_FOCUS].icon, 0);
}

static void launch(app_t app)
{
    if (app == APP_FOCUS) {
        toggle_focus();
        return;
    }
    int32_t x, y;
    icon_pos(app, &x, &y);
    const uint32_t rgb = app == APP_HABITS ? 0x30D158 : APPS[app].bottom;
    switch (app) {
    case APP_INBOX:
        inbox_ui_open(NULL); // the menu goes as the view comes
        break;
    case APP_AGENTS:
        agents_ui_open();
        break;
    case APP_SETTINGS:
        build_settings();
        break;
    case APP_TIMERS:
        open_app(timers_ui_build);
        break;
    case APP_HABITS:
        open_app(habits_ui_build);
        break;
    default:
        return;
    }
    zoom(x + ICON_D / 2, y + ICON_D / 2, rgb);
}

static void app_cb(lv_event_t *e)
{
    const app_t app = (app_t) (intptr_t) lv_event_get_user_data(e);
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        lv_label_set_text(s.hint, tr(APPS[app].en, APPS[app].it));
        // Yoru looks at the one under your finger.
        int32_t x, y;
        icon_pos(app, &x, &y);
        eyes_look_at((float) (x + ICON_D / 2 - CX) / HEX_R, (float) (y + ICON_D / 2 - CY) / HEX_R);
        return;
    }
    launch(app);
}

static void home_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
        if (swipe_dir() == LV_DIR_TOP) {
            menu_close();
        }
        return;
    }
    // A tap on Yoru in the middle: back to it.
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (LV_ABS(p.x - CX) < 70 && LV_ABS(p.y - CY) < 70) {
        menu_close();
    }
}

static void add_badge(lv_obj_t *icon, app_icon_t *ic)
{
    ic->badge = circle(icon, BADGE_D, 0xFF3B30);
    lv_obj_set_style_border_color(ic->badge, COL_BG, 0);
    lv_obj_set_style_border_width(ic->badge, 3, 0);
    lv_obj_align(ic->badge, LV_ALIGN_TOP_RIGHT, 6, -4);
    ic->badge_text = label(ic->badge, &lv_font_montserrat_20, COL_TEXT, "");
    lv_obj_center(ic->badge_text);
    lv_obj_add_flag(ic->badge, LV_OBJ_FLAG_HIDDEN);
}

static void build_icon(int i)
{
    const app_def_t *d = &APPS[i];
    app_icon_t *ic = &s.icons[i];
    memset(ic, 0, sizeof(*ic));
    lv_obj_t *b = circle(s.home, ICON_D, d->bottom);
    ic->icon = b;
    lv_obj_set_user_data(b, (void *) (intptr_t) i);
    lv_obj_set_style_bg_grad_color(b, lv_color_hex(d->bottom), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(d->top), 0);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
    // Pressed, it sinks a little, as a watch's does.
    lv_obj_set_style_transform_width(b, -5, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(b, -5, LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_OVERFLOW_VISIBLE); // the badge sticks out
    lv_obj_add_event_cb(b, app_cb, LV_EVENT_CLICKED, (void *) (intptr_t) i);
    lv_obj_add_event_cb(b, app_cb, LV_EVENT_PRESSED, (void *) (intptr_t) i);

    switch (i) {
    case APP_TIMERS:
        ic->arcs[0] = ring_arc(b, ICON_D - 18, 8, COL_TEXT, lv_color_hex(0xB85A00));
        ic->glyph = label(b, &lv_font_montserrat_28, COL_TEXT, LV_SYMBOL_BELL);
        lv_obj_center(ic->glyph);
        break;
    case APP_HABITS:
        // The day's first three habits as rings, as a watch's Activity.
        for (int k = 0; k < 3; k++) {
            const habit_t *h = habits_at(k);
            static const uint32_t RGB[] = {0xFF375F, 0x30D158, 0x40C8E0};
            const lv_color_t c = lv_color_hex(h != NULL ? h->rgb : RGB[k]);
            ic->arcs[k] = ring_arc(b, ICON_D - 14 - 26 * k, 11, c, lv_color_mix(c, lv_color_black(), 60));
        }
        break;
    case APP_FOCUS: {
        lv_obj_t *moon = circle(b, 46, 0xF2F2F5);
        lv_obj_align(moon, LV_ALIGN_CENTER, -4, 2);
        lv_obj_t *bite = circle(b, 40, 0x5E5CE6);
        lv_obj_align(bite, LV_ALIGN_CENTER, 12, -10);
        ic->arcs[0] = moon;
        ic->arcs[1] = bite;
        break;
    }
    default:
        ic->glyph = label(b, &lv_font_montserrat_40, COL_TEXT, d->glyph);
        lv_obj_center(ic->glyph);
        break;
    }
    // Every icon has one, hidden until it has something to count.
    add_badge(b, ic);
}

/* Six round apps around Yoru, who peeks out from the middle. */
static void build_home(bool pop)
{
    show_face(true);
    s.home = full_screen(false); // see-through: Yoru is the middle one
    lv_obj_add_event_cb(s.home, home_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s.home, home_cb, LV_EVENT_CLICKED, NULL);
    for (int i = 0; i < APP_COUNT; i++) {
        build_icon(i);
        if (pop) {
            pop_out(s.icons[i].icon, i);
        } else {
            pop_cb(s.icons[i].icon, 1024);
        }
    }
    refresh_home();
    s.hint = label(s.home, &lv_font_montserrat_20, COL_TEXT, "");
    lv_obj_align(s.hint, LV_ALIGN_CENTER, 0, 80);
}

/* ---- Open and close -------------------------------------------------------------------------- */

static void idle_cb(lv_timer_t *t)
{
    const uint32_t idle = lv_display_get_inactive_time(NULL);
    const uint32_t limit = s.home != NULL ? IDLE_HOME_MS : s.app != NULL ? IDLE_APP_MS : IDLE_PAGE_MS;
    if (idle >= limit) {
        menu_close();
        return;
    }
    if (s.page != NULL) {
        refresh_values();
    }
    if (s.home != NULL) {
        refresh_home();
    }
    if (s.app != NULL) {
        timers_ui_refresh();
        habits_ui_refresh();
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
    agents_ui_close(); // one thing over the eyes at a time
    inbox_ui_close();
    s.open = true;
    lv_display_trigger_activity(NULL);
    build_home(true);
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
    drop(&s.home);
    drop(&s.settings);
    drop(&s.page);
    drop(&s.editor);
    drop(&s.app);
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
    static const char *const APP_NAMES[APP_COUNT] = {"inbox", "timers", "habits", "settings", NULL, NULL};
    if (strcasecmp(name, "home") == 0) {
        menu_open();
        return true;
    }
    for (int i = 0; i < APP_COUNT; i++) {
        if (APP_NAMES[i] != NULL && strcasecmp(name, APP_NAMES[i]) == 0) {
            menu_open();
            launch((app_t) i);
            return true;
        }
    }
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
