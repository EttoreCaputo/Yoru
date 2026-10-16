#include "approval.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "board_settings.h"
#include "cJSON.h"
#include "esp_log.h"
#include "menu.h"
#include "rpc.h"

static const char *TAG = "approval";

/* The protocol's limit for `text`, in characters. */
#define TEXT_CHARS 72
#define TOOL_CHARS 40
#define PROJECT_CHARS 40
/* The longest wait a card runs down; the host uses 120 s. */
#define SECONDS_MAX 3600
#define TICK_MS 250
/* The ring turns red for the last few seconds. */
#define HURRY_S 10
#define RIM_W 6
#define BTN_W 150
#define BTN_H 64
/* The card fits around Yoru, shrunk in the middle: the words above it,
 * the buttons below. */
#define HEAD_Y 48
#define HEAD_W 290
#define TEXT_Y 86
#define TEXT_W 330
#define TEXT_H 66
#define BTN_Y 306
#define LEFT_Y 384
#define LEFT_W 290

#define COL_TEXT lv_color_hex(0xF2F2F5)
#define COL_DIM lv_color_hex(0x8C90A6)
#define COL_ACCENT lv_color_hex(0x7FA7FF)
#define COL_TRACK lv_color_hex(0x16171D)
#define COL_HURRY lv_color_hex(0xFF6B7A)
#define COL_ALLOW lv_color_hex(0x3DDC84)
#define COL_ALLOW_BG lv_color_hex(0x163522)
#define COL_DENY lv_color_hex(0xFF6B7A)
#define COL_DENY_BG lv_color_hex(0x3A161C)

static struct {
    approval_cb_t cb;
    lv_obj_t *card;
    lv_obj_t *rim;
    lv_obj_t *left;
    lv_timer_t *timer;
    char id[APPROVAL_ID_MAX + 1];
    char project[PROJECT_CHARS + 1];
    uint32_t start_ms;
    uint32_t total_ms;
    int shown_s;
} s;

/* ---- Text the fonts can draw ------------------------------------------------ */

/* U+00C0..U+00FF without their accents: the built-in fonts are ASCII. */
static const char LATIN1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"
                             "aaaaaaaceeeeiiiidnooooo/ouuuuyty";

/* One code point from `p`, or -1 for a broken sequence; `*len` its bytes. */
static int32_t decode(const unsigned char *p, int *len)
{
    int n = p[0] < 0x80 ? 1 : (p[0] & 0xE0) == 0xC0 ? 2 : (p[0] & 0xF0) == 0xE0 ? 3 : (p[0] & 0xF8) == 0xF0 ? 4 : 0;
    if (n == 0) {
        *len = 1;
        return -1;
    }
    int32_t cp = n == 1 ? p[0] : p[0] & (0x7F >> n);
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *len = i;
            return -1;
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *len = n;
    return cp;
}

/* `in` as printable ASCII in `out`, at most `chars` characters, with "..."
 * when it had more. */
static void to_ascii(const char *in, char *out, size_t chars)
{
    const unsigned char *p = (const unsigned char *) in;
    size_t n = 0;
    while (*p != '\0' && n < chars) {
        int len;
        const int32_t cp = decode(p, &len);
        p += len;
        const char *rep = "?";
        char one[2] = {0};
        if (cp >= 0x20 && cp < 0x7F) {
            one[0] = (char) cp;
            rep = one;
        } else if (cp == '\t' || cp == '\n' || cp == '\r' || cp == 0xA0) {
            rep = " ";
        } else if (cp >= 0xC0 && cp <= 0xFF) {
            one[0] = LATIN1[cp - 0xC0];
            rep = one;
        } else if (cp == 0x2018 || cp == 0x2019) {
            rep = "'";
        } else if (cp == 0x201C || cp == 0x201D || cp == 0xAB || cp == 0xBB) {
            rep = "\"";
        } else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212) {
            rep = "-";
        } else if (cp == 0x2026) {
            rep = "...";
        } else if (cp == 0x2192) {
            rep = "->";
        }
        for (; *rep != '\0' && n < chars; rep++) {
            out[n++] = *rep;
        }
    }
    if (*p != '\0' && n >= 3) {
        memcpy(out + n - 3, "...", 3);
    }
    out[n] = '\0';
}

/* ---- The card ------------------------------------------------------------------ */

static bool italian(void)
{
    return board_settings_get(SETTING_LANGUAGE) == 1;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

static void close_card(int allow)
{
    if (s.card == NULL) {
        return;
    }
    lv_obj_delete_async(s.card);
    s.card = s.rim = s.left = NULL;
    if (s.timer != NULL) {
        lv_timer_delete(s.timer);
        s.timer = NULL;
    }
    ESP_LOGI(TAG, "%s closed (%s)", s.id, allow > 0 ? "allowed" : allow == 0 ? "denied" : "no answer");
    s.id[0] = '\0';
    if (s.cb != NULL) {
        s.cb(false, allow);
    }
}

static void answer_cb(lv_event_t *e)
{
    const bool allow = (bool) (intptr_t) lv_event_get_user_data(e);
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "id", s.id);
    cJSON_AddBoolToObject(params, "allow", allow);
    rpc_notify("approval", params);
    close_card(allow ? 1 : 0);
}

static void tick_cb(lv_timer_t *t)
{
    const uint32_t gone = lv_tick_elaps(s.start_ms);
    if (gone >= s.total_ms) {
        close_card(-1); // the host asks in the terminal now
        return;
    }
    const uint32_t left_ms = s.total_ms - gone;
    lv_arc_set_angles(s.rim, 0, (lv_value_precise_t) (360.0f * (float) left_ms / (float) s.total_ms));
    const int left_s = (int) ((left_ms + 999) / 1000);
    if (left_s != s.shown_s) {
        s.shown_s = left_s;
        if (s.project[0] != '\0') {
            lv_label_set_text_fmt(s.left, "%s  " LV_SYMBOL_BULLET "  %d s", s.project, left_s);
        } else {
            lv_label_set_text_fmt(s.left, "%d s", left_s);
        }
        if (left_s <= HURRY_S) {
            lv_obj_set_style_arc_color(s.rim, COL_HURRY, LV_PART_INDICATOR);
            lv_obj_set_style_text_color(s.left, COL_HURRY, 0);
        }
    }
}

static lv_obj_t *button(lv_obj_t *parent, bool allow)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, BTN_W, BTN_H);
    lv_obj_set_style_radius(b, BTN_H / 2, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, allow ? COL_ALLOW_BG : COL_DENY_BG, 0);
    lv_obj_set_style_bg_color(b, allow ? COL_ALLOW : COL_DENY, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, allow ? COL_ALLOW : COL_DENY, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(b, 12);
    lv_obj_add_event_cb(b, answer_cb, LV_EVENT_CLICKED, (void *) (intptr_t) allow);
    const char *text = allow ? (italian() ? LV_SYMBOL_OK " Consenti" : LV_SYMBOL_OK " Allow")
                             : (italian() ? LV_SYMBOL_CLOSE " Nega" : LV_SYMBOL_CLOSE " Deny");
    lv_obj_t *l = label(b, &lv_font_montserrat_24, allow ? COL_ALLOW : COL_DENY, text);
    lv_obj_center(l);
    return b;
}

static void build(const char *tool, const char *text, const char *project)
{
    // See-through, so Yoru shows in the middle, but it takes every touch:
    // no pokes or menu while a request waits.
    s.card = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s.card);
    lv_obj_set_size(s.card, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_center(s.card);
    lv_obj_add_flag(s.card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s.card, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // The time left, along the rim from the top.
    s.rim = lv_arc_create(s.card);
    lv_obj_remove_style_all(s.rim);
    lv_obj_set_size(s.rim, BOARD_LCD_H_RES - 4, BOARD_LCD_V_RES - 4);
    lv_obj_center(s.rim);
    lv_obj_remove_flag(s.rim, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(s.rim, 270);
    lv_arc_set_bg_angles(s.rim, 0, 360);
    lv_arc_set_angles(s.rim, 0, 360);
    lv_obj_set_style_arc_width(s.rim, RIM_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s.rim, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s.rim, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s.rim, RIM_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s.rim, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s.rim, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s.rim, true, LV_PART_INDICATOR);

    // Which tool, on one line.
    char buf[TEXT_CHARS + 1];
    to_ascii(tool, buf, TOOL_CHARS);
    lv_obj_t *head = label(s.card, &lv_font_montserrat_24, COL_ACCENT, buf);
    lv_label_set_long_mode(head, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(head, HEAD_W, lv_font_get_line_height(&lv_font_montserrat_24));
    lv_obj_set_style_text_align(head, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, HEAD_Y);
    to_ascii(project != NULL ? project : "", s.project, PROJECT_CHARS);

    // What it would do.
    to_ascii(text, buf, TEXT_CHARS);
    lv_obj_t *what = label(s.card, &lv_font_montserrat_20, COL_TEXT, buf);
    lv_label_set_long_mode(what, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(what, TEXT_W, TEXT_H);
    lv_obj_set_style_text_align(what, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(what, LV_ALIGN_TOP_MID, 0, TEXT_Y);

    lv_obj_t *deny = button(s.card, false);
    lv_obj_align(deny, LV_ALIGN_TOP_MID, -(BTN_W / 2 + 8), BTN_Y);
    lv_obj_t *allow = button(s.card, true);
    lv_obj_align(allow, LV_ALIGN_TOP_MID, BTN_W / 2 + 8, BTN_Y);

    // The project and the seconds left, under the buttons.
    s.left = label(s.card, &lv_font_montserrat_20, COL_DIM, "");
    lv_label_set_long_mode(s.left, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(s.left, LEFT_W, lv_font_get_line_height(&lv_font_montserrat_20));
    lv_obj_set_style_text_align(s.left, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s.left, LV_ALIGN_TOP_MID, 0, LEFT_Y);
}

/* ---- Calls ------------------------------------------------------------------------ */

void approval_init(approval_cb_t cb)
{
    s.cb = cb;
}

void approval_show(const char *id, const char *tool, const char *text, const char *project, int seconds)
{
    const bool replacing = s.card != NULL;
    if (replacing) {
        // Straight to the new one: Yoru keeps waiting.
        lv_obj_delete_async(s.card);
        s.card = s.rim = s.left = NULL;
        lv_timer_delete(s.timer);
        s.timer = NULL;
        ESP_LOGI(TAG, "%s replaced", s.id);
    }
    // The card takes the screen: the menu goes.
    menu_close();
    snprintf(s.id, sizeof(s.id), "%s", id);
    s.total_ms = (uint32_t) (seconds < 1 ? 1 : seconds > SECONDS_MAX ? SECONDS_MAX : seconds) * 1000;
    s.start_ms = lv_tick_get();
    s.shown_s = -1;
    build(tool, text, project);
    lv_obj_fade_in(s.card, 180, 0);
    s.timer = lv_timer_create(tick_cb, TICK_MS, NULL);
    tick_cb(s.timer);
    ESP_LOGI(TAG, "%s: %s, %d s", s.id, tool, seconds);
    if (!replacing && s.cb != NULL) {
        s.cb(true, -1);
    }
}

bool approval_hide(const char *id)
{
    if (s.card == NULL || strcmp(id, s.id) != 0) {
        return false;
    }
    close_card(-1);
    return true;
}

bool approval_is_open(void)
{
    return s.card != NULL;
}
