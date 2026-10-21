#include "look.h"

#include <math.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "look";

/* The light comes from the top left, as in the Rive file: a radial gradient
 * centred there, from pearl to a soft shadow at the bottom-right edge. */
#define K LOOK_SCALE
#define LIGHT_X (-62.0f * K)
#define LIGHT_Y (-68.0f * K)
#define LIGHT_R (276.0f * K)
typedef struct {
    float pos;
    uint32_t rgb;
} stop_t;

static const stop_t PEARL[] = {
    {0.00f, 0xF6F4F7}, {0.22f, 0xDEDBE2}, {0.58f, 0xAEAAB5}, {0.84f, 0x6F6B78}, {1.00f, 0x3A3742},
};
static look_t s_look;
/* The spheres of other sizes, rendered when first drawn: a few for the
 * sizes and squashes in use, within a budget of PSRAM (a sphere up at the
 * glass is about 450 KB). */
#define BALL_SLOTS 14
#define BALL_BUDGET (3200 * 1024)
typedef struct {
    look_sprite_t sp;
    int rx, ry; // 0: empty
    uint32_t used;
} ball_slot_t;
static ball_slot_t s_balls[BALL_SLOTS];
static uint32_t s_ball_bytes;
static int64_t s_ball_slowest_us;
/* The pearl gradient by the squared distance to the light (1 at LIGHT_R),
 * as RGB565 for each of the 16 dither levels: in PSRAM, read through the
 * cache. */
#define SHADE_N 256
static uint16_t *s_shade565;
static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* The panel shows 32 levels of red and blue and 64 of green: a smooth
 * gradient turns into visible rings. A 4x4 ordered dither spreads each step
 * over its neighbours, too fine to see at this pixel size. 0..1. */
static float dither(int x, int y)
{
    return ((float) BAYER[y & 3][x & 3] + 0.5f) / 16.0f;
}

/* An alpha map recoloured at draw time bands the same way; `amp` is how much
 * alpha one colour step takes at its brightest. */
static uint8_t dither_a8(float a, int x, int y, float amp)
{
    return (uint8_t) clampf(a + (dither(x, y) - 0.5f) * amp, 0, 255);
}

/* A colour along a gradient, as r, g, b 0..255. */
static void gradient(const stop_t *stops, int n, float t, float out[3])
{
    t = clampf(t, 0, 1);
    int i = 0;
    while (i < n - 2 && t > stops[i + 1].pos) {
        i++;
    }
    float k = clampf((t - stops[i].pos) / (stops[i + 1].pos - stops[i].pos), 0, 1);
    for (int c = 0; c < 3; c++) {
        float a = (float) ((stops[i].rgb >> (16 - 8 * c)) & 0xFF);
        float b = (float) ((stops[i + 1].rgb >> (16 - 8 * c)) & 0xFF);
        out[c] = a + (b - a) * k;
    }
}

static esp_err_t sprite_alloc(look_sprite_t *sp, int32_t w, int32_t h, lv_color_format_t cf, int32_t ax, int32_t ay)
{
    // RGB565A8 is the RGB565 plane (the stride) followed by the A8 plane.
    const uint32_t bpp = cf == LV_COLOR_FORMAT_A8 ? 1 : 2;
    const uint32_t size = (uint32_t) (w * h) * (cf == LV_COLOR_FORMAT_A8 ? 1 : 3);
    // Big and read only at draw time: PSRAM (CLAUDE.md).
    uint8_t *data = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(data, ESP_ERR_NO_MEM, TAG, "sprite %ldx%ld", (long) w, (long) h);
    memset(&sp->img, 0, sizeof(sp->img));
    sp->img.header.magic = LV_IMAGE_HEADER_MAGIC;
    sp->img.header.cf = cf;
    sp->img.header.w = w;
    sp->img.header.h = h;
    sp->img.header.stride = w * bpp;
    sp->img.data_size = size;
    sp->img.data = data;
    sp->ax = ax;
    sp->ay = ay;
    return ESP_OK;
}

static inline void put_px(look_sprite_t *sp, int x, int y, const float rgb[3], float a)
{
    const int w = (int) sp->img.header.w, i = y * w + x;
    // RGB565 keeps the top bits: add up to one step before they are cut.
    const float d = dither(x, y);
    const lv_color_t c = lv_color_make((uint8_t) clampf(rgb[0] + 8 * d, 0, 255), (uint8_t) clampf(rgb[1] + 4 * d, 0, 255),
                                       (uint8_t) clampf(rgb[2] + 8 * d, 0, 255));
    uint8_t *data = (uint8_t *) sp->img.data;
    ((uint16_t *) data)[i] = lv_color_to_u16(c);
    data[w * (int) sp->img.header.h * 2 + i] = (uint8_t) (clampf(a, 0, 1) * 255.0f + 0.5f);
}

/* The sphere by day, `r` pixels across from its centre, and the same shading
 * for the hands (an ellipse, flattened). */
static esp_err_t render_ball(look_sprite_t *sp, float rx, float ry, float light_x, float light_y, float light_r,
                             const stop_t *stops)
{
    const int w = (int) ceilf(2 * rx) + 4;
    const int h = (int) ceilf(2 * ry) + 4;
    ESP_RETURN_ON_ERROR(sprite_alloc(sp, w, h, LV_COLOR_FORMAT_RGB565A8, w / 2, h / 2), TAG, "ball");
    const float rmin = fminf(rx, ry);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float px = (float) x + 0.5f - (float) w / 2;
            float py = (float) y + 0.5f - (float) h / 2;
            // Distance to the ellipse's edge, roughly, in pixels.
            float d = (sqrtf(px * px / (rx * rx) + py * py / (ry * ry)) - 1) * rmin;
            float cov = clampf(0.5f - d, 0, 1);
            if (cov <= 0) {
                continue;
            }
            float rgb[3];
            gradient(stops, 5, hypotf(px - light_x, py - light_y) / light_r, rgb);
            put_px(sp, x, y, rgb, cov);
        }
    }
    return ESP_OK;
}

/* The tint fills the lower half of the sphere, strongest at the bottom. */
static esp_err_t render_tint(look_sprite_t *sp)
{
    const float r = LOOK_SPHERE_R;
    const int size = 2 * LOOK_SPHERE_R + 4;
    ESP_RETURN_ON_ERROR(sprite_alloc(sp, size, size, LV_COLOR_FORMAT_A8, size / 2, size / 2), TAG, "tint");
    uint8_t *a8 = (uint8_t *) sp->img.data;
    for (int y = 0; y < size; y++) {
        float py = (float) y + 0.5f - (float) size / 2;
        float k = clampf(py / r, 0, 1);
        for (int x = 0; x < size; x++) {
            float px = (float) x + 0.5f - (float) size / 2;
            float cov = clampf(r - hypotf(px, py) + 0.5f, 0, 1);
            a8[y * size + x] = dither_a8(255 * cov * k, x, y, 10);
        }
    }
    return ESP_OK;
}

/* The halo: a soft ring of glow just around the sphere. Kept tight, so
 * redrawing it each frame stays near Yoru instead of the whole screen. */
static esp_err_t render_halo(look_sprite_t *sp)
{
    const int size = ((int) (384 * K)) & ~1;
    ESP_RETURN_ON_ERROR(sprite_alloc(sp, size, size, LV_COLOR_FORMAT_A8, size / 2, size / 2), TAG, "halo");
    uint8_t *a8 = (uint8_t *) sp->img.data;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float d = hypotf((float) x + 0.5f - size / 2.0f, (float) y + 0.5f - size / 2.0f);
            float k = clampf((190 * K - d) / (60 * K), 0, 1);
            // Drawn at about a third: a colour step is three times the alpha.
            a8[y * size + x] = dither_a8(255 * k * k * (3 - 2 * k), x, y, 24);
        }
    }
    return ESP_OK;
}

static esp_err_t render_ball_fast(look_sprite_t *sp, int rx, int ry);

esp_err_t look_init(void)
{
    const int64_t start = esp_timer_get_time();
    s_shade565 = heap_caps_malloc(SHADE_N * 16 * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_shade565, ESP_ERR_NO_MEM, TAG, "shade table");
    for (int i = 0; i < SHADE_N; i++) {
        float rgb[3];
        gradient(PEARL, 5, sqrtf((float) i / (SHADE_N - 1)), rgb);
        for (int d = 0; d < 16; d++) {
            // RGB565 keeps the top bits: up to one step of dither before the cut.
            const float k = ((float) d + 0.5f) / 16;
            const lv_color_t c = lv_color_make((uint8_t) clampf(rgb[0] + 8 * k, 0, 255),
                                               (uint8_t) clampf(rgb[1] + 4 * k, 0, 255),
                                               (uint8_t) clampf(rgb[2] + 8 * k, 0, 255));
            s_shade565[(i << 4) | d] = lv_color_to_u16(c);
        }
    }
    // The same renderer as every other size, so going nearer or farther
    // never changes the shading.
    ESP_RETURN_ON_ERROR(render_ball_fast(&s_look.day, LOOK_SPHERE_R, LOOK_SPHERE_R), TAG, "day");
    ESP_RETURN_ON_ERROR(render_tint(&s_look.tint), TAG, "tint");
    ESP_RETURN_ON_ERROR(render_halo(&s_look.halo), TAG, "halo");
    ESP_RETURN_ON_ERROR(render_ball(&s_look.hand, 27 * K, 22 * K, -12 * K, -11 * K, 31 * K, PEARL), TAG, "hand");
    ESP_LOGI(TAG, "Yoru rendered in %lld ms", (esp_timer_get_time() - start) / 1000);
    return ESP_OK;
}

/* The sphere at any size, quickly: the shading comes from a table by the
 * squared distance to the light, already dithered and packed for each of the
 * 16 dither levels, and walked along each row by forward differences (adds
 * only, no square roots); only the rim's pixels work out their coverage.
 * The light moves with the surface, as if the round one were scaled. */
static esp_err_t render_ball_fast(look_sprite_t *sp, int rx, int ry)
{
    const int w = 2 * rx + 4, h = 2 * ry + 4;
    const uint32_t size = (uint32_t) (w * h * 3);
    // Only the alpha plane needs clearing: colour outside the disc is unseen.
    uint8_t *data = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(data, ESP_ERR_NO_MEM, TAG, "ball %dx%d", rx, ry);
    memset(data + w * h * 2, 0, (size_t) (w * h));
    memset(&sp->img, 0, sizeof(sp->img));
    sp->img.header.magic = LV_IMAGE_HEADER_MAGIC;
    sp->img.header.cf = LV_COLOR_FORMAT_RGB565A8;
    sp->img.header.w = w;
    sp->img.header.h = h;
    sp->img.header.stride = w * 2;
    sp->img.data_size = size;
    sp->img.data = data;
    sp->ax = w / 2;
    sp->ay = h / 2;

    const float f = sqrtf((float) (rx * ry)) / LOOK_SPHERE_R;
    const float lx = LIGHT_X * (float) rx / LOOK_SPHERE_R, ly = LIGHT_Y * (float) ry / LOOK_SPHERE_R;
    const float shade_k = (SHADE_N - 1) / (LIGHT_R * f * LIGHT_R * f);
    const float rx2 = (float) (rx * rx), irx2 = 1.0f / rx2, iry2 = 1.0f / (float) (ry * ry);
    const float rmin = (float) (rx < ry ? rx : ry);
    // Inside this the edge's antialiasing can't reach: fully covered.
    const float inner = (1 - 1 / rmin) * (1 - 1 / rmin);
    const float half = (float) w / 2;
    uint16_t *rgb = (uint16_t *) data;
    uint8_t *alpha = data + w * h * 2;
    for (int y = 0; y < h; y++) {
        const float py = (float) y + 0.5f - (float) h / 2;
        const float row = 1 - py * py / ((ry + 1.0f) * (ry + 1.0f));
        if (row <= 0) {
            continue;
        }
        const int span = (int) ((float) (rx + 1) * sqrtf(row)) + 2;
        const int x0 = w / 2 - span < 0 ? 0 : w / 2 - span, x1 = w / 2 + span > w ? w : w / 2 + span;
        const float qy = py * py * iry2, dy = py - ly;
        // The shading along the row: a quadratic in x, stepped by adds.
        const float dx0 = (float) x0 + 0.5f - half - lx;
        float t = (dx0 * dx0 + dy * dy) * shade_k, dt = (2 * dx0 + 1) * shade_k;
        const float d2t = 2 * shade_k;
        const uint8_t *bayer = BAYER[y & 3];
        uint16_t *out = rgb + y * w;
        for (int x = x0; x < x1; x++) {
            int i = (int) t;
            i = i >= SHADE_N ? SHADE_N - 1 : i;
            out[x] = s_shade565[(i << 4) | bayer[x & 3]];
            t += dt;
            dt += d2t;
        }
        // Coverage: whole in the middle, worked out only at the rim.
        uint8_t *a = alpha + y * w;
        const float in2 = (inner - qy) * rx2;
        int xi0 = x1, xi1 = x1;
        if (in2 > 0) {
            const float xin = sqrtf(in2);
            xi0 = (int) (half - 0.5f - xin) + 1;
            xi1 = w - xi0;
            xi0 = xi0 < x0 ? x0 : xi0;
            xi1 = xi1 > x1 ? x1 : xi1;
            if (xi1 > xi0) {
                memset(a + xi0, 255, (size_t) (xi1 - xi0));
            } else {
                xi0 = xi1 = x1;
            }
        }
        for (int x = x0; x < x1; x++) {
            if (x == xi0) {
                x = xi1 - 1;
                continue;
            }
            const float px = (float) x + 0.5f - half;
            const float q = px * px * irx2 + qy;
            const float cov = clampf(0.5f - (sqrtf(q) - 1) * rmin, 0, 1);
            a[x] = (uint8_t) (cov * 255.0f + 0.5f);
        }
    }
    return ESP_OK;
}

static void ball_free(ball_slot_t *b)
{
    heap_caps_free((void *) b->sp.img.data);
    s_ball_bytes -= b->sp.img.data_size;
    memset(b, 0, sizeof(*b));
}

const look_sprite_t *look_ball(int rx, int ry, uint32_t frame)
{
    rx = rx < 2 ? 2 : rx;
    ry = ry < 2 ? 2 : ry;
    if (rx == LOOK_SPHERE_R && ry == LOOK_SPHERE_R) {
        return &s_look.day;
    }
    ball_slot_t *free_slot = NULL, *oldest = NULL;
    for (int i = 0; i < BALL_SLOTS; i++) {
        ball_slot_t *b = &s_balls[i];
        if (b->rx == rx && b->ry == ry) {
            b->used = frame;
            return &b->sp;
        }
        if (b->rx == 0) {
            free_slot = free_slot ? free_slot : b;
        } else if (b->used != frame && (oldest == NULL || frame - b->used > frame - oldest->used)) {
            oldest = b;
        }
    }
    // Room for it: the least recently drawn go, never one drawn this frame.
    const uint32_t need = (uint32_t) ((2 * rx + 4) * (2 * ry + 4) * 3);
    while (oldest != NULL && (free_slot == NULL || s_ball_bytes + need > BALL_BUDGET)) {
        ball_free(oldest);
        free_slot = free_slot ? free_slot : oldest;
        oldest = NULL;
        for (int i = 0; i < BALL_SLOTS; i++) {
            ball_slot_t *b = &s_balls[i];
            if (b->rx != 0 && b->used != frame && (oldest == NULL || frame - b->used > frame - oldest->used)) {
                oldest = b;
            }
        }
    }
    if (free_slot == NULL) {
        return NULL;
    }
    const int64_t start = esp_timer_get_time();
    if (render_ball_fast(&free_slot->sp, rx, ry) != ESP_OK) {
        return NULL;
    }
    free_slot->rx = rx;
    free_slot->ry = ry;
    free_slot->used = frame;
    s_ball_bytes += free_slot->sp.img.data_size;
    const int64_t took = esp_timer_get_time() - start;
    if (took > s_ball_slowest_us) {
        s_ball_slowest_us = took;
        ESP_LOGI(TAG, "sphere %dx%d rendered in %lld us", rx, ry, took);
    }
    return &free_slot->sp;
}

const look_t *look_get(void)
{
    return &s_look;
}

void look_draw(lv_layer_t *layer, const look_sprite_t *sp, int32_t x, int32_t y, float scale_x, float scale_y,
               float rotation, lv_opa_t opa, lv_color_t color)
{
    if (opa <= LV_OPA_MIN || scale_x < 0.02f || scale_y < 0.02f) {
        return;
    }
    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.src = &sp->img;
    dsc.opa = opa;
    dsc.pivot.x = sp->ax;
    dsc.pivot.y = sp->ay;
    dsc.scale_x = (int32_t) lroundf(scale_x * LV_SCALE_NONE);
    dsc.scale_y = (int32_t) lroundf(scale_y * LV_SCALE_NONE);
    dsc.rotation = (int32_t) lroundf(rotation * 10);
    dsc.antialias = 1;
    if (sp->img.header.cf == LV_COLOR_FORMAT_A8) {
        dsc.recolor = color;
        dsc.recolor_opa = LV_OPA_COVER;
    }
    const lv_area_t coords = {x - sp->ax, y - sp->ay, x - sp->ax + (int32_t) sp->img.header.w - 1,
                              y - sp->ay + (int32_t) sp->img.header.h - 1};
    lv_draw_image(layer, &dsc, &coords);
}

lv_area_t look_area(const look_sprite_t *sp, int32_t x, int32_t y, float scale_x, float scale_y, bool rotated)
{
    float w = (float) sp->img.header.w, h = (float) sp->img.header.h;
    float l = (float) sp->ax * scale_x, r = (w - (float) sp->ax) * scale_x;
    float t = (float) sp->ay * scale_y, b = (h - (float) sp->ay) * scale_y;
    if (rotated) {
        // Any turn stays inside the circle through the farthest corner.
        float m = fmaxf(fmaxf(hypotf(l, t), hypotf(r, t)), fmaxf(hypotf(l, b), hypotf(r, b)));
        l = r = t = b = m;
    }
    return (lv_area_t) {x - (int32_t) l - 2, y - (int32_t) t - 2, x + (int32_t) r + 2, y + (int32_t) b + 2};
}
