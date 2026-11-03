#include "tasks_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "board.h"
#include "card.h"
#include "clock.h"
#include "playback.h"
#include "tasks.h"
#include "toast.h"

#define CY (BOARD_LCD_V_RES / 2)
#define COL_TASKS lv_color_hex(0x7FA7FF)
#define COL_OFF lv_color_hex(0x4A4D5C)
#define ROW_W 360
#define ROW_H 88
#define DOT_D 18
#define BOX_W 330
#define BTN_W 220
#define BTN_H 56

static struct {
    lv_obj_t *page, *title, *list, *detail;
    void (*back)(void);
    uint32_t version;
    /* The task open, or -1 for the list. */
    int open;
    /* Its id, to find it again in a new list. */
    char open_id[TASKS_ID_MAX + 1];
} s;

static void build(void);

/* ---- Words -------------------------------------------------------------------- */

static lv_color_t state_color(const task_t *t)
{
    return !t->enabled ? COL_OFF : t->running ? CARD_WAIT : t->failed ? CARD_DENY : CARD_ALLOW;
}

/* `at` (Unix seconds) as "today 09:00", "tomorrow 09:00" or "Mon 3/11 09:00". */
static void day_time(int64_t at, char *out, size_t len)
{
    const time_t t = (time_t) at, now = time(NULL), next_day = now + 86400;
    struct tm tm, today, tomorrow;
    if (!clock_valid() || localtime_r(&t, &tm) == NULL || localtime_r(&now, &today) == NULL
        || localtime_r(&next_day, &tomorrow) == NULL) {
        snprintf(out, len, "-");
        return;
    }
    static const char *const EN[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *const IT[] = {"Dom", "Lun", "Mar", "Mer", "Gio", "Ven", "Sab"};
    if (tm.tm_yday == today.tm_yday && tm.tm_year == today.tm_year) {
        snprintf(out, len, "%s %02d:%02d", card_tr("today", "oggi"), tm.tm_hour, tm.tm_min);
    } else if (tm.tm_yday == tomorrow.tm_yday && tm.tm_year == tomorrow.tm_year) {
        snprintf(out, len, "%s %02d:%02d", card_tr("tomorrow", "domani"), tm.tm_hour, tm.tm_min);
    } else {
        snprintf(out, len, "%s %d/%d %02d:%02d", (card_italian() ? IT : EN)[tm.tm_wday], tm.tm_mday, tm.tm_mon + 1,
                 tm.tm_hour, tm.tm_min);
    }
}

/* Under its name: what it does now, or when it runs next. */
static void status_line(const task_t *t, char *out, size_t len)
{
    char when[24];
    if (t->running) {
        snprintf(out, len, "%s", card_tr("Working on it...", "Ci sto lavorando..."));
    } else if (!t->enabled) {
        snprintf(out, len, "%s", t->last ? card_tr("Done, now off", "Fatto, ora spento") : card_tr("Off", "Spento"));
    } else if (t->next > 0) {
        day_time(t->next, when, sizeof(when));
        snprintf(out, len, "%s %s", card_tr("Next:", "Prossima:"), when);
    } else {
        tasks_when(t, out, len);
    }
}

/* ---- Touch --------------------------------------------------------------------- */

static void go_back(void)
{
    if (s.open >= 0) {
        s.open = -1;
        build();
    } else if (s.back != NULL) {
        s.back();
    }
}

static void gesture_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_RIGHT || dir == LV_DIR_BOTTOM) {
        lv_indev_wait_release(indev);
        go_back();
    }
}

static void title_cb(lv_event_t *e)
{
    go_back();
}

static void row_cb(lv_event_t *e)
{
    const int i = (int) (intptr_t) lv_event_get_user_data(e);
    const task_t *t = tasks_at(i);
    if (t == NULL) {
        return;
    }
    playback_sound(SOUND_BOOP);
    s.open = i;
    snprintf(s.open_id, sizeof(s.open_id), "%s", t->id);
    build();
}

static void run_cb(lv_event_t *e)
{
    const task_t *t = tasks_at(s.open);
    if (t == NULL || t->running) {
        return;
    }
    if (!tasks_run(s.open)) {
        playback_sound(SOUND_PFFT);
        toast_show(card_tr("I need the computer for that", "Per questo mi serve il computer"), 3, TOAST_INFO);
        return;
    }
    playback_sound(SOUND_YES);
    toast_show(card_tr("On it!", "Ci penso io!"), 2, TOAST_DONE);
    build();
}

/* Rows fade out as they near the rim, as the menu's do. */
static void scroll_cb(lv_event_t *e)
{
    lv_obj_t *list = lv_event_get_user_data(e);
    const uint32_t n = lv_obj_get_child_count(list);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_get_child(list, (int32_t) i);
        lv_area_t a;
        lv_obj_get_coords(row, &a);
        const int32_t d = LV_ABS((a.y1 + a.y2) / 2 - CY);
        lv_obj_set_style_opa(row, (lv_opa_t) (d < 120 ? 255 : LV_MAX(0, 255 - (d - 120) * 255 / 100)), 0);
    }
}

/* ---- The list ---------------------------------------------------------------------- */

static lv_obj_t *column(int32_t pad_bottom)
{
    lv_obj_t *c = lv_obj_create(s.page);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, BOARD_LCD_H_RES, BOARD_LCD_V_RES - 86);
    lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, 12, 0);
    lv_obj_set_style_pad_top(c, 8, 0);
    lv_obj_set_style_pad_bottom(c, pad_bottom, 0);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_OFF);
    return c;
}

static void add_row(int i, const task_t *t)
{
    lv_obj_t *row = lv_obj_create(s.list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, ROW_W, ROW_H);
    lv_obj_set_style_radius(row, ROW_H / 2, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, CARD_ROW, 0);
    lv_obj_set_style_bg_color(row, CARD_ROW_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(row, 26, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, row_cb, LV_EVENT_SHORT_CLICKED, (void *) (intptr_t) i);

    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, DOT_D, DOT_D);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, state_color(t), 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);

    const int32_t x = DOT_D + 16, w = ROW_W - 52 - x;
    lv_obj_t *name = card_label(row, &lv_font_montserrat_24, t->enabled ? CARD_TEXT : CARD_DIM, t->name);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(name, w);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, x, 14);
    char line[48];
    status_line(t, line, sizeof(line));
    lv_obj_t *sub = card_label(row, &lv_font_montserrat_14, t->running ? CARD_WAIT : CARD_DIM, line);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(sub, w);
    lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, x, -16);
}

static void add_empty(void)
{
    lv_obj_t *box = lv_obj_create(s.list);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, ROW_W, 140);
    lv_obj_t *none = card_label(box, &lv_font_montserrat_24, CARD_TEXT, card_tr("No tasks yet", "Nessun compito"));
    lv_obj_align(none, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_t *hint = card_label(box, &lv_font_montserrat_20, CARD_DIM,
                                card_tr("Give me one: I'll do it\non my own", "Dammene uno: lo faro'\nda solo"));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 78);
}

static void build_list(void)
{
    s.list = column(150);
    lv_obj_add_event_cb(s.list, scroll_cb, LV_EVENT_SCROLL, s.list);
    for (int i = 0; i < tasks_count(); i++) {
        add_row(i, tasks_at(i));
    }
    if (tasks_count() == 0) {
        add_empty();
    }
    lv_obj_update_layout(s.list);
    lv_obj_send_event(s.list, LV_EVENT_SCROLL, NULL);
}

/* ---- One task ------------------------------------------------------------------------ */

static lv_obj_t *words(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = card_label(parent, font, color, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, BOX_W);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

static void build_detail(const task_t *t)
{
    s.detail = column(120);
    char text[64];
    tasks_when(t, text, sizeof(text));
    words(s.detail, &lv_font_montserrat_20, COL_TASKS, text);
    words(s.detail, &lv_font_montserrat_20, CARD_TEXT, t->what[0] ? t->what : t->name);
    static const char *const ACTING_EN[] = {"Reads and tells only", "Asks before acting", "Acts on its own"};
    static const char *const ACTING_IT[] = {"Legge e riferisce", "Chiede prima di agire", "Agisce da solo"};
    words(s.detail, &lv_font_montserrat_14, t->acting == TASK_ACTS ? CARD_WAIT : CARD_DIM,
          (card_italian() ? ACTING_IT : ACTING_EN)[t->acting < 3 ? t->acting : 1]);
    if (t->last > 0) {
        char when[24];
        day_time(t->last, when, sizeof(when));
        snprintf(text, sizeof(text), "%s %s", card_tr("Last time:", "Ultima volta:"), when);
        words(s.detail, &lv_font_montserrat_14, t->failed ? CARD_DENY : CARD_DIM, text);
        if (t->result[0] != '\0') {
            words(s.detail, &lv_font_montserrat_20, CARD_DIM, t->result);
        }
    }
    const char *label = t->running ? card_tr("Working on it...", "Ci sto lavorando...")
                                   : card_tr(LV_SYMBOL_PLAY " Do it now", LV_SYMBOL_PLAY " Fallo adesso");
    lv_obj_t *b = card_button(s.detail, BTN_W, BTN_H, t->running ? CARD_WAIT : CARD_ALLOW,
                              t->running ? CARD_WAIT_BG : CARD_ALLOW_BG, label, &lv_font_montserrat_20, run_cb,
                              LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_margin_top(b, 6, 0);
}

/* ---- The page ------------------------------------------------------------------------ */

static void build(void)
{
    if (s.list != NULL) {
        lv_obj_add_flag(s.list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(s.list);
        s.list = NULL;
    }
    if (s.detail != NULL) {
        lv_obj_add_flag(s.detail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(s.detail);
        s.detail = NULL;
    }
    s.version = tasks_version();
    // The task open, wherever the new list put it.
    if (s.open >= 0) {
        s.open = -1;
        for (int i = 0; i < tasks_count(); i++) {
            if (strcmp(tasks_at(i)->id, s.open_id) == 0) {
                s.open = i;
            }
        }
    }
    char title[48];
    const task_t *t = tasks_at(s.open);
    snprintf(title, sizeof(title), LV_SYMBOL_LEFT "  %s", t != NULL ? t->name : card_tr("Tasks", "Compiti"));
    lv_label_set_text(s.title, title);
    if (t != NULL) {
        build_detail(t);
    } else {
        build_list();
    }
}

static void deleted_cb(lv_event_t *e)
{
    // Only this page's: the menu deletes it later, maybe after a new one.
    if (lv_event_get_target(e) == s.page) {
        memset(&s, 0, sizeof(s));
    }
}

lv_obj_t *tasks_ui_build(void (*back)(void))
{
    memset(&s, 0, sizeof(s));
    s.back = back;
    s.open = -1;
    s.page = card_root();
    lv_obj_add_event_cb(s.page, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s.page, deleted_cb, LV_EVENT_DELETE, NULL);
    s.title = card_label(s.page, &lv_font_montserrat_28, COL_TASKS, "");
    lv_label_set_long_mode(s.title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s.title, 300);
    lv_obj_set_style_text_align(s.title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s.title, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_add_flag(s.title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s.title, 24);
    lv_obj_add_event_cb(s.title, title_cb, LV_EVENT_CLICKED, NULL);
    build();
    return s.page;
}

void tasks_ui_refresh(void)
{
    if (s.page != NULL && tasks_version() != s.version) {
        build();
    }
}
