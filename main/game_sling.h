#pragma once

/* Slingshot (games_ui.c): pull Yoru back and let go, and he flies home
 * across the dark, his path bent by little planets and black holes. All
 * calls with the LVGL lock held. */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The game, full screen in `parent`; `quit` (called later, never from
 * inside an LVGL event) when you tap the title. It stops when deleted. */
lv_obj_t *game_sling_start(lv_obj_t *parent, void (*quit)(void));

/** Its picture in a launcher bubble of radius r. */
void game_sling_icon(lv_layer_t *layer, float x, float y, float r);

#ifdef __cplusplus
}
#endif
