#pragma once

/* What Yoru's mini-games share (games_ui.c, game_*.c): Yoru himself at any
 * size with a few faces, discs and text drawn by hand, the dirty areas a
 * moving thing leaves, the tilt of the case and a few saved numbers.
 *
 * Every game is one full-screen object that draws everything itself in
 * LV_EVENT_DRAW_MAIN and invalidates only the boxes of what moved: on this
 * board a full frame costs ~50 ms, a ball's box well under 1 ms. All calls
 * with the LVGL lock held. */

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KIT_CX (BOARD_LCD_H_RES / 2)
#define KIT_CY (BOARD_LCD_V_RES / 2)
#define KIT_SCREEN_R (BOARD_LCD_H_RES / 2)

typedef enum {
    KIT_EYES_OPEN,  // his pills, looking where gx, gy say
    KIT_EYES_WIDE,  // round and big: scared, amazed
    KIT_EYES_HAPPY, // ^ ^
    KIT_EYES_SHUT,  // two dashes: ouch, or bracing
    KIT_EYES_SAD,   // low and drooping
} kit_eyes_t;

typedef struct {
    float gx, gy; // gaze, -1..1
    float lid;    // 1 open, 0 shut (a blink)
    kit_eyes_t eyes;
} kit_face_t;

/** Yoru's sphere with radii rx x ry (squashed when they differ) centred on
 * (x, y), and his face on it. */
void kit_yoru(lv_layer_t *layer, float x, float y, float rx, float ry, const kit_face_t *face);
/** The box he covers drawn that way. */
lv_area_t kit_yoru_area(float x, float y, float rx, float ry);

/** A blink now and then: the lid for kit_face_t, 1 most of the time. */
float kit_blink(uint32_t now);

void kit_disc(lv_layer_t *layer, float x, float y, float r, lv_color_t color);
void kit_rect(lv_layer_t *layer, const lv_area_t *a, int32_t radius, lv_color_t color);
/** A ring of width w, `r` its outer edge, from angle a0 to a1 (degrees,
 * 0 = right, clockwise). */
void kit_arc(lv_layer_t *layer, float x, float y, float r, float w, int32_t a0, int32_t a1, lv_color_t color);
/** One line of text centred on (x, y). */
void kit_text(lv_layer_t *layer, const lv_font_t *font, float x, float y, lv_color_t color, const char *str);
/** The box kit_text fills. */
lv_area_t kit_text_area(const lv_font_t *font, float x, float y, const char *str);
/** `c` at `k` of its brightness, over black (instead of an opacity, which
 * LVGL blends much slower). */
lv_color_t kit_dim(lv_color_t c, float k);

/** The area around a disc. */
lv_area_t kit_disc_area(float x, float y, float r);
bool kit_overlap(const lv_area_t *a, const lv_area_t *b);
/** Something drawn at `*drawn` is now at `now`: invalidate both (as one
 * box when they touch) and remember the new one. */
void kit_moved(lv_obj_t *obj, lv_area_t *drawn, lv_area_t now);
/** Invalidate it, clipped to the screen. */
void kit_dirty(lv_obj_t *obj, lv_area_t a);

/** The tilt from now on is measured from how the case lies now. */
void kit_tilt_zero(void);
/** Which way and how much "down" points along the screen, in g, from how
 * the case lay at kit_tilt_zero(), in the picture's axes (the screen may be
 * turned). */
void kit_tilt(float *x, float *y);

/** A number kept across restarts (NVS), 0 until saved. */
int32_t kit_load(const char *key);
void kit_save(const char *key, int32_t value);

static inline float kit_clamp(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

#ifdef __cplusplus
}
#endif
