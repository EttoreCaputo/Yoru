#pragma once

/* The inbox on the screen (inbox.c has what's in it):
 * - a dot at the top right of the rim while something is unread: pearl,
 *   red and pulsing when one of them is high or urgent;
 * - a banner over the bottom rim as one arrives (normal and louder): its
 *   source and title for a few seconds; a tap opens it;
 * - the inbox view (the banner, BOOT with something unread, the menu): the
 *   list, newest first, with each one's source, title and age, the unread
 *   marked; a tap opens one: its whole text, the host's actions, Dismiss
 *   and Open (on the computer). Swipe right or down, or tap the title, to
 *   go back, as in the agents view.
 * The dot and the banner keep out of the way of the menu, the views and the
 * cards. All calls with the LVGL lock held. */

#include <stdbool.h>

#include "inbox.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Told when the view opens and closes (the same as the agents view's). */
typedef void (*inbox_ui_cb_t)(bool open);

/** `face` is hidden while the view covers the screen, so it isn't drawn. */
void inbox_ui_init(lv_obj_t *face, inbox_ui_cb_t cb);

/** The banner for the newest one, as loud as `urgency` (normal and up). */
void inbox_ui_banner(inbox_urgency_t urgency);

/** The list; or, with an id, that one's detail over it. */
void inbox_ui_open(const char *id);
void inbox_ui_close(void);
bool inbox_ui_is_open(void);

#ifdef __cplusplus
}
#endif
