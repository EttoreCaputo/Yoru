#include "card.h"

#include <string.h>

#include "board.h"
#include "board_settings.h"

#define RIM_W 6
/* The ring turns red for the last few seconds. */
#define HURRY_S 10

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

static void ascii(const char *in, char *out, size_t chars, bool lines)
{
    const unsigned char *p = (const unsigned char *) (in != NULL ? in : "");
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
        } else if (lines && cp == '\n') {
            rep = "\n";
        } else if (lines && cp == '\r') {
            rep = "";
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
        } else if (cp == 0x2022 || cp == 0xB7) {
            rep = "-";
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

void card_ascii(const char *in, char *out, size_t chars)
{
    ascii(in, out, chars, false);
}

void card_ascii_lines(const char *in, char *out, size_t chars)
{
    ascii(in, out, chars, true);
}

bool card_italian(void)
{
    return board_settings_get(SETTING_LANGUAGE) == 1;
}

/* ---- Pieces --------------------------------------------------------------------- */

lv_obj_t *card_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t *card_root(void)
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
    return o;
}

lv_obj_t *card_rim(lv_obj_t *parent)
{
    lv_obj_t *rim = lv_arc_create(parent);
    lv_obj_remove_style_all(rim);
    lv_obj_set_size(rim, BOARD_LCD_H_RES - 4, BOARD_LCD_V_RES - 4);
    lv_obj_center(rim);
    lv_obj_remove_flag(rim, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(rim, 270);
    lv_arc_set_bg_angles(rim, 0, 360);
    lv_arc_set_angles(rim, 0, 360);
    lv_obj_set_style_arc_width(rim, RIM_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(rim, CARD_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(rim, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(rim, RIM_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(rim, CARD_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(rim, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(rim, true, LV_PART_INDICATOR);
    return rim;
}

int card_rim_set(lv_obj_t *rim, uint32_t left_ms, uint32_t total_ms)
{
    lv_arc_set_angles(rim, 0, (lv_value_precise_t) (360.0f * (float) left_ms / (float) total_ms));
    const int left_s = (int) ((left_ms + 999) / 1000);
    if (left_s <= HURRY_S) {
        lv_obj_set_style_arc_color(rim, CARD_HURRY, LV_PART_INDICATOR);
    }
    return left_s;
}

lv_obj_t *card_button(lv_obj_t *parent, int32_t w, int32_t h, lv_color_t color, lv_color_t bg, const char *text,
                      const lv_font_t *font, lv_event_cb_t cb, lv_event_code_t code, void *user)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_color(b, color, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, color, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    if (cb != NULL) {
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(b, 10);
        lv_obj_add_event_cb(b, cb, code, user);
    } else {
        lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_t *l = card_label(b, font, color, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(l, w - h / 2, lv_font_get_line_height(font)); // one line
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    return b;
}
