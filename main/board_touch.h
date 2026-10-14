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

#ifdef __cplusplus
}
#endif
