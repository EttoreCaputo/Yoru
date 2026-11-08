#pragma once

/* The recording mark: while the host records a meeting, a small pill at the
 * top of the screen with a red dot that breathes and the app's icon (or its
 * glyph). It keeps out of the way of the menu, the inbox and the
 * confirmation card. All calls with the LVGL lock held. */

#include <stdbool.h>
#include <stdint.h>

void recmark_init(void);

/* Show it for `source` (the app recorded), in `rgb` with `glyph` when there
 * is no icon; NULL source hides it. */
void recmark_set(const char *source, uint32_t rgb, const char *glyph);

bool recmark_shown(void);
