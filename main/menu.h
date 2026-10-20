#pragma once

/* The menu on the board (docs/ROADMAP.md, M2), made for the round screen as
 * a watch's: a honeycomb of round apps around Yoru, who peeks out from the
 * middle. They fly out of it as the menu opens, and badges and rings on them
 * say what is new: Notifications (the inbox), Timers, Habits (today's, as
 * rings), Settings (every setting, in categories with round icons), Do not
 * disturb (a moon to tap) and Agents. A tapped app grows from its icon to
 * the whole screen. Changes apply at once and are saved by board_settings.c.
 * Tap Yoru or swipe up on the home to close, tap a title or swipe right to
 * go back, or wait, and it closes. Labels in English or Italian
 * (SETTING_LANGUAGE). All calls with the LVGL lock held. */

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
/** Open straight on a page by its English name, lower case: an app
 * ("inbox", "timers", "habits", "settings"), a category of Settings
 * ("display", "sound", ...) or "home", for the host; false for an unknown
 * one. */
bool menu_open_page(const char *name);
void menu_close(void);
bool menu_is_open(void);

#ifdef __cplusplus
}
#endif
