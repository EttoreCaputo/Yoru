#pragma once

/* What the cards over the eyes share (approval.c, inbox_ui.c, timers_ui.c...):
 * the palette, text the built-in fonts can draw, a see-through root that
 * takes every touch, the countdown along the rim and the pill buttons. All
 * calls with the LVGL lock held. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CARD_TEXT lv_color_hex(0xF2F2F5)
#define CARD_DIM lv_color_hex(0x8C90A6)
#define CARD_ACCENT lv_color_hex(0x7FA7FF)
#define CARD_TRACK lv_color_hex(0x16171D)
#define CARD_ROW lv_color_hex(0x16171D)
#define CARD_ROW_PRESSED lv_color_hex(0x262833)
#define CARD_HURRY lv_color_hex(0xFF6B7A)
#define CARD_ALLOW lv_color_hex(0x3DDC84)
#define CARD_ALLOW_BG lv_color_hex(0x163522)
#define CARD_DENY lv_color_hex(0xFF6B7A)
#define CARD_DENY_BG lv_color_hex(0x3A161C)
#define CARD_WAIT lv_color_hex(0xFFB547)
#define CARD_WAIT_BG lv_color_hex(0x3A2A10)

/** `in` (UTF-8) as printable ASCII in `out`, at most `chars` characters
 * (`out` holds chars + 1), with "..." when it had more: the built-in fonts
 * are ASCII, so accents go and what can't be spelt shows as `?`. */
void card_ascii(const char *in, char *out, size_t chars);
/** The same, keeping the line breaks: a reply's paragraphs. */
void card_ascii_lines(const char *in, char *out, size_t chars);

/** The labels' language: SETTING_LANGUAGE is Italian. */
bool card_italian(void);
static inline const char *card_tr(const char *en, const char *it)
{
    return card_italian() ? it : en;
}

lv_obj_t *card_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);

/** A full-screen root on the top layer that takes every touch and swipe,
 * so nothing reaches Yoru underneath. It has no background: a view that
 * covers Yoru hides him and lets the screen's own black show, instead of
 * filling every redrawn area with black a second time. */
lv_obj_t *card_root(void);

/** The time left, as a ring along the rim from the top. */
lv_obj_t *card_rim(lv_obj_t *parent);
/** Run it down; red for the last few seconds. Returns the seconds left. */
int card_rim_set(lv_obj_t *rim, uint32_t left_ms, uint32_t total_ms);

/** A pill button: `color` for the text and the border, `bg` behind it, the
 * colour itself while pressed. `cb` gets `code` events with `user`. */
lv_obj_t *card_button(lv_obj_t *parent, int32_t w, int32_t h, lv_color_t color, lv_color_t bg, const char *text,
                      const lv_font_t *font, lv_event_cb_t cb, lv_event_code_t code, void *user);

#ifdef __cplusplus
}
#endif
