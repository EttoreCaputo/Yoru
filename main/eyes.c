#include "eyes.h"

#include <math.h>
#include <string.h>

#include "board.h"
#include "esp_random.h"

/* One frame every 33 ms; when nothing moves, nothing is redrawn. */
#define EYES_FRAME_MS 33
#define CX (BOARD_LCD_H_RES / 2)
#define CY (BOARD_LCD_V_RES / 2)
/* Eye centres from the middle, and how far the gaze moves them. */
#define EYE_SPACING 80
#define GAZE_X 52
#define GAZE_Y 38

/* What a mood looks like. The lids: `top` cuts the eye from above (0..1 of
 * its height), `slope` tilts that cut (> 0: lower towards the nose, angry;
 * < 0: lower outside, worried), `bottom` raises a curve from below (the
 * ^ ^ of a smile). `tilt` turns the face, in degrees; `left`/`right` scale
 * each eye's height. */
typedef struct {
    float r, g, b;
    float w, h;
    float top, slope, bottom;
    float tilt;
    float left, right;
} eye_shape_t;

#define RGB(hex) (float) (((hex) >> 16) & 0xFF), (float) (((hex) >> 8) & 0xFF), (float) ((hex) & 0xFF)

static const eye_shape_t s_moods[EYES_MOOD_COUNT] = {
    [EYES_MOOD_NEUTRAL] = {RGB(0x3EE6FF), 84, 122, 0, 0, 0, 0, 1, 1},
    [EYES_MOOD_HAPPY] = {RGB(0x3EE6FF), 93, 116, 0, 0, 1, 0, 1, 1},
    [EYES_MOOD_CURIOUS] = {RGB(0x3EE6FF), 87, 122, 0, 0, 0, -9, 1.15f, 0.72f},
    [EYES_MOOD_THINKING] = {RGB(0xE8915F), 76, 102, 0.18f, -0.12f, 0, 5, 1, 1},
    [EYES_MOOD_GRUMPY] = {RGB(0xFF5A3C), 87, 108, 0.32f, 0.38f, 0, 0, 1, 1},
    [EYES_MOOD_HOT] = {RGB(0xFF8A2A), 84, 105, 0.30f, -0.32f, 0, 0, 1, 1},
    [EYES_MOOD_SLEEPY] = {RGB(0x288CAF), 90, 116, 0.58f, 0, 0, 0, 1, 1},
    [EYES_MOOD_ASLEEP] = {RGB(0x1E6E8A), 90, 116, 0.88f, 0, 0, 0, 1, 1},
};

static const char *const s_mood_names[EYES_MOOD_COUNT] = {
    "neutral", "happy", "curious", "thinking", "grumpy", "hot", "sleepy", "asleep",
};

static struct {
    lv_obj_t *obj;
    eyes_mood_t mood;
    eye_shape_t cur;
    /* Gaze, -1..1, and where it is going. */
    float gx, gy, tgx, tgy;
    bool held;            // eyes_look_at() in charge
    int32_t next_look_ms; // until the next saccade
    /* Blink: 1 open, ~0 closed. */
    float lid;
    int32_t blink_ms;     // > 0 while closing
    int32_t next_blink_ms;
    float squash;
    uint32_t t_ms;        // animation clock for the overlays
    lv_area_t drawn;      // what the last frame covered
    bool drawn_valid;
} s;

static float lerp(float a, float b, float k)
{
    return a + (b - a) * k;
}

static float frand(void)
{
    return (float) (esp_random() & 0xFFFF) / 65535.0f;
}

static bool has_overlay(eyes_mood_t mood)
{
    return mood == EYES_MOOD_THINKING || mood == EYES_MOOD_HOT || mood == EYES_MOOD_ASLEEP;
}

/* Where an eye is this frame. */
typedef struct {
    float cx, cy, w, h;
    int inner; // +1 for the left eye (its inner side is to the right)
} eye_box_t;

static void eye_boxes(eye_box_t out[2])
{
    const float w = s.cur.w * (1 + 0.3f * s.squash);
    const float h = s.cur.h * (1 - 0.38f * s.squash) * s.lid;
    const float lift = EYE_SPACING * sinf(s.cur.tilt * (float) M_PI / 180.0f);
    const float ox = s.gx * GAZE_X;
    const float oy = s.gy * GAZE_Y;
    out[0] = (eye_box_t) {CX - EYE_SPACING + ox, CY + oy + lift, w, h * s.cur.left, 1};
    out[1] = (eye_box_t) {CX + EYE_SPACING + ox, CY + oy - lift, w, h * s.cur.right, -1};
}

static lv_color_t cur_color(float scale)
{
    return lv_color_make((uint8_t) (s.cur.r * scale), (uint8_t) (s.cur.g * scale), (uint8_t) (s.cur.b * scale));
}

static void fill_rect(lv_layer_t *layer, float x1, float y1, float x2, float y2, lv_color_t color, int32_t radius,
                      lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = radius;
    const lv_area_t a = {(int32_t) x1, (int32_t) y1, (int32_t) x2, (int32_t) y2};
    lv_draw_rect(layer, &dsc, &a);
}

static void fill_triangle(lv_layer_t *layer, float x0, float y0, float x1, float y1, float x2, float y2,
                          lv_color_t color)
{
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = color;
    dsc.opa = LV_OPA_COVER;
    dsc.p[0].x = (int32_t) x0;
    dsc.p[0].y = (int32_t) y0;
    dsc.p[1].x = (int32_t) x1;
    dsc.p[1].y = (int32_t) y1;
    dsc.p[2].x = (int32_t) x2;
    dsc.p[2].y = (int32_t) y2;
    lv_draw_triangle(layer, &dsc);
}

static void draw_eye(lv_layer_t *layer, const eye_box_t *e)
{
    const lv_color_t black = lv_color_black();
    const float x0 = e->cx - e->w / 2;
    const float x1 = e->cx + e->w / 2;
    const float top = e->cy - e->h / 2;
    const float h = e->h < 2 ? 2 : e->h;
    fill_rect(layer, x0, top, x1, top + h, cur_color(1), (int32_t) (fminf(e->w, h) * 0.38f), LV_OPA_COVER);

    // Top lid: everything above a line that can slope towards the nose.
    const float edge_l = top + s.cur.top * h - s.cur.slope * h * 0.6f * (float) e->inner;
    const float edge_r = top + s.cur.top * h + s.cur.slope * h * 0.6f * (float) e->inner;
    if (fmaxf(edge_l, edge_r) > top + 0.5f) {
        const float flat = fminf(edge_l, edge_r);
        const float lx = x0 - 6;
        const float rx = x1 + 6;
        if (flat > top) {
            fill_rect(layer, lx, top - 4, rx, flat, black, 0, LV_OPA_COVER);
        }
        if (edge_l > edge_r) {
            fill_triangle(layer, lx, flat, rx, flat, lx, edge_l, black);
        } else if (edge_r > edge_l) {
            fill_triangle(layer, lx, flat, rx, flat, rx, edge_r, black);
        }
    }

    // Bottom lid: a big circle rising from below draws the smile's curve.
    if (s.cur.bottom > 0.01f) {
        const float radius = 0.9f * e->w;
        const float lid_top = e->cy + h / 2 + 2 - s.cur.bottom * (0.3f * h + 2);
        fill_rect(layer, e->cx - radius, lid_top, e->cx + radius, lid_top + 2 * radius, black, LV_RADIUS_CIRCLE,
                  LV_OPA_COVER);
    }
}

static void draw_overlays(lv_layer_t *layer, const eye_box_t eyes[2])
{
    const float t = (float) s.t_ms / 1000.0f;
    if (s.mood == EYES_MOOD_THINKING) {
        for (int i = 0; i < 3; i++) {
            float a = (sinf(t * 5 - (float) i * 0.9f) + 1) / 2;
            float x = CX - 30 + 30 * (float) i;
            fill_rect(layer, x - 8, CY + 142, x + 8, CY + 158, cur_color(1), LV_RADIUS_CIRCLE,
                      (lv_opa_t) (70 + 185 * a));
        }
    } else if (s.mood == EYES_MOOD_ASLEEP) {
        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        dsc.color = lv_color_hex(0x5FB4D2);
        for (int i = 0; i < 3; i++) {
            float p = fmodf(t * 0.35f + (float) i / 3.0f, 1.0f);
            dsc.font = i == 1 ? &lv_font_montserrat_40 : &lv_font_montserrat_28;
            dsc.opa = (lv_opa_t) (255 * (1 - p));
            dsc.text = "z";
            int32_t x = (int32_t) (330 + p * 55 + (float) i * 6);
            int32_t y = (int32_t) (170 - p * 100);
            const lv_area_t a = {x, y, x + 40, y + 48};
            lv_draw_label(layer, &dsc, &a);
        }
    } else if (s.mood == EYES_MOOD_HOT) {
        const lv_color_t blue = lv_color_hex(0x6FD3FF);
        float dy = fmodf(t * 40, 60);
        float x = eyes[1].cx + eyes[1].w / 2 + 22;
        float y = eyes[1].cy - eyes[1].h / 2 + dy;
        fill_triangle(layer, x, y - 16, x - 9, y + 2, x + 9, y + 2, blue);
        fill_rect(layer, x - 9, y - 5, x + 9, y + 13, blue, LV_RADIUS_CIRCLE, LV_OPA_COVER);
    }
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    eye_box_t eyes[2];
    eye_boxes(eyes);
    draw_eye(layer, &eyes[0]);
    draw_eye(layer, &eyes[1]);
    draw_overlays(layer, eyes);
}

/* The area this frame draws on: both eyes (lids included) and the mood's
 * overlay. */
static lv_area_t frame_area(void)
{
    eye_box_t eyes[2];
    eye_boxes(eyes);
    lv_area_t a = {
        (int32_t) (eyes[0].cx - eyes[0].w / 2 - 10),
        (int32_t) (fminf(eyes[0].cy - eyes[0].h / 2, eyes[1].cy - eyes[1].h / 2) - 10),
        (int32_t) (eyes[1].cx + eyes[1].w / 2 + 10),
        (int32_t) (fmaxf(eyes[0].cy + eyes[0].h / 2, eyes[1].cy + eyes[1].h / 2) + 10),
    };
    if (s.mood == EYES_MOOD_THINKING) {
        a.y2 = LV_MAX(a.y2, CY + 162);
    } else if (s.mood == EYES_MOOD_ASLEEP) {
        a.x2 = LV_MAX(a.x2, 440);
        a.y1 = LV_MIN(a.y1, 60);
    } else if (s.mood == EYES_MOOD_HOT) {
        a.x2 = LV_MAX(a.x2, (int32_t) (eyes[1].cx + eyes[1].w / 2 + 36));
        a.y2 = LV_MAX(a.y2, (int32_t) (eyes[1].cy - eyes[1].h / 2 + 80));
    }
    return a;
}

/* Ease every number towards the mood; true if anything visibly moved. */
static bool step(void)
{
    const eye_shape_t *want = &s_moods[s.mood];
    eye_shape_t before = s.cur;
    float *c = (float *) &s.cur;
    const float *w = (const float *) want;
    for (size_t i = 0; i < sizeof(eye_shape_t) / sizeof(float); i++) {
        c[i] = lerp(c[i], w[i], i < 3 ? 0.10f : 0.16f);
    }

    // Saccades: a new place to look every 1.2-3.6 s, unless held.
    if (!s.held) {
        s.next_look_ms -= EYES_FRAME_MS;
        if (s.next_look_ms <= 0) {
            s.next_look_ms = 1200 + (int32_t) (frand() * 2400);
            s.tgx = (frand() * 2 - 1) * 0.8f;
            s.tgy = (frand() * 2 - 1) * 0.5f;
            if (s.mood == EYES_MOOD_THINKING) {
                s.tgx = 0.55f + frand() * 0.2f;
                s.tgy = -0.8f;
            } else if (s.mood == EYES_MOOD_SLEEPY || s.mood == EYES_MOOD_ASLEEP) {
                s.tgx *= 0.3f;
                s.tgy = 0.6f;
            }
        }
    }
    const float gk = s.held ? 0.3f : 0.2f;
    float gx = lerp(s.gx, s.tgx, gk);
    float gy = lerp(s.gy, s.tgy, gk);

    // Blinks every 2.2-5.2 s, one in five doubled; none while asleep.
    if (s.mood != EYES_MOOD_ASLEEP) {
        s.next_blink_ms -= EYES_FRAME_MS;
        if (s.next_blink_ms <= 0) {
            eyes_blink();
        }
    }
    float lid;
    if (s.blink_ms > 0) {
        s.blink_ms -= EYES_FRAME_MS;
        lid = lerp(s.lid, 0.06f, 0.55f);
    } else {
        lid = lerp(s.lid, 1.0f, 0.35f);
    }
    float squash = s.squash * 0.85f;

    bool moved = fabsf(gx - s.gx) > 0.002f || fabsf(gy - s.gy) > 0.002f || fabsf(lid - s.lid) > 0.002f
                 || squash > 0.002f || memcmp(&before, &s.cur, sizeof(before)) != 0;
    s.gx = gx;
    s.gy = gy;
    s.lid = lid;
    s.squash = squash < 0.002f ? 0 : squash;
    return moved;
}

static void frame_cb(lv_timer_t *timer)
{
    s.t_ms += EYES_FRAME_MS;
    bool moved = step();
    if (!moved && !has_overlay(s.mood)) {
        return;
    }
    // Snap values that are close enough, so a still face stops redrawing.
    const eye_shape_t *want = &s_moods[s.mood];
    float *c = (float *) &s.cur;
    const float *w = (const float *) want;
    for (size_t i = 0; i < sizeof(eye_shape_t) / sizeof(float); i++) {
        if (fabsf(c[i] - w[i]) < 0.05f) {
            c[i] = w[i];
        }
    }
    lv_area_t now = frame_area();
    lv_area_t dirty = now;
    if (s.drawn_valid) {
        dirty.x1 = LV_MIN(now.x1, s.drawn.x1);
        dirty.y1 = LV_MIN(now.y1, s.drawn.y1);
        dirty.x2 = LV_MAX(now.x2, s.drawn.x2);
        dirty.y2 = LV_MAX(now.y2, s.drawn.y2);
    }
    lv_obj_invalidate_area(s.obj, &dirty);
    s.drawn = now;
    s.drawn_valid = true;
}

lv_obj_t *eyes_create(lv_obj_t *parent)
{
    memset(&s, 0, sizeof(s));
    s.mood = EYES_MOOD_NEUTRAL;
    s.cur = s_moods[EYES_MOOD_NEUTRAL];
    s.lid = 0.02f; // opens on the first frames
    s.next_blink_ms = 2500;
    s.next_look_ms = 1500;

    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    s.obj = lv_obj_create(parent);
    lv_obj_remove_style_all(s.obj);
    lv_obj_set_size(s.obj, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_center(s.obj);
    lv_obj_add_flag(s.obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s.obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s.obj, draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    lv_timer_create(frame_cb, EYES_FRAME_MS, NULL);
    return s.obj;
}

void eyes_set_mood(eyes_mood_t mood)
{
    if (mood >= EYES_MOOD_COUNT) {
        return;
    }
    s.mood = mood;
    s.next_look_ms = 0; // look somewhere that suits the mood
}

eyes_mood_t eyes_get_mood(void)
{
    return s.mood;
}

const char *eyes_mood_name(eyes_mood_t mood)
{
    return mood < EYES_MOOD_COUNT ? s_mood_names[mood] : "?";
}

void eyes_look_at(float x, float y)
{
    s.held = true;
    s.tgx = fmaxf(-1, fminf(1, x));
    s.tgy = fmaxf(-1, fminf(1, y));
}

void eyes_look_around(void)
{
    s.held = false;
    s.next_look_ms = 600;
}

void eyes_poke(void)
{
    s.squash = 1;
}

void eyes_blink(void)
{
    s.blink_ms = 120;
    s.next_blink_ms = frand() < 0.2f ? 300 : 2200 + (int32_t) (frand() * 3000);
}
