#include "recmark.h"

#include <math.h>
#include <stdio.h>

#include "approval.h"
#include "icons.h"
#include "inbox_ui.h"
#include "lvgl.h"
#include "menu.h"

#define PILL_W 74
#define PILL_H 32
#define PILL_Y 18
#define ICON_PX 22
#define PULSE_MS 80
#define BREATH_MS 1600

static struct {
    lv_obj_t *pill;
    lv_timer_t *timer;
    bool on;
    char source[ICON_NAME_MAX + 1];
    uint32_t rgb;
    char glyph[8];
} s;

static void draw_cb(lv_event_t *ev)
{
    lv_layer_t *layer = lv_event_get_layer(ev);
    lv_area_t a;
    lv_obj_get_coords(s.pill, &a);
    const float cy = (a.y1 + a.y2 + 1) / 2.0f;
    // The red dot breathes: a soft halo that swells, a core that dims a little.
    const float k = 0.5f + 0.5f * sinf(2 * (float) M_PI * (float) (lv_tick_get() % BREATH_MS) / BREATH_MS);
    const float dx = a.x1 + 19;
    lv_draw_rect_dsc_t dot;
    lv_draw_rect_dsc_init(&dot);
    dot.radius = LV_RADIUS_CIRCLE;
    dot.bg_color = lv_color_hex(0xFF3B30);
    const int32_t halo = (int32_t) (8 + 3 * k);
    dot.bg_opa = (lv_opa_t) (50 + 40 * k);
    lv_area_t d = {(int32_t) dx - halo, (int32_t) cy - halo, (int32_t) dx + halo, (int32_t) cy + halo};
    lv_draw_rect(layer, &dot, &d);
    dot.bg_opa = (lv_opa_t) (190 + 65 * k);
    d = (lv_area_t) {(int32_t) dx - 6, (int32_t) cy - 6, (int32_t) dx + 6, (int32_t) cy + 6};
    lv_draw_rect(layer, &dot, &d);
    // What is recorded.
    const float ix = a.x2 - 22;
    const lv_image_dsc_t *icon = icons_find(s.source);
    if (icon != NULL) {
        icons_draw(layer, icon, ix, cy, ICON_PX, LV_OPA_COVER);
        return;
    }
    lv_draw_label_dsc_t t;
    lv_draw_label_dsc_init(&t);
    t.font = &lv_font_montserrat_20;
    t.color = lv_color_hex(s.rgb);
    t.align = LV_TEXT_ALIGN_CENTER;
    t.text = s.glyph[0] != '\0' ? s.glyph : LV_SYMBOL_AUDIO;
    const int32_t lh = lv_font_get_line_height(t.font);
    const lv_area_t ta = {(int32_t) ix - 16, (int32_t) cy - lh / 2, (int32_t) ix + 16, (int32_t) cy + lh / 2};
    lv_draw_label(layer, &t, &ta);
}

static void timer_cb(lv_timer_t *t)
{
    const bool hidden = !s.on || menu_is_open() || inbox_ui_is_open() || approval_is_open();
    if (hidden) {
        lv_obj_add_flag(s.pill, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s.pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(s.pill);
}

void recmark_init(void)
{
    // On the top layer, not clickable: touches go through to Yoru.
    s.pill = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s.pill);
    lv_obj_set_size(s.pill, PILL_W, PILL_H);
    lv_obj_set_style_radius(s.pill, PILL_H / 2, 0);
    lv_obj_set_style_bg_color(s.pill, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s.pill, LV_OPA_60, 0);
    lv_obj_align(s.pill, LV_ALIGN_TOP_MID, 0, PILL_Y);
    lv_obj_remove_flag(s.pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s.pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s.pill, draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    s.timer = lv_timer_create(timer_cb, PULSE_MS, NULL);
    lv_timer_pause(s.timer);
}

void recmark_set(const char *source, uint32_t rgb, const char *glyph)
{
    s.on = source != NULL;
    if (s.on) {
        snprintf(s.source, sizeof(s.source), "%s", source);
        s.rgb = rgb;
        snprintf(s.glyph, sizeof(s.glyph), "%s", glyph != NULL ? glyph : "");
        lv_timer_resume(s.timer);
    } else {
        lv_timer_pause(s.timer);
    }
    timer_cb(s.timer);
}

bool recmark_shown(void)
{
    return s.on;
}
