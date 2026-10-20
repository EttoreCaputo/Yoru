#include "timers_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "card.h"
#include "playback.h"
#include "timers.h"

#define CY (BOARD_LCD_V_RES / 2)
#define ROW_W 360
#define ROW_H 100
#define RING_D 72
#define COL_TIMER lv_color_hex(0xFF9F0A)
#define COL_WORK lv_color_hex(0xFF6347)
#define COL_BREAK lv_color_hex(0x30D158)
#define COL_REMINDER lv_color_hex(0xFFD60A)
#define COL_RING_BG lv_color_hex(0x4A2A00)

typedef struct {
    lv_obj_t *row, *arc, *glyph, *time, *sub;
    timer_state_t state;
} row_t;

static struct {
    lv_obj_t *page, *list;
    void (*back)(void);
    uint32_t version;
    int n;
    row_t rows[TIMERS_MAX];
    char ids[TIMERS_MAX][TIMERS_ID_MAX + 1];
} s;

static lv_color_t kind_color(timer_kind_t kind)
{
    switch (kind) {
    case TIMER_KIND_WORK:
        return COL_WORK;
    case TIMER_KIND_BREAK:
        return COL_BREAK;
    case TIMER_KIND_REMINDER:
        return COL_REMINDER;
    default:
        return COL_TIMER;
    }
}

/* "4:32", "1:05:00"; a reminder says when instead. */
static void big_text(const timer_view_t *t, char *out, size_t len)
{
    if (t->kind == TIMER_KIND_REMINDER && t->at[0] != '\0') {
        snprintf(out, len, "%s", t->at);
        return;
    }
    const int left = (int) ceilf(t->left_s);
    if (left >= 3600) {
        snprintf(out, len, "%d:%02d:%02d", left / 3600, left / 60 % 60, left % 60);
    } else {
        snprintf(out, len, "%d:%02d", left / 60, left % 60);
    }
}

static void sub_text(const timer_view_t *t, char *out, size_t len)
{
    const char *what = t->label[0] != '\0' ? t->label
                       : t->kind == TIMER_KIND_WORK     ? card_tr("Focus", "Concentrazione")
                       : t->kind == TIMER_KIND_BREAK    ? card_tr("Break", "Pausa")
                       : t->kind == TIMER_KIND_REMINDER ? card_tr("Reminder", "Promemoria")
                                                        : "Timer";
    if (t->state == TIMER_RING) {
        snprintf(out, len, "%s", card_tr("Tap to stop", "Tocca per fermare"));
    } else if (t->state == TIMER_PAUSE) {
        snprintf(out, len, "%s, %s", what, card_tr("paused", "in pausa"));
    } else if (t->kind == TIMER_KIND_REMINDER) {
        const int min = (int) ceilf(t->left_s / 60);
        snprintf(out, len, min >= 60 ? card_tr("%s, in %d h %d", "%s, tra %d h %d") : card_tr("%s, in %d min", "%s, tra %d min"),
                 what, min >= 60 ? min / 60 : min, min % 60);
    } else {
        snprintf(out, len, "%s", what);
    }
}

static const char *glyph(const timer_view_t *t)
{
    if (t->kind == TIMER_KIND_REMINDER || t->state == TIMER_RING) {
        return LV_SYMBOL_BELL;
    }
    return t->state == TIMER_PAUSE ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE;
}

static void set_text(lv_obj_t *l, const char *text)
{
    if (strcmp(lv_label_get_text(l), text) != 0) {
        lv_label_set_text(l, text);
    }
}

/* ---- Touch ------------------------------------------------------------------- */

static void go_back(void)
{
    if (s.back != NULL) {
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
    timer_view_t t;
    if (!timers_at(i, &t) || strcmp(t.id, s.ids[i]) != 0) {
        return; // the list changed under the finger
    }
    if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        timers_act(t.id, t.state == TIMER_RING ? "dismiss" : "cancel");
        playback_sound(SOUND_PFFT);
    } else if (t.state == TIMER_RING) {
        timers_act(t.id, "dismiss");
        playback_sound(SOUND_PHEW);
    } else if (t.kind != TIMER_KIND_REMINDER) {
        timers_act(t.id, t.state == TIMER_PAUSE ? "resume" : "pause");
        playback_sound(SOUND_BOOP);
    }
    timers_ui_refresh();
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
        lv_obj_set_style_opa(row, (lv_opa_t) (d < 110 ? 255 : LV_MAX(0, 255 - (d - 110) * 255 / 110)), 0);
    }
}

/* ---- Rows ---------------------------------------------------------------------- */

static void glow_cb(void *row, int32_t v)
{
    lv_obj_set_style_bg_color(row, lv_color_mix(lv_color_hex(0x6A3A00), CARD_ROW, (uint8_t) v), 0);
}

/* A ringing one glows orange, on and off. */
static void glow(lv_obj_t *row, bool on)
{
    lv_anim_delete(row, glow_cb);
    if (!on) {
        lv_obj_set_style_bg_color(row, CARD_ROW, 0);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, row);
    lv_anim_set_exec_cb(&a, glow_cb);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_duration(&a, 600);
    lv_anim_set_reverse_duration(&a, 600);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void update_row(row_t *r, const timer_view_t *t)
{
    char text[40];
    big_text(t, text, sizeof(text));
    set_text(r->time, text);
    sub_text(t, text, sizeof(text));
    set_text(r->sub, text);
    set_text(r->glyph, glyph(t));
    const int32_t v = (int32_t) lroundf(1000 * LV_CLAMP(0.0f, t->left_s / t->total_s, 1.0f));
    if (lv_arc_get_value(r->arc) != v) {
        lv_arc_set_value(r->arc, v);
    }
    if (r->state != t->state) {
        r->state = t->state;
        glow(r->row, t->state == TIMER_RING);
    }
}

static void add_row(int i, const timer_view_t *t)
{
    row_t *r = &s.rows[i];
    snprintf(s.ids[i], sizeof(s.ids[i]), "%s", t->id);
    r->row = lv_obj_create(s.list);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_size(r->row, ROW_W, ROW_H);
    lv_obj_set_style_radius(r->row, ROW_H / 2, 0);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r->row, CARD_ROW, 0);
    lv_obj_set_style_pad_hor(r->row, 14, 0);
    lv_obj_remove_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r->row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r->row, row_cb, LV_EVENT_SHORT_CLICKED, (void *) (intptr_t) i);
    lv_obj_add_event_cb(r->row, row_cb, LV_EVENT_LONG_PRESSED, (void *) (intptr_t) i);

    const lv_color_t c = kind_color(t->kind);
    r->arc = lv_arc_create(r->row);
    lv_obj_remove_style_all(r->arc);
    lv_obj_set_size(r->arc, RING_D, RING_D);
    lv_obj_align(r->arc, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_remove_flag(r->arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(r->arc, 270);
    lv_arc_set_bg_angles(r->arc, 0, 360);
    lv_arc_set_range(r->arc, 0, 1000);
    lv_obj_set_style_arc_width(r->arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(r->arc, lv_color_mix(c, lv_color_black(), 60), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(r->arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(r->arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(r->arc, c, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(r->arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(r->arc, true, LV_PART_INDICATOR);
    r->glyph = card_label(r->arc, &lv_font_montserrat_24, c, "");
    lv_obj_center(r->glyph);

    r->time = card_label(r->row, &lv_font_montserrat_40, CARD_TEXT, "");
    lv_obj_align(r->time, LV_ALIGN_TOP_LEFT, RING_D + 16, 10);
    r->sub = card_label(r->row, &lv_font_montserrat_20, CARD_DIM, "");
    lv_label_set_long_mode(r->sub, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(r->sub, ROW_W - 28 - RING_D - 16);
    lv_obj_align(r->sub, LV_ALIGN_BOTTOM_LEFT, RING_D + 16, -12);
    r->state = TIMER_RUN;
    update_row(r, t);
}

static void add_empty(void)
{
    lv_obj_t *box = lv_obj_create(s.list);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, ROW_W, 190);
    lv_obj_t *bell = card_label(box, &lv_font_montserrat_40, lv_color_hex(0x5A3A10), LV_SYMBOL_BELL);
    lv_obj_align(bell, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_t *none = card_label(box, &lv_font_montserrat_24, CARD_TEXT, card_tr("No timers", "Nessun timer"));
    lv_obj_align(none, LV_ALIGN_TOP_MID, 0, 66);
    lv_obj_t *hint = card_label(box, &lv_font_montserrat_20, CARD_DIM,
                                card_tr("Ask Yoru: \"a timer for\n10 minutes\"", "Chiedi a Yoru: \"un timer\ndi 10 minuti\""));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 104);
}

static void build_list(void)
{
    if (s.list != NULL) {
        // Async: this may run from one of its rows. Hidden now, so the two
        // don't show together for a frame.
        lv_obj_add_flag(s.list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(s.list);
    }
    s.version = timers_version();
    s.n = timers_count();
    s.list = lv_obj_create(s.page);
    lv_obj_remove_style_all(s.list);
    lv_obj_set_size(s.list, BOARD_LCD_H_RES, BOARD_LCD_V_RES - 86);
    lv_obj_align(s.list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(s.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s.list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s.list, 12, 0);
    lv_obj_set_style_pad_top(s.list, 40, 0);
    lv_obj_set_style_pad_bottom(s.list, 150, 0);
    lv_obj_set_scroll_dir(s.list, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(s.list, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(s.list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s.list, scroll_cb, LV_EVENT_SCROLL, s.list);
    for (int i = 0; i < s.n; i++) {
        timer_view_t t;
        if (timers_at(i, &t)) {
            add_row(i, &t);
        }
    }
    if (s.n == 0) {
        add_empty();
    }
    lv_obj_update_layout(s.list);
    lv_obj_send_event(s.list, LV_EVENT_SCROLL, NULL);
}

/* ---- The page ------------------------------------------------------------------------ */

/* LVGL takes the glows away with their rows. */
static void deleted_cb(lv_event_t *e)
{
    // Only this page's: the menu deletes it later, maybe after a new one.
    if (lv_event_get_target(e) == s.page) {
        memset(&s, 0, sizeof(s));
    }
}

lv_obj_t *timers_ui_build(void (*back)(void))
{
    memset(&s, 0, sizeof(s));
    s.back = back;
    s.page = card_root(true);
    lv_obj_add_event_cb(s.page, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s.page, deleted_cb, LV_EVENT_DELETE, NULL);
    lv_obj_t *title = card_label(s.page, &lv_font_montserrat_28, COL_TIMER, LV_SYMBOL_LEFT "  Timer");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_add_flag(title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(title, 24);
    lv_obj_add_event_cb(title, title_cb, LV_EVENT_CLICKED, NULL);
    build_list();
    return s.page;
}

void timers_ui_refresh(void)
{
    if (s.page == NULL) {
        return;
    }
    if (timers_version() != s.version || timers_count() != s.n) {
        build_list();
        return;
    }
    for (int i = 0; i < s.n; i++) {
        timer_view_t t;
        if (timers_at(i, &t)) {
            update_row(&s.rows[i], &t);
        }
    }
}
