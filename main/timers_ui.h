#pragma once

/* The Timers app of the menu (menu.c): the host's timers and reminders
 * (timers.c), the one that ends first first. Each has a ring that runs
 * down and its time left in big digits; a ringing one glows orange. Tap a
 * timer to pause or resume it, tap a ringing one to stop it, hold one to
 * cancel it. Tap the title or swipe right to go back. All calls with the
 * LVGL lock held. */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The page, full screen on the top layer; `back` when you go back. It
 * goes when the menu deletes it. */
lv_obj_t *timers_ui_build(void (*back)(void));
/** Every second while it is up: the countdowns, and a new list if the
 * host sent one. Nothing when it isn't up. */
void timers_ui_refresh(void);

#ifdef __cplusplus
}
#endif
