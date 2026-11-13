#pragma once

/* Yoru's look, rendered into PSRAM (docs/CONCEPT.md, "On the board"): the
 * shaded sphere (at boot, and at other sizes as Yoru moves nearer or
 * farther), the tint and the halo as alpha maps to colour at draw time, and
 * the hands. eyes.c only places
 * and blends these; the shapes come from the Rive mascot
 * (docs/mascot/yoru.riv). */

#include <stdint.h>

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
/* How much a full poke (1) or stretch (-1) squashes the body. */
#define LOOK_SQUASH 0.12f

/* A sprite and where its anchor (the point it is placed by) sits in it. */
typedef struct {
    lv_image_dsc_t img;
    int32_t ax, ay;
} look_sprite_t;

/* The pictures with colour are RGB565A8: the panel's own format plus alpha,
 * a quarter smaller than ARGB and much quicker to blend. */
typedef struct {
    look_sprite_t day;   // the lit sphere
    look_sprite_t tint;  // the lower part of the sphere, A8 (day's)
    look_sprite_t halo;  // a soft disc around it, A8
    look_sprite_t hand;  // a small flattened sphere
} look_t;

/** The sphere's colours: how many, which is in use (or asked for), and its
 * name in the menu. */
#define LOOK_PALETTES 7
int look_palette(void);
const char *look_palette_name(int i, bool italian);
/** Ask for palette `i`, from any task: before look_init() it is the one
 * rendered at boot, after it look_apply_palette() renders it. */
void look_set_palette(int i);
/** On LVGL's task (eyes.c, each frame): render the palette asked for. True
 * when the spheres changed: a sprite of before is no longer valid. */
bool look_apply_palette(void);

/** Render Yoru's pictures now (call once before eyes_create()). */
esp_err_t look_init(void);

/** The lit sphere with radii `rx` x `ry` pixels (squashed when they differ),
 * for any distance and squash: LVGL scaling the sphere costs about 80 ms a
 * frame on this chip, so each size is rendered once, in a few ms, and kept
 * while it is used. Drawn at scale 1. A sphere returned this frame stays
 * valid until the next call with `frame` changed. NULL if memory runs out. */
const look_sprite_t *look_ball(int rx, int ry, uint32_t frame);

/** The tint of a sphere look_ball() gave (or look_get()'s day), its size,
 * drawn at scale 1 on it; NULL for any other sprite. */
const look_sprite_t *look_ball_tint(const look_sprite_t *ball);

/** The same sphere's colour alone (RGB565, no alpha), to copy where it
 * is opaque: about twice as quick as blending. NULL as above. */
const look_sprite_t *look_ball_core(const look_sprite_t *ball);

/** The halo at `scale` (at most 1), rendered and kept like the spheres,
 * so it is drawn at scale 1: LVGL scaling it costs four times as much a
 * pixel. A little smaller than `scale` (steps of a few px), never bigger.
 * NULL if memory runs out. */
const look_sprite_t *look_halo(float scale, uint32_t frame);

/** The hand at `scale`, rendered and kept like the halo, to draw at scale
 * 1: a little off `scale` (to the nearest pixel of radius). NULL when tiny
 * or out of memory. */
const look_sprite_t *look_hand(float scale, uint32_t frame);

const look_t *look_get(void);

/** Draw a sprite with its anchor at (x, y), scaled and turned (degrees)
 * around the anchor. A8 sprites take `color`. */
void look_draw(lv_layer_t *layer, const look_sprite_t *sp, int32_t x, int32_t y, float scale_x, float scale_y,
               float rotation, lv_opa_t opa, lv_color_t color);

/** The area a sprite covers when drawn that way (a bounding box, generous
 * for rotation). */
/** Time (us), pixels and draws of the sprites drawn [0] as they are, [1]
 * scaled or turned, since the last reset (debug/perf). */
void look_draw_stats(uint64_t us[2], uint64_t px[2], uint64_t n[2], bool reset);

lv_area_t look_area(const look_sprite_t *sp, int32_t x, int32_t y, float scale_x, float scale_y, bool rotated);

#ifdef __cplusplus
}
#endif
