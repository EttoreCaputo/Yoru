#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** CST9217 on the shared I2C bus. */
esp_err_t board_touch_init(void);

/** Poll the controller. True with the point in screen pixels while a
 * finger is down. */
bool board_touch_read(uint16_t *x, uint16_t *y);

/** Put the controller to sleep while the screen is dark (it scans, and
 * draws current, otherwise), or wake it with a reset. Not while
 * board_touch_read() may run: the caller has stopped LVGL's reads. */
void board_touch_set_sleep(bool sleep);

#ifdef __cplusplus
}
#endif
