#include "toast.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "approval.h"
#include "board.h"
#include "card.h"
#include "inbox_ui.h"
#include "lvgl.h"
#include "menu.h"

#define POLL_MS 200
#define LINE_W 290
#define LINE_H 34
#define LINE_Y (BOARD_LCD_V_RES - 92)
#define TOAST_CHARS 72
#define TOAST_MAX_S 30

static struct {
    lv_obj_t *line;
    lv_obj_t *text;
    bool hidden;
    /* Up until then, 0 for none. Kept, so one that comes while the menu is
     * up shows once it goes. */
    uint32_t until_ms;
    char toast[TOAST_CHARS + 1];
} s;

static lv_color_t tone_color(toast_tone_t tone)
{
    return tone == TOAST_DONE ? CARD_ALLOW : tone == TOAST_WAIT ? CARD_WAIT : tone == TOAST_ERROR ? CARD_HURRY : CARD_TEXT;
}

static bool up(void)
{
    return s.until_ms != 0 && (int32_t) (lv_tick_get() - s.until_ms) < 0;
}

static void update(void)
{
    if (!up() || s.hidden) {
        lv_obj_add_flag(s.line, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s.line, LV_OBJ_FLAG_HIDDEN);
}

void toast_show(const char *text, int seconds, toast_tone_t tone)
{
    card_ascii(text, s.toast, TOAST_CHARS);
    lv_label_set_text(s.text, s.toast);
    lv_obj_set_style_text_color(s.text, tone_color(tone), 0);
    seconds = seconds < 1 ? 1 : seconds > TOAST_MAX_S ? TOAST_MAX_S : seconds;
    s.until_ms = lv_tick_get() + (uint32_t) seconds * 1000;
    if (s.until_ms == 0) {
        s.until_ms = 1; // 0 means none
    }
    update();
    if (!s.hidden) {
        lv_obj_fade_in(s.line, 150, 0);
    }
}

static void poll_cb(lv_timer_t *t)
{
    // Out of the way of whatever else has the screen.
    s.hidden = menu_is_open() || inbox_ui_is_open() || approval_is_open();
    if (s.until_ms != 0 && !up()) {
        s.until_ms = 0;
    }
    update();
}

void toast_init(void)
{
    // On the top layer, not clickable: touches go through to Yoru.
    s.line = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s.line);
    lv_obj_set_size(s.line, LINE_W, LINE_H);
    lv_obj_set_style_radius(s.line, LINE_H / 2, 0);
    lv_obj_set_style_bg_color(s.line, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s.line, LV_OPA_70, 0);
    lv_obj_align(s.line, LV_ALIGN_TOP_MID, 0, LINE_Y);
    lv_obj_remove_flag(s.line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s.line, LV_OBJ_FLAG_HIDDEN);
    s.text = card_label(s.line, &lv_font_montserrat_20, CARD_TEXT, "");
    lv_label_set_long_mode(s.text, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(s.text, LINE_W - 28, lv_font_get_line_height(&lv_font_montserrat_20));
    lv_obj_set_style_text_align(s.text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s.text);
    lv_timer_create(poll_cb, POLL_MS, NULL);
}
