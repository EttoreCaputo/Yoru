#pragma once

/* The coding agents on the screen (agents.c has what they're doing):
 * - a dot per session along the top of the rim, coloured by its state:
 *   blue breathing while it works, amber pulsing while it waits for you,
 *   green when done, red on an error;
 * - a line over the bottom rim: the host's toasts (show_toast), else the
 *   step the working session is on;
 * - the agents view (a swipe up from the bottom edge, the BOOT button, the
 *   menu): a little Yoru per session in its agent's colour (Claude orange,
 *   Gemini yellow, Codex blue, Cursor green...), acting out its state:
 *   bobbing with thinking dots while it works, hopping with a "!" while it
 *   waits for you, ^ ^ when done, x x and a shiver on an error, dozing when
 *   idle. Its project underneath; tap one for its chat (remote_ui.c), hold
 *   it to interrupt the agent; "New" to start a session in a project;
 *   "Quiet 1 h" and the plan's limits at the bottom.
 * The dots and the line keep out of the way of the menu and the cards. All
 * calls with the LVGL lock held. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agents.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { TOAST_INFO, TOAST_DONE, TOAST_WAIT, TOAST_ERROR } toast_tone_t;

/** Told when the view opens and closes. */
typedef void (*agents_ui_cb_t)(bool open);

/** `face` is hidden while the view covers the screen, so it isn't drawn. */
void agents_ui_init(lv_obj_t *face, agents_ui_cb_t cb);

/** One line over the bottom rim for `seconds`, replacing the one up. UTF-8. */
void agents_ui_toast(const char *text, int seconds, toast_tone_t tone);

void agents_ui_open(void);
void agents_ui_close(void);
bool agents_ui_is_open(void);

/** Ask the host for a shortcut (docs/protocol.md, Shortcuts): `focus`,
 * `interrupt` or `quiet`; `session` NULL for none. False, with a toast, when
 * no host is listening. */
bool agents_ui_shortcut(const char *name, const char *session);

/** The toast up now, if any, for pages that cover the line (remote_ui.c). */
bool agents_ui_toast_now(char *out, size_t len, lv_color_t *color);

/** What the view and its pages share: a state's colour, an agent's colour
 * by its name, a step as "edit agents.c  +42 -3". */
lv_color_t agents_ui_state_color(agent_state_t st);
uint32_t agents_ui_agent_rgb(const char *agent);
void agents_ui_step_text(const agent_step_t *st, char *out, size_t len);

#ifdef __cplusplus
}
#endif
