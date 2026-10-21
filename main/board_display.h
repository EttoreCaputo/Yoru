#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
} board_lcd_t;

/** Bring up the QSPI bus and the CO5300, its clock at 40 or 80 MHz, its
 * pixels RGB565 or, `deep`, RGB888. The panel starts dark (brightness 0) so
 * nothing shows before the first frame. */
esp_err_t board_display_init(board_lcd_t *out, bool fast, bool deep);

/** 0 (off) to 100 %. An AMOLED has no backlight: this is DCS 0x51. */
esp_err_t board_display_set_brightness(int percent);

#ifdef __cplusplus
}
#endif
