#pragma once

/* Tilt ball (games_ui.c): roll Yoru home through the dark by tilting the
 * case, past walls and holes, eating the fireflies on the way. All calls
 * with the LVGL lock held. */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The game, full screen in `parent`; `quit` (called later, never from
 * inside an LVGL event) when you tap the title. It stops when deleted. */
lv_obj_t *game_tilt_start(lv_obj_t *parent, void (*quit)(void));

/** Its picture in a launcher bubble of radius r. */
void game_tilt_icon(lv_layer_t *layer, float x, float y, float r);

#ifdef __cplusplus
}
#endif
