#pragma once

/* The BOOT button (GPIO 0) as a shortcut once Yoru is running: read on an
 * LVGL timer, so its callback runs on the LVGL task with the lock held, the
 * way touch does. A press under a second is short; held longer it fires
 * once as long, without waiting for the release. */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*board_button_cb_t)(bool long_press);

/** With the LVGL lock held. */
void board_button_init(board_button_cb_t cb);

#ifdef __cplusplus
}
#endif
