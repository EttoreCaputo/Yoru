#pragma once

/* The Tasks app of the menu (menu.c): the jobs the user gave Yoru
 * (tasks.c). A row for each, with a dot for how it went last time, when it
 * runs and when next; a tap opens it: what to do, what Yoru did last time,
 * and "Do it now" (the host does it). Tap the title or swipe right to go
 * back. All calls with the LVGL lock held. */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The page, full screen on the top layer; `back` when you go back. It
 * goes when the menu deletes it. */
lv_obj_t *tasks_ui_build(void (*back)(void));
/** Every second while it is up: a new list if the host sent one. */
void tasks_ui_refresh(void);

#ifdef __cplusplus
}
#endif
