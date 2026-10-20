#pragma once

/* Yoru runs an errand (docs/protocol.md, Fetch): while the host's assistant
 * calls one of its connectors (Gmail, Calendar, GitHub...), a little portal
 * in the connector's colour opens on the rim; Yoru looks at it, puts its
 * hands together over its head and dives in. Inside, the portal wobbles and
 * sparkles as it rummages about. When the call is done it hops back out with
 * a parcel in its hands, comes home and opens it in a burst of sparkles; if
 * the call failed it comes back empty-handed and sad, in a puff of dust.
 * It moves the eyes (eyes.c) and draws the portal and the sparkles on its
 * own layer over them. behaviour.c says when it may run. All calls with the
 * LVGL lock held. */

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Told when an errand starts (true) and ends (false). */
typedef void (*errand_cb_t)(bool busy);

/** The layer goes on `parent`, over the face built there before. */
void errand_init(lv_obj_t *parent, errand_cb_t cb);

/** A connector call began: `id` pairs it with its end ("" for none),
 * `source` names the connector ("Gmail"). A second one while Yoru is out
 * joins the same errand. */
void errand_start(const char *id, const char *source);
/** That call ended, well or not. Yoru comes back once all have, and has been
 * inside a moment at least. An id that isn't out is fine. */
void errand_end(const char *id, bool ok);

/** An errand has the eyes now. */
bool errand_busy(void);
/** Something else needs Yoru: the portal goes at once; whoever calls moves
 * Yoru home. */
void errand_abort(void);

#ifdef __cplusplus
}
#endif
