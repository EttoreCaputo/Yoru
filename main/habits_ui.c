#include "habits_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "card.h"
#include "esp_random.h"
#include "habits.h"
#include "playback.h"

#define CX (BOARD_LCD_H_RES / 2)
#define CY (BOARD_LCD_V_RES / 2)
#define COL_HABITS lv_color_hex(0x30D158)
#define ROW_W 360
#define ROW_H 88
#define CHECK_D 58
#define RINGS_D 168
#define RING_W 14
#define SPARK_MS 520

typedef struct {
    lv_obj_t *check, *arc, *mark, *sub;
} row_t;

static struct {
    lv_obj_t *page, *list;
    void (*back)(void);
    uint32_t version;
    int n;
    row_t rows[HABITS_MAX];
    lv_obj_t *rings[3];
    lv_obj_t *count, *today;
} s;

/* ---- Sparkles --------------------------------------------------------------- */

typedef struct {
    int32_t x, y, dx, dy;
} spark_t;

static void spark_cb(void *var, int32_t v)
{
    const spark_t *sp = lv_obj_get_user_data(var);
    // Out fast, slowing down, and fading as it goes.
    const float k = (float) v / 1024, out = 1 - (1 - k) * (1 - k);
    lv_obj_set_pos(var, sp->x + (int32_t) (sp->dx * out), sp->y + (int32_t) (sp->dy * out) + (int32_t) (40 * k * k));
    lv_obj_set_style_bg_opa(var, (lv_opa_t) (255 * (1 - k)), 0);
}

static void spark_done_cb(lv_anim_t *a)
{
    lv_obj_delete(a->var);
}

/* Its place goes with it, however it goes (the page may close first). */
static void spark_deleted_cb(lv_event_t *e)
{
    lv_free(lv_obj_get_user_data(lv_event_get_target(e)));
}

/* `n` dots fly out of (x, y), the screen's coordinates, `reach` far. */
static void sparkle(int32_t x, int32_t y, lv_color_t c, int n, int reach)
{
    static const uint32_t CONFETTI[] = {0xFFD45C, 0xFF7AB8, 0x7FE3FF, 0x30D158, 0xBF5AF2};
    for (int i = 0; i < n; i++) {
        spark_t *sp = lv_malloc(sizeof(spark_t));
        if (sp == NULL) {
            return;
        }
        const float a = 2 * (float) M_PI * ((float) i + (float) (esp_random() % 100) / 200) / (float) n;
        const float r = (float) reach * (0.7f + (float) (esp_random() % 60) / 100);
        const int32_t d = 6 + (int32_t) (esp_random() % 6);
        *sp = (spark_t) {x - d / 2, y - d / 2, (int32_t) (r * cosf(a)), (int32_t) (r * sinf(a))};
        lv_obj_t *o = lv_obj_create(s.page);
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, d, d);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, n > 10 && i % 2 ? lv_color_hex(CONFETTI[i % 5]) : c, 0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(o, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_user_data(o, sp);
        lv_obj_add_event_cb(o, spark_deleted_cb, LV_EVENT_DELETE, NULL);
        lv_anim_t an;
        lv_anim_init(&an);
        lv_anim_set_var(&an, o);
        lv_anim_set_exec_cb(&an, spark_cb);
        lv_anim_set_values(&an, 0, 1024);
        lv_anim_set_duration(&an, SPARK_MS + (uint32_t) (n > 10 ? 300 : 0));
        lv_anim_set_completed_cb(&an, spark_done_cb);
        lv_anim_start(&an);
        spark_cb(o, 0);
    }
}

/* A circle that pops: it swells and settles back with a bounce. */
static void pop_cb(void *var, int32_t v)
{
    lv_obj_set_style_transform_width(var, v, 0);
    lv_obj_set_style_transform_height(var, v, 0);
}

static void pop(lv_obj_t *o, int32_t by)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, pop_cb);
    lv_anim_set_values(&a, by, 0);
    lv_anim_set_duration(&a, 380);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);
}

static void centre_of(lv_obj_t *o, int32_t *x, int32_t *y)
{
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    *x = (a.x1 + a.x2) / 2;
    *y = (a.y1 + a.y2) / 2;
}

/* ---- What the rows say -------------------------------------------------------- */

static void set_text(lv_obj_t *l, const char *text)
{
    if (strcmp(lv_label_get_text(l), text) != 0) {
        lv_label_set_text(l, text);
    }
}

static void paint_row(int i)
{
    const habit_t *h = habits_at(i);
    row_t *r = &s.rows[i];
    if (h == NULL || r->check == NULL) {
        return;
    }
    const bool done = h->done >= h->goal;
    const lv_color_t c = lv_color_hex(h->rgb);
    lv_obj_set_style_bg_opa(r->check, done ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(r->check, done || h->goal > 1 ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    char text[40];
    if (done) {
        set_text(r->mark, LV_SYMBOL_OK);
        lv_obj_set_style_text_color(r->mark, lv_color_black(), 0);
        lv_obj_set_style_text_font(r->mark, &lv_font_montserrat_28, 0);
    } else if (h->goal > 1) {
        snprintf(text, sizeof(text), "%d", h->done);
        set_text(r->mark, text);
        lv_obj_set_style_text_color(r->mark, c, 0);
        lv_obj_set_style_text_font(r->mark, &lv_font_montserrat_24, 0);
    } else {
        set_text(r->mark, "");
    }
    if (r->arc != NULL) {
        lv_arc_set_value(r->arc, h->done * 1000 / h->goal);
        if (done) {
            lv_obj_add_flag(r->arc, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(r->arc, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (h->goal > 1 && !done) {
        snprintf(text, sizeof(text), card_tr("%d of %d today", "%d di %d oggi"), h->done, h->goal);
    } else if (h->streak > 1) {
        snprintf(text, sizeof(text), card_tr("%d days in a row", "%d giorni di fila"), h->streak);
    } else if (done) {
        snprintf(text, sizeof(text), "%s", card_tr("Done today", "Fatto oggi"));
    } else {
        snprintf(text, sizeof(text), "%s", card_tr("Tap when it's done", "Tocca quando e' fatto"));
    }
    set_text(r->sub, text);
}

static void paint_summary(void)
{
    for (int k = 0; k < 3; k++) {
        const habit_t *h = habits_at(k);
        if (s.rings[k] != NULL && h != NULL) {
            lv_arc_set_value(s.rings[k], h->done * 1000 / h->goal);
        }
    }
    char text[16];
    const bool all = s.n > 0 && habits_done() == s.n;
    if (all) {
        set_text(s.count, LV_SYMBOL_OK);
        lv_obj_set_style_text_color(s.count, COL_HABITS, 0);
        set_text(s.today, card_tr("All done!", "Tutto fatto!"));
    } else {
        snprintf(text, sizeof(text), "%d/%d", habits_done(), s.n);
        set_text(s.count, text);
        lv_obj_set_style_text_color(s.count, CARD_TEXT, 0);
        set_text(s.today, card_tr("today", "oggi"));
    }
}

/* ---- Touch --------------------------------------------------------------------- */

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
    const habit_t *h = habits_at(i);
    if (h == NULL) {
        return;
    }
    const bool undo = lv_event_get_code(e) == LV_EVENT_LONG_PRESSED;
    const bool was_all = habits_done() == s.n;
    const int done = habits_tick(i, undo ? -1 : 1);
    s.version = habits_version(); // ours: no need to rebuild
    row_t *r = &s.rows[i];
    if (done < 0) {
        pop(r->check, -8); // already full (or empty): a shrug of a pop
        return;
    }
    paint_row(i);
    paint_summary();
    if (undo) {
        playback_sound(SOUND_PFFT);
        return;
    }
    int32_t x, y;
    centre_of(r->check, &x, &y);
    pop(r->check, 16);
    if (done < h->goal) {
        playback_sound(SOUND_BOOP);
        sparkle(x, y, lv_color_hex(h->rgb), 5, 30);
        return;
    }
    if (habits_done() == s.n && !was_all) {
        // The whole day: confetti out of the rings.
        playback_sound(SOUND_TADA);
        sparkle(x, y, lv_color_hex(h->rgb), 8, 46);
        if (s.count != NULL) {
            centre_of(s.count, &x, &y);
            pop(s.count, 30);
            sparkle(x, y, COL_HABITS, 18, 120);
        }
        return;
    }
    playback_sound(SOUND_YES);
    sparkle(x, y, lv_color_hex(h->rgb), 8, 46);
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

/* ---- Building --------------------------------------------------------------------- */

static lv_obj_t *ring(lv_obj_t *parent, int32_t d, int32_t w, lv_color_t c)
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
    lv_obj_set_style_arc_color(arc, lv_color_mix(c, lv_color_black(), 55), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, c, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    return arc;
}

/* The day's rings, the first three habits', around how many are done. */
static void add_summary(void)
{
    lv_obj_t *box = lv_obj_create(s.list);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, RINGS_D, RINGS_D);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    static const uint32_t RGB[] = {0xFF375F, 0x30D158, 0x40C8E0};
    for (int k = 0; k < 3; k++) {
        const habit_t *h = habits_at(k);
        s.rings[k] = ring(box, RINGS_D - k * (2 * RING_W + 6), RING_W, lv_color_hex(h != NULL ? h->rgb : RGB[k]));
    }
    s.count = card_label(box, &lv_font_montserrat_28, CARD_TEXT, "");
    lv_obj_align(s.count, LV_ALIGN_CENTER, 0, -8);
    s.today = card_label(box, &lv_font_montserrat_14, CARD_DIM, "");
    lv_obj_align(s.today, LV_ALIGN_CENTER, 0, 18);
    paint_summary();
}

static void add_row(int i, const habit_t *h)
{
    row_t *r = &s.rows[i];
    const lv_color_t c = lv_color_hex(h->rgb);
    lv_obj_t *row = lv_obj_create(s.list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, ROW_W, ROW_H);
    lv_obj_set_style_radius(row, ROW_H / 2, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, CARD_ROW, 0);
    lv_obj_set_style_bg_color(row, CARD_ROW_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(row, 15, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, row_cb, LV_EVENT_SHORT_CLICKED, (void *) (intptr_t) i);
    lv_obj_add_event_cb(row, row_cb, LV_EVENT_LONG_PRESSED, (void *) (intptr_t) i);

    // The circle to tick: an outline, a ring that fills for a count, a
    // full disc with a tick once done.
    r->check = lv_obj_create(row);
    lv_obj_remove_style_all(r->check);
    lv_obj_set_size(r->check, CHECK_D, CHECK_D);
    lv_obj_align(r->check, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(r->check, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(r->check, c, 0);
    lv_obj_set_style_border_color(r->check, c, 0);
    lv_obj_set_style_border_width(r->check, 4, 0);
    lv_obj_remove_flag(r->check, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(r->check, LV_OBJ_FLAG_SCROLLABLE);
    r->arc = h->goal > 1 ? ring(r->check, CHECK_D, 6, c) : NULL;
    r->mark = card_label(r->check, &lv_font_montserrat_24, c, "");
    lv_obj_center(r->mark);

    lv_obj_t *name = card_label(row, &lv_font_montserrat_24, CARD_TEXT, h->name);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(name, ROW_W - 30 - CHECK_D - 14);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, CHECK_D + 14, 14);
    r->sub = card_label(row, &lv_font_montserrat_14, CARD_DIM, "");
    lv_obj_align(r->sub, LV_ALIGN_BOTTOM_LEFT, CHECK_D + 14, -16);
    paint_row(i);
}

static void add_empty(void)
{
    lv_obj_t *box = lv_obj_create(s.list);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, ROW_W, 100);
    lv_obj_t *none = card_label(box, &lv_font_montserrat_24, CARD_TEXT, card_tr("No habits yet", "Nessuna abitudine"));
    lv_obj_align(none, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_t *hint = card_label(box, &lv_font_montserrat_20, CARD_DIM,
                                card_tr("Ask Yoru to help you\nwith a new one", "Chiedi a Yoru di aiutarti\ncon una nuova"));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 44);
}

static void build_list(void)
{
    if (s.list != NULL) {
        lv_obj_add_flag(s.list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(s.list);
    }
    memset(s.rows, 0, sizeof(s.rows));
    s.version = habits_version();
    s.n = habits_count();
    s.list = lv_obj_create(s.page);
    lv_obj_remove_style_all(s.list);
    lv_obj_set_size(s.list, BOARD_LCD_H_RES, BOARD_LCD_V_RES - 86);
    lv_obj_align(s.list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(s.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s.list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s.list, 12, 0);
    lv_obj_set_style_pad_top(s.list, 8, 0);
    lv_obj_set_style_pad_bottom(s.list, 150, 0);
    lv_obj_set_scroll_dir(s.list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s.list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s.list, scroll_cb, LV_EVENT_SCROLL, s.list);
    add_summary();
    for (int i = 0; i < s.n; i++) {
        add_row(i, habits_at(i));
    }
    if (s.n == 0) {
        add_empty();
    }
    lv_obj_update_layout(s.list);
    lv_obj_send_event(s.list, LV_EVENT_SCROLL, NULL);
}

/* ---- The page ------------------------------------------------------------------------ */

static void deleted_cb(lv_event_t *e)
{
    // Only this page's: the menu deletes it later, maybe after a new one.
    if (lv_event_get_target(e) == s.page) {
        memset(&s, 0, sizeof(s));
    }
}

lv_obj_t *habits_ui_build(void (*back)(void))
{
    memset(&s, 0, sizeof(s));
    s.back = back;
    s.page = card_root();
    lv_obj_add_event_cb(s.page, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s.page, deleted_cb, LV_EVENT_DELETE, NULL);
    char title[32];
    snprintf(title, sizeof(title), LV_SYMBOL_LEFT "  %s", card_tr("Habits", "Abitudini"));
    lv_obj_t *t = card_label(s.page, &lv_font_montserrat_28, COL_HABITS, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(t, 24);
    lv_obj_add_event_cb(t, title_cb, LV_EVENT_CLICKED, NULL);
    build_list();
    return s.page;
}

void habits_ui_refresh(void)
{
    if (s.page != NULL && habits_version() != s.version) {
        build_list();
    }
}
