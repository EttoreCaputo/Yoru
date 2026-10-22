#pragma once

/* The Games app of the menu (menu.c): Yoru's mini-games, as round bubbles
 * around him: Tilt ball (game_tilt.c), Slingshot (game_sling.c) and
 * Constellations (game_stars.c). Tap one to play, tap a game's title to
 * come back here; tap this title or swipe right to go back to the menu.
 * All calls with the LVGL lock held. */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The page, full screen on the top layer; `back` when you go back. It
 * goes when the menu deletes it. */
lv_obj_t *games_ui_build(void (*back)(void));
/** Every second while it is up. */
void games_ui_refresh(void);

#ifdef __cplusplus
}
#endif
