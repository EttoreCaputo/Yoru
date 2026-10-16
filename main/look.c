#include "look.h"

#include <math.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

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
/* The squashed spheres, by level + LOOK_SQUASH_STEPS (the middle one is
 * s_look.day), and which are ready. */
static look_sprite_t s_squashed[2 * LOOK_SQUASH_STEPS + 1];
static volatile bool s_squashed_ready[2 * LOOK_SQUASH_STEPS + 1];

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* The panel shows 32 levels of red and blue and 64 of green: a smooth
 * gradient turns into visible rings. A 4x4 ordered dither spreads each step
 * over its neighbours, too fine to see at this pixel size. 0..1. */
static float dither(int x, int y)
{
    static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
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
static esp_err_t render_ball(look_sprite_t *sp, float rx, float ry, float light_x, float light_y, float light_r)
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
            gradient(PEARL, 5, hypotf(px - light_x, py - light_y) / light_r, rgb);
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

/* The halo: a soft disc that shows around the sphere. */
static esp_err_t render_halo(look_sprite_t *sp)
{
    const int size = ((int) (456 * K)) & ~1;
    ESP_RETURN_ON_ERROR(sprite_alloc(sp, size, size, LV_COLOR_FORMAT_A8, size / 2, size / 2), TAG, "halo");
    uint8_t *a8 = (uint8_t *) sp->img.data;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float d = hypotf((float) x + 0.5f - size / 2.0f, (float) y + 0.5f - size / 2.0f);
            float k = clampf((226 * K - d) / (96 * K), 0, 1);
            // Drawn at about a third: a colour step is three times the alpha.
            a8[y * size + x] = dither_a8(255 * k * k * (3 - 2 * k), x, y, 24);
        }
    }
    return ESP_OK;
}

/* The sphere squashed by `level`: wider and shorter (or the other way), the
 * light moving with the surface, as if the round one were scaled. */
static esp_err_t render_squashed(look_sprite_t *sp, int level)
{
    const float sq = LOOK_SQUASH * (float) level / LOOK_SQUASH_STEPS;
    const float sx = 1 + sq, sy = 1 - sq, r = LOOK_SPHERE_R;
    return render_ball(sp, r * sx, r * sy, LIGHT_X * sx, LIGHT_Y * sy, LIGHT_R);
}

/* The squashed spheres aren't needed at boot: they are made at low priority
 * while Yoru is already on screen, the small squashes first. */
static void squash_task(void *arg)
{
    const int64_t start = esp_timer_get_time();
    for (int step = 1; step <= LOOK_SQUASH_STEPS; step++) {
        for (int sign = 1; sign >= -1; sign -= 2) {
            const int i = LOOK_SQUASH_STEPS + sign * step;
            if (render_squashed(&s_squashed[i], sign * step) != ESP_OK) {
                ESP_LOGE(TAG, "squashed sphere %d", sign * step);
                vTaskDeleteWithCaps(NULL);
            }
            s_squashed_ready[i] = true;
        }
    }
    ESP_LOGI(TAG, "squashed spheres ready in %lld ms, PSRAM free %u KB", (esp_timer_get_time() - start) / 1000,
             (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    vTaskDeleteWithCaps(NULL);
}

esp_err_t look_init(void)
{
    const int64_t start = esp_timer_get_time();
    const float r = LOOK_SPHERE_R;
    ESP_RETURN_ON_ERROR(render_ball(&s_look.day, r, r, LIGHT_X, LIGHT_Y, LIGHT_R), TAG, "day");
    ESP_RETURN_ON_ERROR(render_tint(&s_look.tint), TAG, "tint");
    ESP_RETURN_ON_ERROR(render_halo(&s_look.halo), TAG, "halo");
    ESP_RETURN_ON_ERROR(render_ball(&s_look.hand, 27 * K, 22 * K, -12 * K, -11 * K, 31 * K), TAG, "hand");
    ESP_LOGI(TAG, "Yoru rendered in %lld ms", (esp_timer_get_time() - start) / 1000);
    // Lowest priority, stack in PSRAM: it only computes (0.3.1's rule).
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(squash_task, "look", 4096, NULL, 1, NULL, 0,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "squash task");
    return ESP_OK;
}

const look_sprite_t *look_day(int level)
{
    if (level == 0) {
        return &s_look.day;
    }
    if (level < -LOOK_SQUASH_STEPS || level > LOOK_SQUASH_STEPS) {
        return NULL;
    }
    const int i = level + LOOK_SQUASH_STEPS;
    return s_squashed_ready[i] ? &s_squashed[i] : NULL;
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
