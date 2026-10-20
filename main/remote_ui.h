#pragma once

/* The agents view's own pages (docs/protocol.md, Remote):
 * - a session's chat: its project and state, the conversation as bubbles
 *   (yours right, the agent's left, from the host's `set_chat`) fading out
 *   at the box's edges, and "hold PWR to talk", which says "listening"
 *   while PWR is held;
 * - "New": the agents and the projects the host listed (`set_projects`);
 *   pick one of each, then hold PWR and say what to do.
 * Each page says where PWR's words go (remote_ui_target()). A swipe right or
 * down, or a tap on the title, goes back to the view. All calls with the
 * LVGL lock held. */

#include <stdbool.h>
#include <stddef.h>

#include "agents.h"

#ifdef __cplusplus
extern "C" {
#endif

void remote_ui_chat_open(const char *session);
void remote_ui_new_open(void);
void remote_ui_close(void);
bool remote_ui_is_open(void);

/** Where push-to-talk goes from the page up, as `utterance_start`'s `to`
 * (a JSON object as text), or "" for none. */
void remote_ui_target(char *out, size_t len);

/** Keep the page up to date: agents_ui.c's poll, with its copy of the
 * sessions. */
void remote_ui_poll(const agents_t *agents);

#ifdef __cplusplus
}
#endif
