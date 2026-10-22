#include "game_kit.h"

#include <math.h>
#include <string.h>

#include "esp_random.h"
#include "look.h"
#include "lvgl_port.h"
#include "motion.h"
#include "nvs.h"

#define EYE_COLOR lv_color_hex(0x1D1B24)
#define NVS_NAMESPACE "games"

static struct {
    uint32_t ball_frame;
    float rest[2];
    uint32_t blink_ms, next_blink_ms;
} k;

/* ---- Yoru -------------------------------------------------------------------- */

static void eye_pill(lv_layer_t *layer, float x, float y, float hw, float hh)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = EYE_COLOR;
    dsc.radius = LV_RADIUS_CIRCLE;
    const lv_area_t a = {(int32_t) lroundf(x - hw), (int32_t) lroundf(y - hh), (int32_t) lroundf(x + hw),
                         (int32_t) lroundf(y + hh)};
    lv_draw_rect(layer, &dsc, &a);
}

/* The eyes as in the menu's bubble (menu.c, draw_yoru), which are the face's
 * under the menu: they slide over the surface and narrow near its edge. */
static void draw_face(lv_layer_t *layer, float x, float y, float rx, float ry, const kit_face_t *f)
{
    const float r = fminf(rx, ry);
    const float gx = kit_clamp(f->gx, -1, 1), gy = kit_clamp(f->gy, -1, 1);
    const float nx = 1 - 0.3f * gx * gx, ny = 1 - 0.25f * gy * gy;
    const float ey = y + (0.114f + 0.343f * gy) * ry;
    for (int side = -1; side <= 1; side += 2) {
        const float ex = x + ((float) side * 0.229f * nx + 0.443f * gx) * rx;
        switch (f->eyes) {
        case KIT_EYES_WIDE:
            eye_pill(layer, ex, ey, fmaxf(1.5f, 0.11f * r), fmaxf(1.5f, 0.12f * r));
            break;
        case KIT_EYES_HAPPY:
            kit_arc(layer, ex, ey + 0.04f * r, fmaxf(3, 0.12f * r), fmaxf(1.5f, 0.055f * r), 195, 345, EYE_COLOR);
            break;
        case KIT_EYES_SHUT:
            eye_pill(layer, ex, ey, fmaxf(2, 0.1f * r), fmaxf(1, 0.03f * r));
            break;
        case KIT_EYES_SAD:
            eye_pill(layer, ex, ey + 0.06f * r, fmaxf(1, 0.071f * r * nx), fmaxf(1, 0.09f * r * ny * f->lid));
            break;
        default:
            eye_pill(layer, ex, ey, fmaxf(1, 0.071f * r * nx), fmaxf(1, 0.157f * r * ny * f->lid));
            break;
        }
    }
}

void kit_yoru(lv_layer_t *layer, float x, float y, float rx, float ry, const kit_face_t *face)
{
    // Even sizes only: fewer spheres to render and keep (look_ball).
    const int qx = LV_MAX(2, 2 * (int) lroundf(rx / 2)), qy = LV_MAX(2, 2 * (int) lroundf(ry / 2));
    const look_sprite_t *sp = look_ball(qx, qy, ++k.ball_frame);
    if (sp == NULL) {
        return;
    }
    look_draw(layer, sp, (int32_t) lroundf(x), (int32_t) lroundf(y), 1, 1, 0, LV_OPA_COVER, lv_color_black());
    if (face != NULL && qx >= 6) {
        draw_face(layer, x, y, (float) qx, (float) qy, face);
    }
}

lv_area_t kit_yoru_area(float x, float y, float rx, float ry)
{
    // The sprite has a little margin around the sphere.
    return (lv_area_t) {(int32_t) floorf(x - rx) - 4, (int32_t) floorf(y - ry) - 4, (int32_t) ceilf(x + rx) + 4,
                        (int32_t) ceilf(y + ry) + 4};
}

float kit_blink(uint32_t now)
{
    if (k.next_blink_ms == 0 || (int32_t) (now - k.next_blink_ms) >= 0) {
        k.blink_ms = now;
        k.next_blink_ms = now + 2000 + esp_random() % 3000;
    }
    const uint32_t t = now - k.blink_ms;
    return t < 70 ? 1 - (float) t / 70 : t < 140 ? (float) (t - 70) / 70 : 1;
}

/* ---- Drawing --------------------------------------------------------------- */

void kit_disc(lv_layer_t *layer, float x, float y, float r, lv_color_t color)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.radius = LV_RADIUS_CIRCLE;
    const lv_area_t a = {(int32_t) lroundf(x - r), (int32_t) lroundf(y - r), (int32_t) lroundf(x + r) - 1,
                         (int32_t) lroundf(y + r) - 1};
    lv_draw_rect(layer, &dsc, &a);
}

void kit_rect(lv_layer_t *layer, const lv_area_t *a, int32_t radius, lv_color_t color)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.radius = radius;
    lv_draw_rect(layer, &dsc, a);
}

void kit_arc(lv_layer_t *layer, float x, float y, float r, float w, int32_t a0, int32_t a1, lv_color_t color)
{
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center.x = (int32_t) lroundf(x);
    dsc.center.y = (int32_t) lroundf(y);
    dsc.radius = (uint16_t) lroundf(r);
    dsc.width = (int32_t) fmaxf(1, lroundf(w));
    dsc.start_angle = a0;
    dsc.end_angle = a1;
    dsc.rounded = a1 - a0 < 360;
    dsc.color = color;
    lv_draw_arc(layer, &dsc);
}

lv_area_t kit_text_area(const lv_font_t *font, float x, float y, const char *str)
{
    lv_point_t size;
    lv_text_get_size(&size, str, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const int32_t w = size.x / 2 + 2, h = lv_font_get_line_height(font);
    return (lv_area_t) {(int32_t) x - w, (int32_t) y - h / 2, (int32_t) x + w, (int32_t) y - h / 2 + h};
}

void kit_text(lv_layer_t *layer, const lv_font_t *font, float x, float y, lv_color_t color, const char *str)
{
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = font;
    dsc.color = color;
    dsc.text = str;
    dsc.align = LV_TEXT_ALIGN_CENTER;
    const lv_area_t a = kit_text_area(font, x, y, str);
    lv_draw_label(layer, &dsc, &a);
}

lv_color_t kit_dim(lv_color_t c, float k)
{
    return lv_color_mix(c, lv_color_black(), (uint8_t) (255 * kit_clamp(k, 0, 1)));
}

/* ---- Dirty areas ----------------------------------------------------------- */

lv_area_t kit_disc_area(float x, float y, float r)
{
    return (lv_area_t) {(int32_t) floorf(x - r) - 1, (int32_t) floorf(y - r) - 1, (int32_t) ceilf(x + r) + 1,
                        (int32_t) ceilf(y + r) + 1};
}

bool kit_overlap(const lv_area_t *a, const lv_area_t *b)
{
    return a->x1 <= b->x2 && b->x1 <= a->x2 && a->y1 <= b->y2 && b->y1 <= a->y2;
}

void kit_dirty(lv_obj_t *obj, lv_area_t a)
{
    a.x1 = LV_MAX(0, a.x1);
    a.y1 = LV_MAX(0, a.y1);
    a.x2 = LV_MIN(BOARD_LCD_H_RES - 1, a.x2);
    a.y2 = LV_MIN(BOARD_LCD_V_RES - 1, a.y2);
    if (a.x1 <= a.x2 && a.y1 <= a.y2) {
        lv_obj_invalidate_area(obj, &a);
    }
}

void kit_moved(lv_obj_t *obj, lv_area_t *drawn, lv_area_t now)
{
    const lv_area_t was = *drawn;
    if (was.x1 > was.x2) {
        kit_dirty(obj, now); // nothing drawn yet
    } else if (kit_overlap(&was, &now)) {
        kit_dirty(obj, (lv_area_t) {LV_MIN(was.x1, now.x1), LV_MIN(was.y1, now.y1), LV_MAX(was.x2, now.x2),
                                    LV_MAX(was.y2, now.y2)});
    } else {
        kit_dirty(obj, was);
        kit_dirty(obj, now);
    }
    *drawn = now;
}

/* ---- Tilt ------------------------------------------------------------------ */

static void gravity(float out[2])
{
    float g[3], w[3];
    motion_raw(g, w);
    out[0] = g[0];
    out[1] = g[1];
}

void kit_tilt_zero(void)
{
    gravity(k.rest);
}

void kit_tilt(float *x, float *y)
{
    float g[2];
    gravity(g);
    const float px = g[0] - k.rest[0], py = g[1] - k.rest[1];
    // From the panel's axes to the picture's, as Yoru's lean (behaviour.c).
    switch (lvgl_port_rotation()) {
    case 1:
        *x = -py;
        *y = px;
        break;
    case 2:
        *x = -px;
        *y = -py;
        break;
    case 3:
        *x = py;
        *y = -px;
        break;
    default:
        *x = px;
        *y = py;
        break;
    }
}

/* ---- Saved numbers ------------------------------------------------------------ */

int32_t kit_load(const char *key)
{
    nvs_handle_t nvs;
    int32_t v = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_i32(nvs, key, &v);
        nvs_close(nvs);
    }
    return v;
}

void kit_save(const char *key, int32_t value)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_i32(nvs, key, value);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}
