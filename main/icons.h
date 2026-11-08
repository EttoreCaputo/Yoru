#pragma once

/* The apps' icons the host sends (`set_icon`, docs/protocol.md, Icons): kept
 * in PSRAM by the service's name, drawn on the clips' card, in the errand's
 * portal and on the recording mark. Call with the LVGL lock held. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#define ICON_SIDE 48
/* RGB565 then an alpha plane (LVGL's RGB565A8). */
#define ICON_BYTES (ICON_SIDE * ICON_SIDE * 3)
#define ICON_NAME_MAX 24
/* The most raw bytes in one `set_icon` part: its base64 fits a link frame. */
#define ICON_PART_MAX 2048

/* Part of `name`'s icon: `len` bytes at `at`. A part at 0 starts it over;
 * parts must come in order. False with why in `err`. */
bool icons_put(const char *name, uint32_t at, const uint8_t *data, size_t len, const char **err);

/* The whole icon for `name` (case-insensitive), or NULL. */
const lv_image_dsc_t *icons_find(const char *name);

/* How many icons are kept whole. */
int icons_count(void);

/* `icon` drawn as a square `side` pixels wide centred on (x, y). */
void icons_draw(lv_layer_t *layer, const lv_image_dsc_t *icon, float x, float y, float side, lv_opa_t opa);
