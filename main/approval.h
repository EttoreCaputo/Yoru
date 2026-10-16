#pragma once

/* A Claude Code permission request on the board (docs/protocol.md,
 * Approvals): a card around Yoru with the tool and what it would do on
 * top, Deny and Allow below with the project and the seconds left, and a
 * ring along the rim that runs them down. A tap answers with the `approval`
 * notification; the card also goes when the host takes it away or the time
 * is up. One at a time: a new one replaces it. All calls with the LVGL lock
 * held. */

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The longest id the card keeps; a longer one is refused. */
#define APPROVAL_ID_MAX 47

/** Told when a card comes up and goes, so Yoru can wait for the answer.
 * `allow` is the answer given on the board: 1 allowed, 0 denied, -1 none
 * (taken away, timed out, replaced). */
typedef void (*approval_cb_t)(bool open, int allow);

void approval_init(approval_cb_t cb);

/** Show a request for `seconds`, replacing the one up. `text` and `project`
 * (NULL for none) are UTF-8; what the fonts can't draw is spelt in ASCII. */
void approval_show(const char *id, const char *tool, const char *text, const char *project, int seconds);

/** Take the card away if it shows `id`; false if it didn't. */
bool approval_hide(const char *id);

bool approval_is_open(void);

#ifdef __cplusplus
}
#endif
