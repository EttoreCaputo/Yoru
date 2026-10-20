#pragma once

/* The Habits app of the menu (menu.c): today's habits (habits.c). On top,
 * their rings, as a watch's Activity, around how many are done; under them
 * a row for each with a circle to tick, its count and its streak. A tap
 * ticks one off with a pop and a few sparkles (confetti when the day is
 * done), a hold takes one back. Tap the title or swipe right to go back. All
 * calls with the LVGL lock held. */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The page, full screen on the top layer; `back` when you go back. It
 * goes when the menu deletes it. */
lv_obj_t *habits_ui_build(void (*back)(void));
/** Every second while it is up: a new list if the host sent one. */
void habits_ui_refresh(void);

#ifdef __cplusplus
}
#endif
