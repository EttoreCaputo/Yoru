#pragma once

/* Constellations (games_ui.c): draw the sky Yoru comes from, dragging him
 * from star to star along every line in one stroke. All calls with the LVGL
 * lock held. */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The game, full screen in `parent`; `quit` (called later, never from
 * inside an LVGL event) when you tap the title. It stops when deleted. */
lv_obj_t *game_stars_start(lv_obj_t *parent, void (*quit)(void));

/** Its picture in a launcher bubble of radius r. */
void game_stars_icon(lv_layer_t *layer, float x, float y, float r);

#ifdef __cplusplus
}
#endif
