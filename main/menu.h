#pragma once

/* The settings menu on the board (docs/ROADMAP.md, M2), made for the round
 * screen: a ring of icons around Yoru, who peeks up at it; each opens a
 * round list whose rows fade towards the rim, and a range opens an arc that
 * the finger drags along the edge. Changes apply at once and are saved by
 * board_settings.c. Swipe up on the ring, tap the title to go back, or wait,
 * and it closes. Labels in English or Italian (SETTING_LANGUAGE). All calls
 * with the LVGL lock held. */

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Told when the menu opens and closes, so Yoru can peek at it. */
typedef void (*menu_cb_t)(bool open);

/** `face` is hidden while a page covers the screen, so it isn't drawn. */
void menu_init(lv_obj_t *face, menu_cb_t cb);

void menu_open(void);
/** Open straight on a page by its English name, lower case ("display",
 * "sound", ...), for the host; false for an unknown one. */
bool menu_open_page(const char *name);
void menu_close(void);
bool menu_is_open(void);

#ifdef __cplusplus
}
#endif
