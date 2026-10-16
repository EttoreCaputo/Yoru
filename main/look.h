#pragma once

/* Yoru's look, rendered once at boot into PSRAM (docs/CONCEPT.md, "On the
 * board"): the shaded sphere, the tint and the halo
 * as alpha maps to colour at draw time, and the hands. eyes.c only places
 * and blends these; the shapes come from the Rive mascot
 * (docs/mascot/yoru.riv). */

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How big Yoru is on the 466 px screen. Everything is designed at 1 (a
 * 280 px sphere, as in the Rive file) and drawn at this scale: at 0.7 the
 * sphere is about 40% of the disc, small enough to float and cheap to draw. */
#define LOOK_SCALE 0.7f
#define LOOK_SPHERE_R 98
/* How much a full poke (1) or stretch (-1) squashes the body. LVGL scaling
 * the sphere costs about 80 ms a frame on this chip, so the squashed
 * spheres are rendered ahead, this many steps on each side. */
#define LOOK_SQUASH 0.12f
#define LOOK_SQUASH_STEPS 8

/* A sprite and where its anchor (the point it is placed by) sits in it. */
typedef struct {
    lv_image_dsc_t img;
    int32_t ax, ay;
} look_sprite_t;

/* The pictures with colour are RGB565A8: the panel's own format plus alpha,
 * a quarter smaller than ARGB and much quicker to blend. */
typedef struct {
    look_sprite_t day;   // the lit sphere
    look_sprite_t tint;  // the lower part of the sphere, A8
    look_sprite_t halo;  // a soft disc around it, A8
    look_sprite_t hand;  // a small flattened sphere
} look_t;

/** Render Yoru's pictures now (call once before eyes_create()) and start
 * rendering the squashed spheres in a background task. */
esp_err_t look_init(void);

/** The lit sphere squashed by `level` / LOOK_SQUASH_STEPS (-steps..steps,
 * positive wider), drawn at scale 1; NULL while it isn't rendered yet. */
const look_sprite_t *look_day(int level);

const look_t *look_get(void);

/** Draw a sprite with its anchor at (x, y), scaled and turned (degrees)
 * around the anchor. A8 sprites take `color`. */
void look_draw(lv_layer_t *layer, const look_sprite_t *sp, int32_t x, int32_t y, float scale_x, float scale_y,
               float rotation, lv_opa_t opa, lv_color_t color);

/** The area a sprite covers when drawn that way (a bounding box, generous
 * for rotation). */
lv_area_t look_area(const look_sprite_t *sp, int32_t x, int32_t y, float scale_x, float scale_y, bool rotated);

#ifdef __cplusplus
}
#endif
