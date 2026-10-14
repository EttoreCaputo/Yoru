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
/* The hearts' distance from the middle. */
#define HEART_X 160
/* The most pieces an overlay has (three z's, three dots). */
#define MAX_OVERLAY_RECTS 3

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
    [EYES_MOOD_SAD] = {RGB(0x5C8DFF), 80, 110, 0.26f, -0.40f, 0, 0, 1, 1},
    [EYES_MOOD_LOVING] = {RGB(0xFF7AB8), 93, 116, 0, 0, 1, 0, 1, 1},
    [EYES_MOOD_SURPRISED] = {RGB(0x3EE6FF), 98, 140, 0, 0, 0, 0, 1.04f, 1.04f},
    [EYES_MOOD_BORED] = {RGB(0x6FA0B4), 88, 112, 0.44f, 0, 0.2f, 0, 1, 1},
    [EYES_MOOD_LISTENING] = {RGB(0x8AF4FF), 92, 132, 0, 0, 0, -4, 1, 1},
    [EYES_MOOD_SPEAKING] = {RGB(0xB8F0FF), 90, 118, 0, 0, 0.3f, 0, 1, 1},
};

static const char *const s_mood_names[EYES_MOOD_COUNT] = {
    "neutral", "happy", "curious", "thinking", "grumpy", "hot", "sleepy", "asleep", "sad", "loving", "surprised", "bored", "listening", "speaking",
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
    /* The voice's level, eased, and where it is going. */
    float level, tlevel;
    uint32_t t_ms;        // animation clock for the overlays
    /* What the last frame covered, eyes and overlay apart: the overlay keeps
     * moving while the eyes are still (the z's of sleep). */
    lv_area_t eyes_drawn;
    bool eyes_valid;
    lv_area_t over_drawn[MAX_OVERLAY_RECTS];
    int over_count;
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
    return mood == EYES_MOOD_THINKING || mood == EYES_MOOD_HOT || mood == EYES_MOOD_ASLEEP || mood == EYES_MOOD_LOVING;
}

/* Where an eye is this frame. */
typedef struct {
    float cx, cy, w, h;
    int inner; // +1 for the left eye (its inner side is to the right)
} eye_box_t;

static void eye_boxes(eye_box_t out[2])
{
    const float swell = 1 + 0.14f * s.level;
    const float w = s.cur.w * (1 + 0.3f * s.squash) * swell;
    const float h = s.cur.h * (1 - 0.38f * s.squash) * s.lid * swell;
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

/* Where the top lid cuts the eye, at its outer left and right ends. */
static void lid_edges(const eye_box_t *e, float *edge_l, float *edge_r)
{
    const float top = e->cy - e->h / 2;
    const float h = e->h < 2 ? 2 : e->h;
    *edge_l = top + s.cur.top * h - s.cur.slope * h * 0.6f * (float) e->inner;
    *edge_r = top + s.cur.top * h + s.cur.slope * h * 0.6f * (float) e->inner;
}

/* The highest row of an eye the top lid leaves showing. */
static float visible_top(const eye_box_t *e)
{
    float edge_l, edge_r;
    lid_edges(e, &edge_l, &edge_r);
    return fmaxf(e->cy - e->h / 2, fminf(edge_l, edge_r));
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
    float edge_l, edge_r;
    lid_edges(e, &edge_l, &edge_r);
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

/* A small heart: two round tops and a point below, centred on (x, y). */
static void draw_heart(lv_layer_t *layer, float x, float y, lv_color_t color)
{
    fill_rect(layer, x - 13, y - 7, x + 1, y + 7, color, LV_RADIUS_CIRCLE, LV_OPA_COVER);
    fill_rect(layer, x - 1, y - 7, x + 13, y + 7, color, LV_RADIUS_CIRCLE, LV_OPA_COVER);
    fill_triangle(layer, x - 13, y + 1, x + 13, y + 1, x, y + 16, color);
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
    } else if (s.mood == EYES_MOOD_LOVING) {
        // Two hearts float up beside the eyes and fade into the black.
        for (int i = 0; i < 2; i++) {
            float p = fmodf(t * 0.4f + (float) i * 0.5f, 1.0f);
            float x = i == 0 ? CX - HEART_X : CX + HEART_X;
            draw_heart(layer, x, CY - 40 - p * 90, cur_color(1 - p));
        }
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

/* The area this frame's eyes show. Everything above the top lid is black on
 * black, so it starts there: a sleeping eye is a thin slice. */
static lv_area_t eyes_area(void)
{
    eye_box_t eyes[2];
    eye_boxes(eyes);
    return (lv_area_t) {
        (int32_t) (eyes[0].cx - eyes[0].w / 2 - 10),
        (int32_t) (fminf(visible_top(&eyes[0]), visible_top(&eyes[1])) - 10),
        (int32_t) (eyes[1].cx + eyes[1].w / 2 + 10),
        (int32_t) (fmaxf(eyes[0].cy + eyes[0].h / 2, eyes[1].cy + eyes[1].h / 2) + 10),
    };
}

/* The pieces of the mood's overlay this frame, one box each (with a margin for
 * antialiasing), so only what moves gets redrawn. Mirrors draw_overlays().
 * Returns how many. */
static int overlay_areas(lv_area_t out[MAX_OVERLAY_RECTS])
{
    const float t = (float) s.t_ms / 1000.0f;
    eye_box_t eyes[2];
    eye_boxes(eyes);
    if (s.mood == EYES_MOOD_THINKING) {
        // The dots stay put and only fade: one box for the row.
        out[0] = (lv_area_t) {CX - 40, CY + 140, CX + 40, CY + 160};
        return 1;
    }
    if (s.mood == EYES_MOOD_ASLEEP) {
        for (int i = 0; i < 3; i++) {
            float p = fmodf(t * 0.35f + (float) i / 3.0f, 1.0f);
            int32_t x = (int32_t) (330 + p * 55 + (float) i * 6);
            int32_t y = (int32_t) (170 - p * 100);
            out[i] = (lv_area_t) {x - 2, y - 2, x + 42, y + 50};
        }
        return 3;
    }
    if (s.mood == EYES_MOOD_HOT) {
        float dy = fmodf(t * 40, 60);
        int32_t x = (int32_t) (eyes[1].cx + eyes[1].w / 2 + 22);
        int32_t y = (int32_t) (eyes[1].cy - eyes[1].h / 2 + dy);
        out[0] = (lv_area_t) {x - 12, y - 19, x + 12, y + 16};
        return 1;
    }
    if (s.mood == EYES_MOOD_LOVING) {
        for (int i = 0; i < 2; i++) {
            float p = fmodf(t * 0.4f + (float) i * 0.5f, 1.0f);
            int32_t x = i == 0 ? CX - HEART_X : CX + HEART_X;
            int32_t y = (int32_t) (CY - 40 - p * 90);
            out[i] = (lv_area_t) {x - 16, y - 10, x + 16, y + 19};
        }
        return 2;
    }
    return 0;
}

/* Redraws where something was and where it is now: one box if the two touch
 * (a small step), two if it jumped (a z starting over at the bottom). */
static void invalidate_move(const lv_area_t *was, const lv_area_t *now)
{
    bool touch = was->x1 <= now->x2 && now->x1 <= was->x2 && was->y1 <= now->y2 && now->y1 <= was->y2;
    if (touch) {
        lv_area_t both;
        both.x1 = LV_MIN(was->x1, now->x1);
        both.y1 = LV_MIN(was->y1, now->y1);
        both.x2 = LV_MAX(was->x2, now->x2);
        both.y2 = LV_MAX(was->y2, now->y2);
        lv_obj_invalidate_area(s.obj, &both);
    } else {
        lv_obj_invalidate_area(s.obj, was);
        lv_obj_invalidate_area(s.obj, now);
    }
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
            } else if (s.mood == EYES_MOOD_SLEEPY || s.mood == EYES_MOOD_ASLEEP || s.mood == EYES_MOOD_SAD) {
                s.tgx *= 0.3f;
                s.tgy = 0.6f;
            } else if (s.mood == EYES_MOOD_BORED) {
                // Off to the side, as if anything else were more interesting.
                s.tgx = s.tgx < 0 ? -0.75f : 0.75f;
                s.tgy = 0.1f;
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
    // Quick to rise with a syllable, slower to fall: it reads as talking.
    float level = lerp(s.level, s.tlevel, s.tlevel > s.level ? 0.5f : 0.2f);
    if (level < 0.005f) {
        level = 0;
    }

    bool moved = fabsf(gx - s.gx) > 0.002f || fabsf(gy - s.gy) > 0.002f || fabsf(lid - s.lid) > 0.002f
                 || squash > 0.002f || fabsf(level - s.level) > 0.002f || memcmp(&before, &s.cur, sizeof(before)) != 0;
    s.level = level;
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
    if (!moved && !has_overlay(s.mood) && s.over_count == 0) {
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
    if (moved || !s.eyes_valid) {
        lv_area_t now = eyes_area();
        if (s.eyes_valid) {
            invalidate_move(&s.eyes_drawn, &now);
        } else {
            lv_obj_invalidate_area(s.obj, &now);
        }
        s.eyes_drawn = now;
        s.eyes_valid = true;
    }
    // Each overlay piece against its own last place; a piece the mood no
    // longer has is cleared once.
    lv_area_t over[MAX_OVERLAY_RECTS];
    int n = overlay_areas(over);
    for (int i = 0; i < LV_MAX(n, s.over_count); i++) {
        if (i < n && i < s.over_count) {
            invalidate_move(&s.over_drawn[i], &over[i]);
        } else if (i < n) {
            lv_obj_invalidate_area(s.obj, &over[i]);
        } else {
            lv_obj_invalidate_area(s.obj, &s.over_drawn[i]);
        }
        if (i < n) {
            s.over_drawn[i] = over[i];
        }
    }
    s.over_count = n;
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
    if (mood >= EYES_MOOD_COUNT || mood == s.mood) {
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

void eyes_set_level(float level)
{
    s.tlevel = fmaxf(0, fminf(1, level));
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
