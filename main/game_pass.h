#pragma once

/* Pass the ball (games_ui.c): with another Yoru nearby, roll a ball off the
 * edge facing it and into its screen, and back (docs/FRIENDS.md). All calls
 * with the LVGL lock held. */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The game, full screen in `parent`; `quit` (called later, never from
 * inside an LVGL event) when you tap the title. It stops when deleted. */
lv_obj_t *game_pass_start(lv_obj_t *parent, void (*quit)(void));

/** Its picture in a launcher bubble of radius r. */
void game_pass_icon(lv_layer_t *layer, float x, float y, float r);

#ifdef __cplusplus
}
#endif
