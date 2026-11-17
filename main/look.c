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
/* The hand's long radius. */
#define HAND_RX (27 * K)
#define LIGHT_X (-62.0f * K)
#define LIGHT_Y (-68.0f * K)
#define LIGHT_R (276.0f * K)
typedef struct {
    float pos;
    uint32_t rgb;
} stop_t;

/* The sphere's colours, light to shadow, picked in the menu: pearl by
 * default. */
#define STOPS 5
typedef struct {
    const char *en, *it;
    stop_t stops[STOPS];
} palette_t;
#define P(a, b, c, d, e) {{0.00f, a}, {0.22f, b}, {0.58f, c}, {0.84f, d}, {1.00f, e}}
static const palette_t PALETTES[LOOK_PALETTES] = {
    {"Pearl", "Perla", P(0xF6F4F7, 0xDEDBE2, 0xAEAAB5, 0x6F6B78, 0x3A3742)},
    {"Dawn", "Alba", P(0xFFF1E6, 0xFAD3BE, 0xE09F86, 0x9A5B4E, 0x4A2A2C)},
    {"Mint", "Menta", P(0xEEFBF5, 0xC8EEDD, 0x8CCDB2, 0x4C8A75, 0x213F37)},
    {"Lilac", "Lilla", P(0xF6F0FF, 0xDCCFF5, 0xAE9AD8, 0x6E5C99, 0x352C4E)},
    {"Night", "Notte", P(0xE8EEFF, 0xB8C6EE, 0x7C8CC6, 0x434F86, 0x1E2546)},
    {"Gold", "Oro", P(0xFFF8E4, 0xF5E1A8, 0xD4B060, 0x8E6E2E, 0x46361A)},
    {"Rose", "Rosa", P(0xFFF0F3, 0xF7CCD6, 0xE096AA, 0x9A5468, 0x4A2532)},
};
#undef P
static int s_palette;
/* Asked from any task (a setting), done on LVGL's (look_apply_palette). */
static volatile int s_palette_wanted = -1;
#define STOPS_NOW (PALETTES[s_palette].stops)
static look_t s_look;
/* The spheres of other sizes, rendered when first drawn: a few for the
 * sizes and squashes in use, within a budget of PSRAM (a sphere up at the
 * glass is about 450 KB). The budget is small: PSRAM is shared with
 * MultiNet, which loads while Yoru greets you at boot and crashes inside
 * ESP-SR if it can't have its memory, and with the cloud's TLS. */
#define BALL_SLOTS 14
#define BALL_BUDGET (1024 * 1024)
typedef struct {
    look_sprite_t sp;
    look_sprite_t tint; // its tint, in the same block
    look_sprite_t core; // its colour alone, RGB565
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
            gradient(stops, STOPS, hypotf(px - light_x, py - light_y) / light_r, rgb);
            put_px(sp, x, y, rgb, cov);
        }
    }
    return ESP_OK;
}

static esp_err_t render_halo_at(look_sprite_t *sp, int size);
static int halo_full(void);
static esp_err_t render_ball_fast(look_sprite_t *sp, look_sprite_t *tint, look_sprite_t *core, int rx, int ry);
static void shade_table(void);
/* The day sphere's colour alone (look_ball_core). */
static look_sprite_t s_day_core;

esp_err_t look_init(void)
{
    const int64_t start = esp_timer_get_time();
    s_shade565 = heap_caps_malloc(SHADE_N * 16 * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_shade565, ESP_ERR_NO_MEM, TAG, "shade table");
    if (s_palette_wanted >= 0) {
        s_palette = s_palette_wanted;
        s_palette_wanted = -1;
    }
    shade_table();
    // The same renderer as every other size, so going nearer or farther
    // never changes the shading.
    ESP_RETURN_ON_ERROR(render_ball_fast(&s_look.day, &s_look.tint, &s_day_core, LOOK_SPHERE_R, LOOK_SPHERE_R), TAG, "day");
    // The halo: a soft ring of glow just around the sphere. Kept tight, so
    // redrawing it each frame stays near Yoru instead of the whole screen.
    ESP_RETURN_ON_ERROR(render_halo_at(&s_look.halo, halo_full()), TAG, "halo");
    ESP_RETURN_ON_ERROR(render_ball(&s_look.hand, HAND_RX, 22 * K, -12 * K, -11 * K, 31 * K, STOPS_NOW), TAG, "hand");
    ESP_LOGI(TAG, "Yoru rendered in %lld ms", (esp_timer_get_time() - start) / 1000);
    return ESP_OK;
}

/* The gradient of the palette in use, by the squared distance to the light. */
static void shade_table(void)
{
    for (int i = 0; i < SHADE_N; i++) {
        float rgb[3];
        gradient(STOPS_NOW, STOPS, sqrtf((float) i / (SHADE_N - 1)), rgb);
        for (int d = 0; d < 16; d++) {
            // RGB565 keeps the top bits: up to one step of dither before the cut.
            const float k = ((float) d + 0.5f) / 16;
            const lv_color_t c = lv_color_make((uint8_t) clampf(rgb[0] + 8 * k, 0, 255),
                                               (uint8_t) clampf(rgb[1] + 4 * k, 0, 255),
                                               (uint8_t) clampf(rgb[2] + 8 * k, 0, 255));
            s_shade565[(i << 4) | d] = lv_color_to_u16(c);
        }
    }
}

/* The sphere at any size, quickly: the shading comes from a table by the
 * squared distance to the light, already dithered and packed for each of the
 * 16 dither levels, and walked along each row by forward differences (adds
 * only, no square roots); only the rim's pixels work out their coverage.
 * The light moves with the surface, as if the round one were scaled.
 *
 * Its tint comes along, an A8 plane after the sphere's: the lower half of
 * the disc, strongest at the bottom. Drawn at scale 1 like the sphere, it
 * costs a quarter of what LVGL scaling one tint to every size did. */
static esp_err_t render_ball_fast(look_sprite_t *sp, look_sprite_t *tint, look_sprite_t *core, int rx, int ry)
{
    const int w = 2 * rx + 4, h = 2 * ry + 4;
    const uint32_t size = (uint32_t) (w * h * 4);
    // Only the alpha planes need clearing: colour outside the disc is unseen.
    uint8_t *data = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data == NULL) {
        return ESP_ERR_NO_MEM; // look_ball() makes room and tries again
    }
    memset(data + w * h * 2, 0, (size_t) (w * h * 2));
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
    *tint = *sp;
    tint->img.header.cf = LV_COLOR_FORMAT_A8;
    tint->img.header.stride = w;
    tint->img.data_size = (uint32_t) (w * h);
    tint->img.data = data + w * h * 3;
    // The colour plane alone: where the sphere is opaque it is copied, not
    // blended.
    *core = *sp;
    core->img.header.cf = LV_COLOR_FORMAT_RGB565;
    core->img.data_size = (uint32_t) (w * h * 2);

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
        // The tint: the coverage, fading in from the middle down.
        if (py > 0) {
            const float k = clampf(py / (float) ry, 0, 1);
            uint8_t *ta = alpha + w * h + y * w;
            for (int x = x0; x < x1; x++) {
                const float v = (float) a[x] * k + ((float) bayer[x & 3] - 7.5f) * (10.0f / 16.0f);
                ta[x] = a[x] == 0 ? 0 : (uint8_t) clampf(v, 0, 255);
            }
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
    const uint32_t need = (uint32_t) ((2 * rx + 4) * (2 * ry + 4) * 4);
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
    // PSRAM is shared with the voice, the WiFi's TLS and the cloud: when no
    // block is big enough, older sizes go until one is (freed neighbours
    // merge).
    while (render_ball_fast(&free_slot->sp, &free_slot->tint, &free_slot->core, rx, ry) != ESP_OK) {
        oldest = NULL;
        for (int i = 0; i < BALL_SLOTS; i++) {
            ball_slot_t *b = &s_balls[i];
            if (b->rx != 0 && b->used != frame && (oldest == NULL || frame - b->used > frame - oldest->used)) {
                oldest = b;
            }
        }
        if (oldest == NULL) {
            return NULL;
        }
        ball_free(oldest);
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

const look_sprite_t *look_ball_tint(const look_sprite_t *ball)
{
    if (ball == &s_look.day) {
        return &s_look.tint;
    }
    for (int i = 0; i < BALL_SLOTS; i++) {
        if (ball == &s_balls[i].sp) {
            return &s_balls[i].tint;
        }
    }
    return NULL;
}

const look_sprite_t *look_ball_core(const look_sprite_t *ball)
{
    if (ball == &s_look.day) {
        return &s_day_core;
    }
    for (int i = 0; i < BALL_SLOTS; i++) {
        if (ball == &s_balls[i].sp) {
            return &s_balls[i].core;
        }
    }
    return NULL;
}

/* The halo at other sizes, as Yoru moves nearer or farther: a couple kept,
 * in steps of HALO_STEP px (it is soft: the steps don't show). */
#define HALO_SLOTS 2
#define HALO_STEP 4
typedef struct {
    look_sprite_t sp;
    int size; // 0: empty
    uint32_t used;
} halo_slot_t;
static halo_slot_t s_halos[HALO_SLOTS];
/* The halo's alpha by the squared distance from the middle, as a share of
 * its radius squared. */
#define HALO_LUT_N 512
static float s_halo_lut[HALO_LUT_N];

static int halo_full(void)
{
    return ((int) (384 * K)) & ~1;
}

/* Dithered like the boot one; the shape is the same at every size. */
static esp_err_t render_halo_at(look_sprite_t *sp, int size)
{
    ESP_RETURN_ON_ERROR(sprite_alloc(sp, size, size, LV_COLOR_FORMAT_A8, size / 2, size / 2), TAG, "halo");
    if (s_halo_lut[0] == 0) {
        for (int i = 0; i < HALO_LUT_N; i++) {
            const float u = sqrtf(((float) i + 0.5f) / HALO_LUT_N); // distance / radius
            const float k = clampf((1 - u) * (190.0f / 60.0f), 0, 1);
            s_halo_lut[i] = 255 * k * k * (3 - 2 * k);
        }
    }
    // Twice the distance, in whole numbers: (2x + 1 - size) from the middle.
    const float r2 = 2 * 190 * K * (float) size / (float) halo_full();
    const float idx_k = HALO_LUT_N / (r2 * r2);
    uint8_t *a8 = (uint8_t *) sp->img.data;
    for (int y = 0; y < size; y++) {
        const int dy = 2 * y + 1 - size;
        const uint8_t *bayer = BAYER[y & 3];
        for (int x = 0; x < size; x++) {
            const int dx = 2 * x + 1 - size;
            const int i = (int) ((float) (dx * dx + dy * dy) * idx_k);
            if (i >= HALO_LUT_N) {
                continue; // calloc'd
            }
            const float v = s_halo_lut[i] + ((float) bayer[x & 3] - 7.5f) * (24.0f / 16.0f);
            a8[y * size + x] = (uint8_t) clampf(v, 0, 255);
        }
    }
    return ESP_OK;
}

const look_sprite_t *look_halo(float scale, uint32_t frame)
{
    const int full = halo_full();
    // Down to the step, never up: the areas worked out at `scale` cover it.
    int size = (int) ((float) full * scale) / HALO_STEP * HALO_STEP;
    if (size >= full - 1) {
        return &s_look.halo;
    }
    size = size < 8 ? 8 : size;
    // An empty slot, or the one drawn longest ago (never one drawn this frame).
    halo_slot_t *pick = NULL;
    for (int i = 0; i < HALO_SLOTS; i++) {
        halo_slot_t *h = &s_halos[i];
        if (h->size == size) {
            h->used = frame;
            return &h->sp;
        }
        if (h->size == 0) {
            pick = pick == NULL || pick->size != 0 ? h : pick;
        } else if (h->used != frame && (pick == NULL || (pick->size != 0 && frame - h->used > frame - pick->used))) {
            pick = h;
        }
    }
    if (pick == NULL) {
        return NULL;
    }
    if (pick->size != 0) {
        heap_caps_free((void *) pick->sp.img.data);
        pick->size = 0;
    }
    if (render_halo_at(&pick->sp, size) != ESP_OK) {
        return NULL;
    }
    pick->size = size;
    pick->used = frame;
    return &pick->sp;
}

/* The hand at other sizes, kept like the halo, a pixel of radius apart. */
#define HAND_SLOTS 4
typedef struct {
    look_sprite_t sp;
    int rx; // 0: empty
    uint32_t used;
} hand_slot_t;
static hand_slot_t s_hands[HAND_SLOTS];


const look_sprite_t *look_hand(float scale, uint32_t frame)
{
    const int rx = (int) lroundf(HAND_RX * scale);
    if (rx < 3) {
        return NULL;
    }
    if (rx == (int) lroundf(HAND_RX)) {
        return &s_look.hand;
    }
    hand_slot_t *pick = NULL;
    for (int i = 0; i < HAND_SLOTS; i++) {
        hand_slot_t *h = &s_hands[i];
        if (h->rx == rx) {
            h->used = frame;
            return &h->sp;
        }
        if (h->rx == 0) {
            pick = pick == NULL || pick->rx != 0 ? h : pick;
        } else if (h->used != frame && (pick == NULL || (pick->rx != 0 && frame - h->used > frame - pick->used))) {
            pick = h;
        }
    }
    if (pick == NULL) {
        return NULL;
    }
    if (pick->rx != 0) {
        heap_caps_free((void *) pick->sp.img.data);
        pick->rx = 0;
    }
    // The boot one's shape and light, at this size.
    const float k = (float) rx / HAND_RX;
    if (render_ball(&pick->sp, (float) rx, 22 * K * k, -12 * K * k, -11 * K * k, 31 * K * k, STOPS_NOW) != ESP_OK) {
        return NULL;
    }
    pick->rx = rx;
    pick->used = frame;
    return &pick->sp;
}

int look_palette(void)
{
    return s_palette_wanted >= 0 ? s_palette_wanted : s_palette;
}

const char *look_palette_name(int i, bool italian)
{
    if (i < 0 || i >= LOOK_PALETTES) {
        return "?";
    }
    return italian ? PALETTES[i].it : PALETTES[i].en;
}

void look_set_palette(int i)
{
    if (i >= 0 && i < LOOK_PALETTES) {
        s_palette_wanted = i;
    }
}

/* The other sizes go (rendered again in the new colours when drawn), the day
 * sphere and the hand are rendered now. The new day sphere is made before
 * the old one goes: if PSRAM has no room, Yoru keeps its colours. */
bool look_apply_palette(void)
{
    const int want = s_palette_wanted;
    if (want < 0 || s_shade565 == NULL) {
        return false;
    }
    s_palette_wanted = -1;
    if (want == s_palette) {
        return false;
    }
    const int was = s_palette;
    for (int i = 0; i < BALL_SLOTS; i++) {
        if (s_balls[i].rx != 0) {
            ball_free(&s_balls[i]);
        }
    }
    for (int i = 0; i < HAND_SLOTS; i++) {
        if (s_hands[i].rx != 0) {
            heap_caps_free((void *) s_hands[i].sp.img.data);
            s_hands[i].rx = 0;
        }
    }
    s_palette = want;
    shade_table();
    look_sprite_t day, tint, core, hand;
    if (render_ball_fast(&day, &tint, &core, LOOK_SPHERE_R, LOOK_SPHERE_R) != ESP_OK) {
        ESP_LOGW(TAG, "no room for the %s sphere", PALETTES[want].en);
        s_palette = was;
        shade_table();
        return true;
    }
    if (render_ball(&hand, HAND_RX, 22 * K, -12 * K, -11 * K, 31 * K, STOPS_NOW) != ESP_OK) {
        hand = s_look.hand; // the old hand, rather than none
    } else {
        heap_caps_free((void *) s_look.hand.img.data);
    }
    heap_caps_free((void *) s_look.day.img.data);
    s_look.day = day;
    s_look.tint = tint;
    s_day_core = core;
    s_look.hand = hand;
    ESP_LOGI(TAG, "sphere: %s", PALETTES[want].en);
    return true;
}

const look_t *look_get(void)
{
    return &s_look;
}

/* Time and pixels of the sprites drawn as they are and scaled or turned
 * (debug/perf): a transform costs several times as much a pixel. */
static uint64_t s_draw_us[2], s_draw_px[2], s_draw_n[2];

void look_draw_stats(uint64_t us[2], uint64_t px[2], uint64_t n[2], bool reset)
{
    memcpy(us, s_draw_us, sizeof(s_draw_us));
    memcpy(px, s_draw_px, sizeof(s_draw_px));
    memcpy(n, s_draw_n, sizeof(s_draw_n));
    if (reset) {
        memset(s_draw_us, 0, sizeof(s_draw_us));
        memset(s_draw_px, 0, sizeof(s_draw_px));
        memset(s_draw_n, 0, sizeof(s_draw_n));
    }
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
    const int64_t t0 = esp_timer_get_time();
    lv_draw_image(layer, &dsc, &coords);
    const int k = dsc.scale_x != LV_SCALE_NONE || dsc.scale_y != LV_SCALE_NONE || dsc.rotation != 0;
    s_draw_us[k] += (uint64_t) (esp_timer_get_time() - t0);
    s_draw_n[k]++;
    const lv_area_t a = look_area(sp, x, y, scale_x, scale_y, dsc.rotation != 0), *c = &layer->_clip_area;
    const int32_t w = LV_MIN(a.x2, c->x2) - LV_MAX(a.x1, c->x1) + 1, h = LV_MIN(a.y2, c->y2) - LV_MAX(a.y1, c->y1) + 1;
    if (w > 0 && h > 0) {
        s_draw_px[k] += (uint64_t) (w * h);
    }
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
