#include "inbox_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "card.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "approval.h"
#include "menu.h"
#include "toast.h"

static const char *TAG = "inbox_ui";

#define CX (BOARD_LCD_H_RES / 2)
#define CY (BOARD_LCD_V_RES / 2)
#define POLL_MS 200
/* The unread dot: on the rim at the top right. */
#define DOT_R 222
#define DOT_DEG (-54.0f)
#define DOT_D 12
/* The banner, over the bottom rim where the toast is, inside the circle. */
#define BANNER_W 296
#define BANNER_H 60
#define BANNER_Y (BOARD_LCD_V_RES - 116)
#define BANNER_NORMAL_S 5
#define BANNER_HIGH_S 8
#define BANNER_URGENT_S 12
/* The view. */
#define LIST_IDLE_MS 30000
#define DETAIL_IDLE_MS 90000
#define AGE_REFRESH_MS 30000
#define TOP_H 70
#define ROW_W 344
#define ROW_H 78
#define ROW_PAD 26
#define MARK_D 10
/* The detail: the words in a box that scrolls, the host's actions, then
 * Dismiss and Open, all inside the circle. */
#define HEAD_Y 30
#define HEAD_W 260
#define BOX_Y 84
#define BOX_W 330
#define BTN_W 140
#define BTN_H 52
#define BTN_GAP 12
#define ACTIONS_Y 280
#define BOTTOM_Y 342
#define BODY_COLOR lv_color_hex(0xC4C7D4)

enum { BTN_DISMISS = 10, BTN_OPEN = 11 };

/* What the rows and the detail point at, by id: the inbox may change under
 * them. PSRAM, as the inbox. */
EXT_RAM_BSS_ATTR static char s_row_ids[INBOX_MAX][INBOX_ID_MAX + 1];
EXT_RAM_BSS_ATTR static inbox_item_t s_detail;

static struct {
    lv_obj_t *face;
    inbox_ui_cb_t cb;
    lv_obj_t *dot;
    inbox_urgency_t dot_urgency;
    bool dot_stale; // drawn for another urgency or visibility
    lv_obj_t *banner, *banner_source, *banner_title;
    char banner_id[INBOX_ID_MAX + 1];
    uint32_t banner_until_ms; // 0: none
    bool hidden;
    /* The view, and on it the list or one's detail. */
    lv_obj_t *view, *page, *list;
    bool detail;
    uint32_t version;
    uint32_t built_ms;
} s;

static void show_list(void);
static void show_detail(const char *id);

static uint32_t now_ms(void)
{
    return lv_tick_get();
}

static lv_color_t urgency_color(inbox_urgency_t u)
{
    switch (u) {
    case INBOX_URGENT:
        return CARD_HURRY;
    case INBOX_HIGH:
        return CARD_WAIT;
    case INBOX_NORMAL:
        return CARD_TEXT;
    default:
        return CARD_DIM;
    }
}

/* "now", "5 min", "2 h", "3 d", from when it reached the board. */
static void age_text(uint32_t arrived_s, char *out, size_t len)
{
    const uint32_t age = inbox_now_s() - arrived_s;
    if (age < 60) {
        snprintf(out, len, "%s", card_tr("now", "ora"));
    } else if (age < 3600) {
        snprintf(out, len, "%u min", (unsigned) (age / 60));
    } else if (age < 86400) {
        snprintf(out, len, "%u h", (unsigned) (age / 3600));
    } else {
        snprintf(out, len, card_tr("%u d", "%u g"), (unsigned) (age / 86400));
    }
}

/* ---- The unread dot ------------------------------------------------------------ */

static void opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa(obj, (lv_opa_t) v, 0);
}

static void place_dot(void)
{
    inbox_urgency_t top;
    const int unread = inbox_unread(&top);
    if (s.hidden || unread == 0) {
        if (!lv_obj_has_flag(s.dot, LV_OBJ_FLAG_HIDDEN)) {
            lv_anim_delete(s.dot, opa_cb);
            lv_obj_add_flag(s.dot, LV_OBJ_FLAG_HIDDEN);
        }
        s.dot_urgency = INBOX_NONE;
        return;
    }
    if (top == s.dot_urgency && !s.dot_stale) {
        return;
    }
    s.dot_urgency = top;
    s.dot_stale = false;
    lv_obj_set_style_bg_color(s.dot, urgency_color(top), 0);
    lv_anim_delete(s.dot, opa_cb);
    lv_obj_set_style_opa(s.dot, top == INBOX_LOW ? LV_OPA_70 : LV_OPA_COVER, 0);
    if (top >= INBOX_HIGH) {
        // Pulsing, as a waiting agent's dot does.
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s.dot);
        lv_anim_set_exec_cb(&a, opa_cb);
        lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_20);
        lv_anim_set_duration(&a, 900);
        lv_anim_set_reverse_duration(&a, 900);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }
    lv_obj_remove_flag(s.dot, LV_OBJ_FLAG_HIDDEN);
}

/* ---- The banner ------------------------------------------------------------------ */

static void banner_cb(lv_event_t *e)
{
    char id[INBOX_ID_MAX + 1];
    snprintf(id, sizeof(id), "%s", s.banner_id);
    s.banner_until_ms = 0;
    lv_obj_add_flag(s.banner, LV_OBJ_FLAG_HIDDEN);
    inbox_ui_open(id);
}

static void update_banner(void)
{
    if (s.banner_until_ms != 0 && (int32_t) (now_ms() - s.banner_until_ms) >= 0) {
        s.banner_until_ms = 0;
    }
    // Gone from the inbox (hidden by the host, or dismissed): no banner.
    if (s.banner_until_ms != 0 && inbox_find(s.banner_id) == NULL) {
        s.banner_until_ms = 0;
    }
    const bool show = s.banner_until_ms != 0 && !s.hidden;
    if (show == lv_obj_has_flag(s.banner, LV_OBJ_FLAG_HIDDEN)) {
        if (show) {
            lv_obj_remove_flag(s.banner, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s.banner, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void inbox_ui_banner(inbox_urgency_t urgency)
{
    const inbox_item_t *it = inbox_at(0);
    if (it == NULL || urgency < INBOX_NORMAL) {
        return;
    }
    snprintf(s.banner_id, sizeof(s.banner_id), "%s", it->id);
    lv_label_set_text(s.banner_source, it->source[0] ? it->source : "Yoru");
    lv_label_set_text(s.banner_title, it->title);
    lv_obj_set_style_border_color(s.banner, urgency_color(urgency), 0);
    const int seconds = urgency == INBOX_URGENT ? BANNER_URGENT_S
                        : urgency == INBOX_HIGH ? BANNER_HIGH_S
                                                : BANNER_NORMAL_S;
    s.banner_until_ms = now_ms() + (uint32_t) seconds * 1000;
    if (s.banner_until_ms == 0) {
        s.banner_until_ms = 1; // 0 means none
    }
    update_banner();
    if (!s.hidden) {
        lv_obj_fade_in(s.banner, 150, 0);
    }
}

/* ---- The list ---------------------------------------------------------------------- */

static void back_or_close(void)
{
    if (s.detail) {
        show_list();
    } else {
        inbox_ui_close();
    }
}

static void view_gesture_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_RIGHT || dir == LV_DIR_BOTTOM) {
        lv_indev_wait_release(indev);
        back_or_close();
    }
}

static void back_cb(lv_event_t *e)
{
    back_or_close();
}

/* Rows fade out as they near the rim, as the menu's do. */
static void list_scroll_cb(lv_event_t *e)
{
    lv_obj_t *list = lv_event_get_user_data(e);
    const uint32_t n = lv_obj_get_child_count(list);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_get_child(list, (int32_t) i);
        lv_area_t a;
        lv_obj_get_coords(row, &a);
        const int32_t d = LV_ABS((a.y1 + a.y2) / 2 - CY);
        const int32_t opa = d < 120 ? 255 : LV_MAX(0, 255 - (d - 120) * 255 / 100);
        lv_obj_set_style_opa(row, (lv_opa_t) opa, 0);
    }
}

static void row_cb(lv_event_t *e)
{
    const int i = (int) (intptr_t) lv_event_get_user_data(e);
    char id[INBOX_ID_MAX + 1];
    snprintf(id, sizeof(id), "%s", s_row_ids[i]);
    show_detail(id);
}

static void add_row(lv_obj_t *list, int i, const inbox_item_t *it)
{
    snprintf(s_row_ids[i], sizeof(s_row_ids[i]), "%s", it->id);
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, ROW_W, ROW_H);
    lv_obj_set_style_radius(row, 26, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, CARD_ROW, 0);
    lv_obj_set_style_bg_color(row, CARD_ROW_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(row, ROW_PAD, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void *) (intptr_t) i);

    const int32_t inner = ROW_W - 2 * ROW_PAD;
    int32_t x = 0;
    if (it->unread) {
        lv_obj_t *mark = lv_obj_create(row);
        lv_obj_remove_style_all(mark);
        lv_obj_set_size(mark, MARK_D, MARK_D);
        lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(mark, it->urgency >= INBOX_HIGH ? urgency_color(it->urgency) : CARD_ACCENT, 0);
        lv_obj_align(mark, LV_ALIGN_TOP_LEFT, 0, 14);
        x = MARK_D + 8;
    }
    char age[16];
    age_text(it->arrived_s, age, sizeof(age));
    lv_obj_t *when = card_label(row, &lv_font_montserrat_14, CARD_DIM, age);
    lv_obj_align(when, LV_ALIGN_TOP_RIGHT, 0, 10);
    lv_obj_t *src = card_label(row, &lv_font_montserrat_14, it->unread ? CARD_ACCENT : CARD_DIM,
                               it->source[0] ? it->source : "Yoru");
    lv_label_set_long_mode(src, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(src, inner - x - 60);
    lv_obj_align(src, LV_ALIGN_TOP_LEFT, x, 10);
    lv_obj_t *title = card_label(row, &lv_font_montserrat_20, it->unread ? CARD_TEXT : CARD_DIM, it->title);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(title, inner, lv_font_get_line_height(&lv_font_montserrat_20));
    lv_obj_align(title, LV_ALIGN_BOTTOM_LEFT, 0, -12);
}

static lv_obj_t *new_page(void)
{
    if (s.page != NULL) {
        // Async: this runs from the old page's own buttons. Hidden now, so
        // the two don't show together for a frame.
        lv_obj_add_flag(s.page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(s.page);
    }
    s.page = lv_obj_create(s.view);
    s.list = NULL;
    lv_obj_remove_style_all(s.page);
    lv_obj_set_size(s.page, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_remove_flag(s.page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s.page, LV_OBJ_FLAG_CLICKABLE); // the view takes the swipes
    s.built_ms = now_ms();
    s.version = inbox_version();
    return s.page;
}

static void show_list(void)
{
    // Where it was scrolled to, so a refresh doesn't jump.
    const int32_t scrolled = s.list != NULL && !s.detail ? lv_obj_get_scroll_y(s.list) : 0;
    s.detail = false;
    lv_obj_t *page = new_page();

    lv_obj_t *title = card_label(page, &lv_font_montserrat_20, CARD_ACCENT,
                                 card_tr(LV_SYMBOL_DOWN "  Inbox", LV_SYMBOL_DOWN "  Notifiche"));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_add_flag(title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(title, 24);
    lv_obj_add_event_cb(title, back_cb, LV_EVENT_CLICKED, NULL);

    const int n = inbox_count();
    if (n == 0) {
        lv_obj_t *none = card_label(page, &lv_font_montserrat_20, CARD_DIM, card_tr("Nothing new", "Niente di nuovo"));
        lv_obj_align(none, LV_ALIGN_CENTER, 0, -16);
        lv_obj_t *hint = card_label(page, &lv_font_montserrat_14, CARD_DIM,
                                    card_tr("What your computer tells you shows here", "Qui arriva cio' che ti dice il PC"));
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, 16);
        return;
    }
    lv_obj_t *list = lv_obj_create(page);
    s.list = list;
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, BOARD_LCD_H_RES, BOARD_LCD_V_RES - TOP_H);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_style_pad_top(list, 20, 0);
    // Room below, so the last row can come up to the middle.
    lv_obj_set_style_pad_bottom(list, 150, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(list, list_scroll_cb, LV_EVENT_SCROLL, list);
    for (int i = 0; i < n; i++) {
        add_row(list, i, inbox_at(i));
    }
    lv_obj_update_layout(list);
    if (scrolled > 0) {
        lv_obj_scroll_to_y(list, scrolled, LV_ANIM_OFF);
    }
    lv_obj_send_event(list, LV_EVENT_SCROLL, NULL);
}

/* ---- One notification ----------------------------------------------------------------- */

static void button_cb(lv_event_t *e)
{
    const int b = (int) (intptr_t) lv_event_get_user_data(e);
    char id[INBOX_ID_MAX + 1];
    snprintf(id, sizeof(id), "%s", s_detail.id);
    if (b == BTN_DISMISS) {
        inbox_act(id, "dismiss");
        show_list();
    } else if (b == BTN_OPEN) {
        inbox_act(id, "open");
        inbox_ui_close(); // off to the computer
        toast_show(card_tr("Opening it on the computer", "La apro sul computer"), 3, TOAST_INFO);
    } else {
        char index[4];
        snprintf(index, sizeof(index), "%d", b);
        inbox_act(id, index);
        show_list();
    }
}

static void add_words(lv_obj_t *page, int32_t box_h)
{
    lv_obj_t *box = lv_obj_create(page);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, BOX_W, box_h);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, BOX_Y);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 10, 0);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_t *title = card_label(box, &lv_font_montserrat_24, CARD_TEXT, s_detail.title);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(title, BOX_W);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    if (s_detail.text[0] != '\0') {
        lv_obj_t *text = card_label(box, &lv_font_montserrat_20, BODY_COLOR, s_detail.text);
        lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(text, BOX_W);
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    }
}

static void show_detail(const char *id)
{
    const inbox_item_t *it = inbox_find(id);
    if (it == NULL) {
        show_list();
        return;
    }
    s_detail = *it;
    s.detail = true;
    inbox_act(id, "read"); // once, while it was unread
    lv_obj_t *page = new_page();

    // Where it comes from, and the way back.
    char head[INBOX_SOURCE_CHARS + 8];
    snprintf(head, sizeof(head), LV_SYMBOL_LEFT "  %s", s_detail.source[0] ? s_detail.source : "Yoru");
    lv_obj_t *h = card_label(page, &lv_font_montserrat_20, CARD_ACCENT, head);
    lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(h, HEAD_W, lv_font_get_line_height(&lv_font_montserrat_20));
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, HEAD_Y);
    lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(h, 20);
    lv_obj_add_event_cb(h, back_cb, LV_EVENT_CLICKED, NULL);
    char age[16];
    age_text(s_detail.arrived_s, age, sizeof(age));
    lv_obj_t *when = card_label(page, &lv_font_montserrat_14, urgency_color(s_detail.urgency), age);
    lv_obj_align(when, LV_ALIGN_TOP_MID, 0, HEAD_Y + 28);

    const int na = s_detail.nactions;
    add_words(page, (na > 0 ? ACTIONS_Y : BOTTOM_Y) - BOX_Y - 12);
    for (int i = 0; i < na; i++) {
        lv_obj_t *b = card_button(page, BTN_W, BTN_H, CARD_ACCENT, CARD_ROW, s_detail.actions[i],
                                  &lv_font_montserrat_20, button_cb, LV_EVENT_CLICKED, (void *) (intptr_t) i);
        const int32_t dx = na == 1 ? 0 : (i == 0 ? -1 : 1) * (BTN_W / 2 + BTN_GAP / 2);
        lv_obj_align(b, LV_ALIGN_TOP_MID, dx, ACTIONS_Y);
    }
    lv_obj_t *dismiss = card_button(page, BTN_W, BTN_H, CARD_DENY, CARD_DENY_BG,
                                    card_tr(LV_SYMBOL_CLOSE " Dismiss", LV_SYMBOL_CLOSE " Elimina"),
                                    &lv_font_montserrat_20, button_cb, LV_EVENT_CLICKED, (void *) (intptr_t) BTN_DISMISS);
    lv_obj_align(dismiss, LV_ALIGN_TOP_MID, -(BTN_W / 2 + BTN_GAP / 2), BOTTOM_Y);
    lv_obj_t *open = card_button(page, BTN_W, BTN_H, CARD_TEXT, CARD_ROW,
                                 card_tr(LV_SYMBOL_UPLOAD " Open", LV_SYMBOL_UPLOAD " Apri"), &lv_font_montserrat_20,
                                 button_cb, LV_EVENT_CLICKED, (void *) (intptr_t) BTN_OPEN);
    lv_obj_align(open, LV_ALIGN_TOP_MID, BTN_W / 2 + BTN_GAP / 2, BOTTOM_Y);
}

/* ---- The view ---------------------------------------------------------------------- */

void inbox_ui_open(const char *id)
{
    if (s.view == NULL) {
        menu_close(); // one view at a time
        s.view = card_root();
        lv_obj_add_event_cb(s.view, view_gesture_cb, LV_EVENT_GESTURE, NULL);
        s.page = s.list = NULL;
        s.detail = false;
        // No fade: blending the whole screen takes LVGL a layer of its own
        // every frame, and runs at a few frames a second.
        if (s.face != NULL) {
            lv_obj_add_flag(s.face, LV_OBJ_FLAG_HIDDEN);
        }
        ESP_LOGI(TAG, "view open, %d in the inbox", inbox_count());
        if (s.cb != NULL) {
            s.cb(true);
        }
    }
    lv_display_trigger_activity(NULL);
    if (id != NULL && inbox_find(id) != NULL) {
        show_detail(id);
    } else {
        show_list();
    }
}

void inbox_ui_close(void)
{
    if (s.view == NULL) {
        return;
    }
    lv_obj_delete_async(s.view);
    s.view = s.page = s.list = NULL;
    s.detail = false;
    if (s.face != NULL) {
        lv_obj_remove_flag(s.face, LV_OBJ_FLAG_HIDDEN);
    }
    ESP_LOGI(TAG, "view closed");
    if (s.cb != NULL) {
        s.cb(false);
    }
}

bool inbox_ui_is_open(void)
{
    return s.view != NULL;
}

/* ---- Keeping up ------------------------------------------------------------------------- */

static void keep_view(void)
{
    const uint32_t idle = lv_display_get_inactive_time(NULL);
    if (idle >= (s.detail ? DETAIL_IDLE_MS : LIST_IDLE_MS)) {
        inbox_ui_close();
        return;
    }
    const bool changed = inbox_version() != s.version;
    if (s.detail) {
        if (changed && inbox_find(s_detail.id) == NULL) {
            show_list(); // taken away meanwhile
        } else if (changed) {
            s.version = inbox_version();
        }
    } else if (changed || now_ms() - s.built_ms >= AGE_REFRESH_MS) {
        show_list();
    }
}

static void poll_cb(lv_timer_t *t)
{
    const bool hidden = s.view != NULL || menu_is_open() || approval_is_open();
    if (hidden != s.hidden) {
        s.hidden = hidden;
        s.dot_stale = true;
    }
    place_dot();
    update_banner();
    if (s.view != NULL) {
        keep_view();
    }
}

void inbox_ui_init(lv_obj_t *face, inbox_ui_cb_t cb)
{
    s.face = face;
    s.cb = cb;
    s.dot_urgency = INBOX_NONE;
    // On the top layer, not clickable: touches go through to Yoru.
    s.dot = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s.dot);
    lv_obj_set_size(s.dot, DOT_D, DOT_D);
    lv_obj_set_style_radius(s.dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s.dot, LV_OPA_COVER, 0);
    const float a = DOT_DEG * 3.14159265f / 180.0f;
    lv_obj_set_pos(s.dot, CX + (int32_t) lroundf(DOT_R * cosf(a)) - DOT_D / 2,
                   CY + (int32_t) lroundf(DOT_R * sinf(a)) - DOT_D / 2);
    lv_obj_remove_flag(s.dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s.dot, LV_OBJ_FLAG_HIDDEN);

    // The banner takes its taps; the rest of the screen stays Yoru's.
    s.banner = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s.banner);
    lv_obj_set_size(s.banner, BANNER_W, BANNER_H);
    lv_obj_set_style_radius(s.banner, BANNER_H / 2, 0);
    lv_obj_set_style_bg_color(s.banner, CARD_ROW, 0);
    lv_obj_set_style_bg_color(s.banner, CARD_ROW_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(s.banner, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s.banner, 2, 0);
    lv_obj_align(s.banner, LV_ALIGN_TOP_MID, 0, BANNER_Y);
    lv_obj_remove_flag(s.banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s.banner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s.banner, banner_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s.banner, LV_OBJ_FLAG_HIDDEN);
    s.banner_source = card_label(s.banner, &lv_font_montserrat_14, CARD_ACCENT, "");
    lv_label_set_long_mode(s.banner_source, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(s.banner_source, BANNER_W - 48, lv_font_get_line_height(&lv_font_montserrat_14));
    lv_obj_set_style_text_align(s.banner_source, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s.banner_source, LV_ALIGN_TOP_MID, 0, 7);
    s.banner_title = card_label(s.banner, &lv_font_montserrat_20, CARD_TEXT, "");
    lv_label_set_long_mode(s.banner_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(s.banner_title, BANNER_W - 40, lv_font_get_line_height(&lv_font_montserrat_20));
    lv_obj_set_style_text_align(s.banner_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s.banner_title, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_timer_create(poll_cb, POLL_MS, NULL);
}
