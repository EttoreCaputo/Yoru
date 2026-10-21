#pragma once

/* One line over the bottom rim for a few seconds: the host's `show_toast`
 * and Yoru's own short notes (a timer that rang, the mic muted). It keeps
 * out of the way of the menu, the inbox and the confirmation card, and
 * shows again once they go if its time isn't up. All calls with the LVGL lock held. */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { TOAST_INFO, TOAST_DONE, TOAST_WAIT, TOAST_ERROR } toast_tone_t;

void toast_init(void);

/** `text` (UTF-8) for `seconds`, replacing the one up. */
void toast_show(const char *text, int seconds, toast_tone_t tone);

#ifdef __cplusplus
}
#endif
