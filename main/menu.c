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
#include "esp_random.h"
#include "esp_system.h"
#include "habits.h"
#include "habits_ui.h"
#include "inbox.h"
#include "inbox_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link.h"
#include "look.h"
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
/* The home: a honeycomb of small round bubbles, as on a watch, Yoru one of
 * them. Centre to centre this far, each this big. */
#define GRID_STEP 100.0f
#define BUBBLE_R 42.0f
/* The fisheye: past FISH_IN from the middle of the screen the bubbles
 * shrink, down to FISH_MIN of their size FISH_SPAN further out. */
#define FISH_IN 130.0f
#define FISH_SPAN 110.0f
#define FISH_MIN 0.3f
/* The home redraws only the bubbles that changed, at most this often. */
#define HOME_FRAME_MS 16
/* Opening, Yoru shrinks into its bubble and the others bloom out of the
 * middle one after the other. Closing, Yoru goes back to the middle at the
 * size the face has under the menu (eyes.c, MENU_SIZE), and the face takes
 * over. */
#define INTRO_MS 360
#define INTRO_STAGGER_MS 30
#define OUTRO_MS 240
#define YORU_PEEK_R (LOOK_SPHERE_R * 0.62f)
/* A tapped bubble grows over the screen as it opens its app, and shrinks
 * back into place when you come back. Past the corners: */
#define ZOOM_MS 260
#define ZOOM_R 340.0f
#define PULSE_MS 260
#define NOTE_MS 1800
/* A finger that moves this far pans instead of tapping. */
#define TAP_SLOP 10
#define MAX_THROW 1600.0f
/* A swipe up this long, from this close to the bottom edge, closes it. */
#define EDGE_PX 56
#define CLOSE_SWIPE_PX 60
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
    SET("24-bit colour", "Colori a 24 bit", K_SWITCH, SETTING_COLOR_24, F_NONE),
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
    SET("BOOT button", "Tasto BOOT", K_SWITCH, SETTING_BOOT_KEY, F_NONE),
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

/* The bubbles on the home in honeycomb order: the middle, then clockwise
 * from the top left (slot_pos). A new app goes at the end, on the next ring. */
typedef enum {
    APP_INBOX,
    APP_YORU,
    APP_TIMERS,
    APP_HABITS,
    APP_SETTINGS,
    APP_FOCUS,
    APP_COUNT
} app_t;

typedef struct {
    const char *en, *it;
    /* The bubble's gradient, top to bottom. */
    uint32_t top, bottom;
    const char *glyph;
} app_def_t;

static const app_def_t APPS[APP_COUNT] = {
    [APP_INBOX] = {"Notifications", "Notifiche", 0xFF6B6B, 0xE0262F, LV_SYMBOL_ENVELOPE},
    [APP_YORU] = {"Yoru", "Yoru", 0, 0, NULL},
    [APP_TIMERS] = {"Timers", "Timer", 0xFFB340, 0xF07800, NULL},
    [APP_HABITS] = {"Habits", "Abitudini", 0x2A2D36, 0x101114, NULL},
    [APP_SETTINGS] = {"Settings", "Impostazioni", 0x9A9AA2, 0x55555D, LV_SYMBOL_SETTINGS},
    [APP_FOCUS] = {"Do not disturb", "Non disturbare", 0x8E8CF8, 0x4B48D6, NULL},
};

static struct {
    lv_obj_t *face;
    menu_cb_t cb;
    /* One of these is up at a time: the home, Settings, a category's page,
     * the arc, or an app's page. */
    lv_obj_t *home, *settings, *page, *editor, *app;
    /* The app last opened, whose bubble the way back shrinks into. */
    app_t from;
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

/* A bubble this frame: its centre from the middle of the screen, its radius. */
typedef struct {
    float x, y, r;
} bubble_t;

/* What the bubbles show live: badges, the timer's ring, the habits' rings,
 * the moon. */
typedef struct {
    int badge[APP_COUNT];
    uint32_t badge_rgb[APP_COUNT];
    float timer;
    float habit[3];
    uint32_t habit_rgb[3];
    bool focus;
} live_t;

typedef enum { PH_IDLE, PH_INTRO, PH_OUTRO, PH_ZOOM, PH_UNZOOM } phase_t;

/* The home: one object that draws every bubble itself, so a frame redraws
 * the bubbles that changed and nothing else. */
static struct {
    lv_obj_t *obj, *name;
    lv_timer_t *tick;
    uint32_t tick_ms;
    phase_t phase;
    uint32_t phase_ms;
    int target; // the bubble zooming
    /* The pan, kept while the menu is open, and a throw's speed (px/s). */
    float ox, oy, vx, vy;
    bool dragging, drag, from_bottom;
    lv_point_t press_pt, last_pt;
    uint32_t last_ms;
    float press_ox, press_oy;
    int pressed; // -1: none
    uint32_t pulse_ms[APP_COUNT];
    bubble_t now[APP_COUNT];
    lv_area_t drawn[APP_COUNT];
    uint32_t dirty; // a bit per bubble to redraw where it is
    live_t live;
    const char *note;
    uint32_t note_ms;
    /* Yoru's eyes: where they look, the lid, the blinks. */
    float gx, gy, lid;
    uint32_t blink_ms, next_blink_ms;
    uint32_t ball_frame;
} h;

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
static void build_home(app_t from);
static void home_drop(void);
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
    home_drop();
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
    build_home(APP_SETTINGS);
}

/* The categories, each a row with its round icon, as a watch's Settings. */
static void build_settings(void)
{
    home_drop();
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

/* Each bubble from its slot and the pan to where it shows this frame. */
static float ease_out(float k)
{
    const float u = 1 - k;
    return 1 - u * u * u;
}

static float ease_in_out(float k)
{
    return k < 0.5f ? 4 * k * k * k : 1 - powf(-2 * k + 2, 3) / 2;
}

/* Past 1 and back, for a pop. */
static float back_out(float k)
{
    const float c1 = 1.4f, c3 = c1 + 1, u = k - 1;
    return 1 + c3 * u * u * u + c1 * u * u;
}

static float clamp01(float k)
{
    return k < 0 ? 0 : k > 1 ? 1 : k;
}

/* Slot i of the honeycomb, from the middle: the middle, then ring after
 * ring of 6, 12, 18..., each from the top left clockwise. */
static void slot_pos(int i, float *x, float *y)
{
    static const int8_t DIR[6][2] = {{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}};
    int q = 0, r = 0;
    if (i > 0) {
        int k = 1, n = i - 1;
        while (n >= 6 * k) {
            n -= 6 * k;
            k++;
        }
        r = -k;
        for (int side = 0; side < 6 && n > 0; side++) {
            const int steps = LV_MIN(n, k);
            q += DIR[side][0] * steps;
            r += DIR[side][1] * steps;
            n -= steps;
        }
    }
    *x = GRID_STEP * ((float) q + (float) r * 0.5f);
    *y = GRID_STEP * 0.8660254f * (float) r;
}

/* Like a watch's: towards the rim the bubbles shrink and close up, so more
 * of them fit and the ones in the middle are the big ones. Returns the
 * scale; (x, y) from the middle of the screen. */
static float fisheye(float *x, float *y)
{
    const float d = hypotf(*x, *y);
    if (d <= FISH_IN) {
        return 1;
    }
    const float u = d - FISH_IN;
    const float t = fminf(1, u / FISH_SPAN);
    const float to = u <= FISH_SPAN ? FISH_IN + u * (1 - 0.4f * t)
                                    : FISH_IN + 0.6f * FISH_SPAN + 0.2f * (u - FISH_SPAN);
    *x *= to / d;
    *y *= to / d;
    return 1 - (1 - FISH_MIN) * t * t * (3 - 2 * t);
}

static void pan_bounds(float *x0, float *x1, float *y0, float *y1)
{
    *x0 = *x1 = *y0 = *y1 = 0;
    for (int i = 0; i < APP_COUNT; i++) {
        float x, y;
        slot_pos(i, &x, &y);
        // Far enough that any of them can come to the middle.
        *x0 = fminf(*x0, -x);
        *x1 = fmaxf(*x1, -x);
        *y0 = fminf(*y0, -y);
        *y1 = fmaxf(*y1, -y);
    }
}

static uint32_t since(uint32_t ms)
{
    return lv_tick_elaps(ms);
}

/* Where bubble i is this frame and how big, from the middle of the screen. */
static bubble_t place(int i)
{
    float x, y;
    slot_pos(i, &x, &y);
    x += h.ox;
    y += h.oy;
    const float f = fisheye(&x, &y);
    float r = BUBBLE_R * f;
    const float t = (float) since(h.phase_ms);
    switch (h.phase) {
    case PH_INTRO: {
        if (i == APP_YORU) {
            // Yoru shrinks into its bubble from where it was, in the middle.
            const float k = ease_out(clamp01(t / INTRO_MS));
            x *= k;
            y *= k;
            r = LOOK_SPHERE_R + (r - LOOK_SPHERE_R) * k;
            break;
        }
        // The others bloom out of the middle one after the other.
        const float k = clamp01((t - (float) i * INTRO_STAGGER_MS) / INTRO_MS);
        const float spread = 0.45f + 0.55f * ease_out(k);
        x *= spread;
        y *= spread;
        r *= k <= 0 ? 0 : back_out(k);
        break;
    }
    case PH_OUTRO: {
        const float k = ease_in_out(clamp01(t / OUTRO_MS));
        if (i == APP_YORU) {
            // Back to the middle at the size the face has under the menu,
            // which takes over from there.
            x *= 1 - k;
            y *= 1 - k;
            r += (YORU_PEEK_R - r) * k;
        } else {
            x *= 1 - 0.4f * k;
            y *= 1 - 0.4f * k;
            r *= 1 - k;
        }
        break;
    }
    case PH_ZOOM:
    case PH_UNZOOM:
        if (i == h.target) {
            float k = ease_in_out(clamp01(t / ZOOM_MS));
            k = h.phase == PH_ZOOM ? k : 1 - k;
            x *= 1 - k;
            y *= 1 - k;
            r += (ZOOM_R - r) * k;
        }
        break;
    default:
        break;
    }
    if (i == h.pressed) {
        r *= 0.9f; // it sinks a little under the finger
    }
    if (h.pulse_ms[i] != 0) {
        const float k = clamp01((float) since(h.pulse_ms[i]) / PULSE_MS);
        r *= 1 - 0.16f * sinf((float) M_PI * k);
    }
    return (bubble_t) {x, y, fmaxf(0, r)};
}

static lv_area_t bubble_area(const bubble_t *b)
{
    // The badge sticks out a little past the top right.
    const float pad = 0.12f * b->r + 4;
    lv_area_t a = {
        (int32_t) floorf(CX + b->x - b->r - pad),
        (int32_t) floorf(CY + b->y - b->r - pad),
        (int32_t) ceilf(CX + b->x + b->r + pad),
        (int32_t) ceilf(CY + b->y + b->r + pad),
    };
    return a;
}

static void invalidate_bubble(int i, const bubble_t *b)
{
    const lv_area_t was = h.drawn[i], now = bubble_area(b);
    const bool touch = was.x1 <= now.x2 && now.x1 <= was.x2 && was.y1 <= now.y2 && now.y1 <= was.y2;
    if (touch) {
        lv_area_t both = {LV_MIN(was.x1, now.x1), LV_MIN(was.y1, now.y1), LV_MAX(was.x2, now.x2),
                          LV_MAX(was.y2, now.y2)};
        lv_obj_invalidate_area(h.obj, &both);
    } else {
        lv_obj_invalidate_area(h.obj, &was);
        lv_obj_invalidate_area(h.obj, &now);
    }
    h.drawn[i] = now;
}

/* ---- Drawing the home: everything by hand, in one object ---------------------- */

static void disc(lv_layer_t *layer, float x, float y, float r, lv_color_t color)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.radius = LV_RADIUS_CIRCLE;
    const lv_area_t a = {(int32_t) lroundf(x - r), (int32_t) lroundf(y - r), (int32_t) lroundf(x + r) - 1,
                         (int32_t) lroundf(y + r) - 1};
    lv_draw_rect(layer, &dsc, &a);
}

/* A ring that fills up from the top, clockwise. `r` is its outer edge. */
static void ring(lv_layer_t *layer, float x, float y, float r, float w, float k, lv_color_t color, lv_color_t track)
{
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center.x = (int32_t) lroundf(x);
    dsc.center.y = (int32_t) lroundf(y);
    dsc.radius = (uint16_t) lroundf(r);
    dsc.width = (int32_t) fmaxf(1, lroundf(w));
    dsc.start_angle = 0;
    dsc.end_angle = 360;
    dsc.color = track;
    lv_draw_arc(layer, &dsc);
    k = clamp01(k);
    if (k > 0.005f) {
        dsc.start_angle = 270;
        dsc.end_angle = 270 + (int32_t) lroundf(360 * k);
        dsc.rounded = k < 0.995f;
        dsc.color = color;
        lv_draw_arc(layer, &dsc);
    }
}

static void text(lv_layer_t *layer, const lv_font_t *font, float x, float y, lv_color_t color, const char *str)
{
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = font;
    dsc.color = color;
    dsc.text = str;
    dsc.align = LV_TEXT_ALIGN_CENTER;
    const int32_t lh = lv_font_get_line_height(font);
    const lv_area_t a = {(int32_t) x - 60, (int32_t) y - lh / 2, (int32_t) x + 60, (int32_t) y - lh / 2 + lh};
    lv_draw_label(layer, &dsc, &a);
}

/* The glyph's font for a bubble this big; NULL when too small for one. */
static const lv_font_t *glyph_font(float r, int smaller)
{
    static const lv_font_t *const FONTS[] = {&lv_font_montserrat_28, &lv_font_montserrat_20, &lv_font_montserrat_14};
    const int step = (r >= 34 ? 0 : r >= 24 ? 1 : r >= 16 ? 2 : 3) + smaller;
    return step < 3 ? FONTS[step] : NULL;
}

/* Yoru in its bubble: its own sphere, and its eyes as under the menu
 * (eyes.c, EYES_MOOD_PEEK), so the face can take over where it ends. */
static void draw_yoru(lv_layer_t *layer, float x, float y, float r)
{
    // Rendered once per size (look_ball): fewer sizes when it is big.
    // Big, in steps of 4 from its own size, which is rendered already.
    const int rq = r > 64 ? LOOK_SPHERE_R - 4 * (int) lroundf((LOOK_SPHERE_R - r) / 4)
                          : LV_MAX(2, 2 * (int) lroundf(r / 2));
    const look_sprite_t *sp = look_ball(rq, rq, ++h.ball_frame);
    if (sp == NULL) {
        return;
    }
    look_draw(layer, sp, (int32_t) lroundf(x), (int32_t) lroundf(y), 1, 1, 0, LV_OPA_COVER, lv_color_black());
    const float rr = (float) rq;
    const float nx = 1 - 0.3f * h.gx * h.gx, ny = 1 - 0.25f * h.gy * h.gy;
    const float hw = fmaxf(1, 0.071f * rr * nx), hh = fmaxf(1, 0.157f * rr * ny * h.lid);
    const float ey = y + (0.114f + 0.343f * h.gy) * rr;
    for (int side = -1; side <= 1; side += 2) {
        const float ex = x + ((float) side * 0.229f * nx + 0.443f * h.gx) * rr;
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = lv_color_hex(0x1D1B24);
        dsc.radius = LV_RADIUS_CIRCLE;
        const lv_area_t a = {(int32_t) lroundf(ex - hw), (int32_t) lroundf(ey - hh), (int32_t) lroundf(ex + hw),
                             (int32_t) lroundf(ey + hh)};
        lv_draw_rect(layer, &dsc, &a);
    }
}

static void draw_badge(lv_layer_t *layer, int i, float x, float y, float r)
{
    const int n = h.live.badge[i];
    if (n <= 0 || r < 18) {
        return;
    }
    const float bx = x + 0.72f * r, by = y - 0.72f * r, br = 0.31f * r;
    disc(layer, bx, by, br + fmaxf(2, 0.07f * r), COL_BG); // cut out of the bubble
    disc(layer, bx, by, br, lv_color_hex(h.live.badge_rgb[i]));
    char str[8];
    snprintf(str, sizeof(str), n > 9 ? "9+" : "%d", n);
    text(layer, r >= 34 ? &lv_font_montserrat_20 : &lv_font_montserrat_14, bx, by, COL_TEXT, str);
}

static void draw_app(lv_layer_t *layer, int i, const bubble_t *b)
{
    const float x = CX + b->x, y = CY + b->y, r = b->r;
    if (r < 1) {
        return;
    }
    if (i == APP_YORU) {
        draw_yoru(layer, x, y, r);
        return;
    }
    const app_def_t *d = &APPS[i];
    const bool zooming = (h.phase == PH_ZOOM || h.phase == PH_UNZOOM) && i == h.target;
    // Opening, it darkens to the black of the page it opens: the page then
    // takes over without a fade (a blend over the whole screen is slow).
    float dark = 0;
    if (zooming) {
        const float k = clamp01((float) since(h.phase_ms) / ZOOM_MS);
        dark = h.phase == PH_ZOOM ? k : 1 - k;
        dark = dark * dark;
    }
    const bool focus_on = h.live.focus;
    uint32_t top = d->top, bottom = d->bottom;
    if (i == APP_FOCUS) {
        top = bottom = focus_on ? 0x5E5CE6 : 0x2C2C34;
    }
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = LV_RADIUS_CIRCLE;
    const lv_color_t c0 = lv_color_mix(lv_color_black(), lv_color_hex(top), (uint8_t) (255 * dark));
    const lv_color_t c1 = lv_color_mix(lv_color_black(), lv_color_hex(bottom), (uint8_t) (255 * dark));
    dsc.bg_color = c1;
    if (top != bottom) {
        const lv_color_t colors[2] = {c0, c1};
        lv_grad_init_stops(&dsc.bg_grad, colors, NULL, NULL, 2);
        dsc.bg_grad.dir = LV_GRAD_DIR_VER;
    }
    const lv_area_t a = {(int32_t) lroundf(x - r), (int32_t) lroundf(y - r), (int32_t) lroundf(x + r) - 1,
                         (int32_t) lroundf(y + r) - 1};
    lv_draw_rect(layer, &dsc, &a);
    if (zooming && dark > 0.15f) {
        return; // only the colour grows to the page
    }
    // What's on it keeps its size while the bubble grows from under it.
    const float rc = zooming ? BUBBLE_R : r;
    switch (i) {
    case APP_TIMERS: {
        ring(layer, x, y, 0.84f * rc, 0.15f * rc, h.live.timer, COL_TEXT, lv_color_hex(0xB85A00));
        const lv_font_t *f = glyph_font(rc, 1);
        if (f != NULL) {
            text(layer, f, x, y, COL_TEXT, LV_SYMBOL_BELL);
        }
        break;
    }
    case APP_HABITS:
        // The day's first three habits as rings, as a watch's Activity.
        for (int k = 0; k < 3; k++) {
            const lv_color_t c = lv_color_hex(h.live.habit_rgb[k]);
            ring(layer, x, y, (0.86f - 0.235f * (float) k) * rc, 0.19f * rc, h.live.habit[k], c,
                 lv_color_mix(c, lv_color_black(), 60));
        }
        break;
    case APP_FOCUS: {
        // The moon: a pale disc with a bite out of it in the bubble's colour.
        disc(layer, x - 0.07f * rc, y + 0.04f * rc, 0.41f * rc, focus_on ? COL_TEXT : lv_color_hex(0x8E8E93));
        disc(layer, x + 0.21f * rc, y - 0.18f * rc, 0.36f * rc, lv_color_hex(top));
        break;
    }
    default: {
        const lv_font_t *f = glyph_font(rc, 0);
        if (f != NULL && d->glyph != NULL) {
            text(layer, f, x, y, COL_TEXT, d->glyph);
        }
        break;
    }
    }
    draw_badge(layer, i, x, y, rc);
}

/* Over the object's black, which hides what's under it. */
static void home_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const bool zooming = h.phase == PH_ZOOM || h.phase == PH_UNZOOM;
    for (int i = 0; i < APP_COUNT; i++) {
        if (!(zooming && i == h.target)) {
            draw_app(layer, i, &h.now[i]);
        }
    }
    if (zooming) {
        draw_app(layer, h.target, &h.now[h.target]); // over the others
    }
}

/* ---- What the bubbles say ------------------------------------------------------------ */

/* What is unread, waiting, running, done today; the bubbles that changed
 * are redrawn. */
static void refresh_home(void)
{
    live_t v;
    memset(&v, 0, sizeof(v));
    v.badge[APP_INBOX] = inbox_unread(NULL);
    v.badge_rgb[APP_INBOX] = 0xFF3B30;
    timer_view_t tv;
    const int nt = timers_count();
    v.timer = timers_at(0, &tv) && tv.total_s > 0 ? tv.left_s / tv.total_s : 0;
    v.badge[APP_TIMERS] = nt > 1 ? nt : 0;
    v.badge_rgb[APP_TIMERS] = 0xFF3B30;
    static const uint32_t RGB[] = {0xFF375F, 0x30D158, 0x40C8E0};
    for (int k = 0; k < 3; k++) {
        const habit_t *hb = habits_at(k);
        v.habit[k] = hb != NULL && hb->goal > 0 ? (float) hb->done / (float) hb->goal : 0;
        v.habit_rgb[k] = hb != NULL ? hb->rgb : RGB[k];
    }
    v.focus = board_settings_on(SETTING_FOCUS);

    for (int i = 0; i < APP_COUNT; i++) {
        if (v.badge[i] != h.live.badge[i]) {
            h.dirty |= 1u << i;
        }
    }
    if (fabsf(v.timer - h.live.timer) > 0.002f) {
        h.dirty |= 1u << APP_TIMERS;
    }
    if (memcmp(v.habit, h.live.habit, sizeof(v.habit)) != 0 || memcmp(v.habit_rgb, h.live.habit_rgb, sizeof(v.habit_rgb)) != 0) {
        h.dirty |= 1u << APP_HABITS;
    }
    if (v.focus != h.live.focus) {
        h.dirty |= 1u << APP_FOCUS;
    }
    h.live = v;
}

/* The name under the bubbles: the one under the finger, else the one in
 * the middle; or what just happened, for a moment. */
static void refresh_name(void)
{
    const char *str;
    if (h.note != NULL && since(h.note_ms) < NOTE_MS) {
        str = h.note;
    } else {
        int best = h.pressed;
        if (best < 0) {
            float d = 1e9f;
            for (int i = 0; i < APP_COUNT; i++) {
                const float di = hypotf(h.now[i].x, h.now[i].y);
                if (di < d) {
                    d = di;
                    best = i;
                }
            }
        }
        str = tr(APPS[best].en, APPS[best].it);
    }
    if (strcmp(lv_label_get_text(h.name), str) != 0) {
        lv_label_set_text(h.name, str);
    }
}

/* ---- Moving ----------------------------------------------------------------------------- */

static void open_target(app_t app);
static void home_drop(void);

/* Where Yoru looks: at the bubble under your finger, else at the others;
 * up as it goes back, as the face does under the menu. */
static void yoru_gaze(float *gx, float *gy)
{
    const bubble_t *y = &h.now[APP_YORU];
    float dx = -y->x, dy = -y->y;
    if (h.phase == PH_OUTRO) {
        dx = 0;
        dy = -1;
    } else if (h.pressed >= 0 && h.pressed != APP_YORU) {
        dx = h.now[h.pressed].x - y->x;
        dy = h.now[h.pressed].y - y->y;
    }
    const float d = hypotf(dx, dy);
    *gx = d > 1 ? 0.8f * dx / d : 0;
    *gy = d > 1 ? 0.8f * dy / d : 0.2f;
    if (h.phase == PH_OUTRO) {
        *gy = -1;
    }
}

static void yoru_step(float dt)
{
    // A blink every few seconds: closing quickly, opening a little slower.
    float lid = 1;
    if (h.blink_ms != 0) {
        const float t = (float) since(h.blink_ms);
        lid = t < 70 ? 1 - 0.94f * t / 70 : t < 200 ? 0.06f + 0.94f * (t - 70) / 130 : 1;
        if (t >= 200) {
            h.blink_ms = 0;
            h.next_blink_ms = lv_tick_get() + 2500 + esp_random() % 3000;
        }
    } else if (h.phase == PH_IDLE && (int32_t) (lv_tick_get() - h.next_blink_ms) >= 0) {
        h.blink_ms = lv_tick_get();
    }
    float tgx, tgy;
    yoru_gaze(&tgx, &tgy);
    const float k = 1 - expf(-dt * 14);
    const float gx = h.gx + (tgx - h.gx) * k, gy = h.gy + (tgy - h.gy) * k;
    if (fabsf(gx - h.gx) > 0.003f || fabsf(gy - h.gy) > 0.003f || lid != h.lid) {
        h.dirty |= 1u << APP_YORU;
    }
    h.gx = gx;
    h.gy = gy;
    h.lid = lid;
}

/* A throw glides on and slows down; past the edge it springs back. */
static void pan_step(float dt)
{
    if (h.dragging) {
        return;
    }
    float x0, x1, y0, y1;
    pan_bounds(&x0, &x1, &y0, &y1);
    h.ox += h.vx * dt;
    h.oy += h.vy * dt;
    const float slow = expf(-dt * 5);
    h.vx *= slow;
    h.vy *= slow;
    const float back = 1 - expf(-dt * 14);
    const float cx = LV_CLAMP(x0, h.ox, x1), cy = LV_CLAMP(y0, h.oy, y1);
    if (cx != h.ox) {
        h.ox += (cx - h.ox) * back;
        h.vx *= expf(-dt * 20);
        if (fabsf(cx - h.ox) < 0.3f) {
            h.ox = cx;
        }
    }
    if (cy != h.oy) {
        h.oy += (cy - h.oy) * back;
        h.vy *= expf(-dt * 20);
        if (fabsf(cy - h.oy) < 0.3f) {
            h.oy = cy;
        }
    }
    if (fabsf(h.vx) < 4) {
        h.vx = 0;
    }
    if (fabsf(h.vy) < 4) {
        h.vy = 0;
    }
}

static void tick_cb(lv_timer_t *t)
{
    const float dt = (float) LV_MIN(since(h.tick_ms), 100) / 1000.0f;
    h.tick_ms = lv_tick_get();
    pan_step(dt);
    yoru_step(dt);

    const uint32_t in_phase = since(h.phase_ms);
    bool done = false;
    if (h.phase == PH_INTRO && in_phase >= INTRO_MS + (APP_COUNT - 1) * INTRO_STAGGER_MS) {
        h.phase = PH_IDLE;
    } else if (h.phase == PH_UNZOOM && in_phase >= ZOOM_MS) {
        h.phase = PH_IDLE;
        h.dirty |= 1u << h.target; // with its glyph back
    } else if ((h.phase == PH_ZOOM && in_phase >= ZOOM_MS) || (h.phase == PH_OUTRO && in_phase >= OUTRO_MS)) {
        done = true;
    }
    for (int i = 0; i < APP_COUNT; i++) {
        if (h.pulse_ms[i] != 0 && since(h.pulse_ms[i]) >= PULSE_MS) {
            h.pulse_ms[i] = 0;
            h.dirty |= 1u << i;
        }
        const bubble_t b = place(i);
        const bubble_t *was = &h.now[i];
        const bool moved = fabsf(b.x - was->x) > 0.2f || fabsf(b.y - was->y) > 0.2f || fabsf(b.r - was->r) > 0.2f;
        // The one opening or closing changes colour too.
        const bool zooming = (h.phase == PH_ZOOM || h.phase == PH_UNZOOM) && i == h.target;
        if (moved || zooming || (h.dirty & (1u << i)) || h.pulse_ms[i] != 0) {
            h.now[i] = b;
            invalidate_bubble(i, &b);
        }
    }
    h.dirty = 0;
    refresh_name();

    if (done) {
        const phase_t ph = h.phase;
        const app_t app = (app_t) h.target;
        h.phase = PH_IDLE;
        if (ph == PH_ZOOM) {
            open_target(app);
        } else {
            menu_close();
        }
    }
}

static void set_phase(phase_t ph, int target)
{
    h.phase = ph;
    h.phase_ms = lv_tick_get();
    h.target = target;
}

static int hit(lv_point_t p)
{
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < APP_COUNT; i++) {
        const bubble_t *b = &h.now[i];
        const float d = hypotf((float) p.x - (CX + b->x), (float) p.y - (CY + b->y));
        if (d <= b->r + 6 && d < bd && b->r > 8) {
            best = i;
            bd = d;
        }
    }
    return best;
}

static void press(int i)
{
    if (h.pressed >= 0) {
        h.dirty |= 1u << h.pressed;
    }
    h.pressed = i;
    if (i >= 0) {
        h.dirty |= 1u << i;
    }
}

/* Past the edge the pan gives less and less, as a rubber band. */
static float band(float v, float lo, float hi)
{
    return v < lo ? lo - (lo - v) * 0.35f : v > hi ? hi + (v - hi) * 0.35f : v;
}

static void toggle_focus(void)
{
    const bool on = !board_settings_on(SETTING_FOCUS);
    board_settings_set(SETTING_FOCUS, on);
    playback_sound(on ? SOUND_YAWN : SOUND_CHIRP);
    refresh_home();
    h.note = on ? tr("Shh... do not disturb", "Shh... non disturbare") : tr("Back to normal", "Si torna a parlare");
    h.note_ms = lv_tick_get();
    h.pulse_ms[APP_FOCUS] = lv_tick_get(); // a little bounce, so the tap is felt
}

static void home_close(void)
{
    if (h.phase == PH_IDLE || h.phase == PH_INTRO) {
        press(-1);
        set_phase(PH_OUTRO, APP_YORU);
    }
}

static void tap(app_t app)
{
    switch (app) {
    case APP_YORU:
        home_close();
        break;
    case APP_FOCUS:
        toggle_focus();
        break;
    default:
        set_phase(PH_ZOOM, app);
        break;
    }
}

static void home_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (h.phase != PH_IDLE && h.phase != PH_INTRO) {
        return; // on its way somewhere
    }
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    const uint32_t now = lv_tick_get();
    switch (code) {
    case LV_EVENT_PRESSED:
        h.dragging = true;
        h.drag = false;
        h.press_pt = p;
        h.last_pt = p;
        h.last_ms = now;
        h.press_ox = h.ox;
        h.press_oy = h.oy;
        h.vx = h.vy = 0;
        h.from_bottom = p.y > BOARD_LCD_V_RES - EDGE_PX;
        press(hit(p));
        break;
    case LV_EVENT_PRESSING: {
        const int32_t dx = p.x - h.press_pt.x, dy = p.y - h.press_pt.y;
        if (h.from_bottom && -dy > CLOSE_SWIPE_PX) {
            // Pushed up from the bottom edge: back to Yoru, as the menu
            // came down from the top.
            h.dragging = false;
            home_close();
            return;
        }
        if (!h.drag && dx * dx + dy * dy > TAP_SLOP * TAP_SLOP) {
            h.drag = true;
            press(-1);
        }
        if (h.drag && !h.from_bottom) {
            float x0, x1, y0, y1;
            pan_bounds(&x0, &x1, &y0, &y1);
            h.ox = band(h.press_ox + (float) dx, x0, x1);
            h.oy = band(h.press_oy + (float) dy, y0, y1);
            const float dt = (float) LV_MAX(since(h.last_ms), 1) / 1000.0f;
            h.vx = 0.6f * h.vx + 0.4f * (float) (p.x - h.last_pt.x) / dt;
            h.vy = 0.6f * h.vy + 0.4f * (float) (p.y - h.last_pt.y) / dt;
            h.last_pt = p;
            h.last_ms = now;
        }
        break;
    }
    case LV_EVENT_RELEASED: {
        const int i = h.pressed;
        h.dragging = false;
        press(-1);
        // A finger held still at the end stops the throw.
        if (since(h.last_ms) > 80) {
            h.vx = h.vy = 0;
        }
        h.vx = LV_CLAMP(-MAX_THROW, h.vx, MAX_THROW);
        h.vy = LV_CLAMP(-MAX_THROW, h.vy, MAX_THROW);
        if (!h.drag && i >= 0) {
            tap((app_t) i);
        }
        break;
    }
    case LV_EVENT_PRESS_LOST:
        h.dragging = false;
        press(-1);
        break;
    default:
        break;
    }
}

/* The honeycomb, with Yoru as one of its bubbles. `from` is the app being
 * left, which shrinks back into its bubble; APP_COUNT for none (the
 * bubbles bloom out of Yoru). */
static void build_home(app_t from)
{
    home_drop();
    show_face(false); // Yoru is in its bubble now
    h.obj = full_screen(true);
    s.home = h.obj;
    lv_obj_add_event_cb(h.obj, home_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(h.obj, home_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(h.obj, home_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(h.obj, home_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(h.obj, home_cb, LV_EVENT_PRESS_LOST, NULL);
    h.name = label(h.obj, &lv_font_montserrat_20, COL_DIM, "");
    lv_obj_set_width(h.name, 260);
    lv_obj_set_style_text_align(h.name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(h.name, LV_LABEL_LONG_DOT);
    lv_obj_align(h.name, LV_ALIGN_BOTTOM_MID, 0, -30);

    h.pressed = -1;
    h.dragging = false;
    h.vx = h.vy = 0;
    h.note = NULL;
    h.lid = 1;
    h.blink_ms = 0;
    h.next_blink_ms = lv_tick_get() + 1500;
    memset(h.pulse_ms, 0, sizeof(h.pulse_ms));
    memset(&h.live, 0, sizeof(h.live));
    refresh_home();
    if (from < APP_COUNT) {
        set_phase(PH_UNZOOM, from);
    } else {
        set_phase(PH_INTRO, APP_YORU);
    }
    for (int i = 0; i < APP_COUNT; i++) {
        h.now[i] = place(i);
        h.drawn[i] = bubble_area(&h.now[i]);
    }
    yoru_gaze(&h.gx, &h.gy);
    lv_obj_invalidate(h.obj);
    refresh_name();
    h.tick_ms = lv_tick_get();
    h.tick = lv_timer_create(tick_cb, HOME_FRAME_MS, NULL);
}

static void home_drop(void)
{
    if (h.tick != NULL) {
        lv_timer_delete(h.tick);
        h.tick = NULL;
    }
    h.obj = NULL;
    h.name = NULL;
    drop(&s.home);
}

/* ---- Opening an app ------------------------------------------------------------------ */

static void app_back(void)
{
    drop(&s.app);
    build_home(s.from);
}

static void open_app(lv_obj_t *(*build)(void (*back)(void)))
{
    home_drop();
    show_face(false);
    s.app = build(app_back);
}

/* Once its bubble has grown over the screen, or at once for the host. */
static void open_target(app_t app)
{
    s.from = app;
    switch (app) {
    case APP_INBOX:
        inbox_ui_open(NULL); // the menu goes as the view comes
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
        break;
    }
}

/* ---- Open and close -------------------------------------------------------------------------- */

static void idle_cb(lv_timer_t *t)
{
    const uint32_t idle = lv_display_get_inactive_time(NULL);
    const uint32_t limit = s.home != NULL ? IDLE_HOME_MS : s.app != NULL ? IDLE_APP_MS : IDLE_PAGE_MS;
    if (idle >= limit) {
        if (s.home != NULL) {
            home_close(); // Yoru goes back to the middle
        } else {
            menu_close();
        }
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
    inbox_ui_close(); // one thing over the eyes at a time
    s.open = true;
    lv_display_trigger_activity(NULL);
    h.ox = h.oy = 0;
    build_home(APP_COUNT);
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
    home_drop();
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
    static const char *const APP_NAMES[APP_COUNT] = {
        [APP_INBOX] = "inbox", [APP_TIMERS] = "timers", [APP_HABITS] = "habits", [APP_SETTINGS] = "settings"};
    if (strcasecmp(name, "home") == 0) {
        menu_open();
        return true;
    }
    for (int i = 0; i < APP_COUNT; i++) {
        if (APP_NAMES[i] != NULL && strcasecmp(name, APP_NAMES[i]) == 0) {
            menu_open();
            open_target((app_t) i);
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
