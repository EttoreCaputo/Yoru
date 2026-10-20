#pragma once

/* What was said to an agent, before it goes (docs/protocol.md, Remote,
 * Drafts): the words the host heard, where they go, Send and Cancel, and a
 * ring along the rim running the time down. Holding PWR over it says it
 * again (draft_redo()). The answer goes back with the `draft` notification.
 * One at a time: a new one replaces it. All calls with the LVGL lock held. */

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DRAFT_ID_MAX 47
#define DRAFT_TEXT_CHARS 400
#define DRAFT_TO_CHARS 40

/** Told when a draft comes up and goes: `sent` 1 when sent from here, 0 when
 * cancelled here (or said again), -1 for none (taken away, timed out). */
typedef void (*draft_cb_t)(bool open, int sent);

void draft_init(draft_cb_t cb);

/** Show the words (UTF-8) for `seconds`, replacing the draft up. */
void draft_show(const char *id, const char *text, const char *to, int seconds);

/** Take the draft away if it shows `id`; false if it didn't. */
bool draft_hide(const char *id);

/** The words are to be said again: tell the host (`redo`) and close. False
 * without a draft up. */
bool draft_redo(void);

bool draft_is_open(void);

#ifdef __cplusplus
}
#endif
