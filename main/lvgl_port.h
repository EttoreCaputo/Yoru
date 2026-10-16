#pragma once

#include "board_display.h"
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** LVGL on the panel, with the touch screen as its pointer. Runs LVGL in
 * its own task on core 0. */
esp_err_t lvgl_port_init(const board_lcd_t *lcd, lv_display_t **out_display);

/** Turn what's drawn by `quarter` turns (0..3, LVGL's rotation: 1 is 90°
 * clockwise). Touch follows. With the LVGL lock held. */
void lvgl_port_set_rotation(int quarter);
int lvgl_port_rotation(void);

void lvgl_port_lock(void);
void lvgl_port_unlock(void);

/** How hard LVGL worked over the last full window of LVGL_PORT_STATS_WINDOW_MS:
 * the share of time in lv_timer_handler() and its longest single run. */
#define LVGL_PORT_STATS_WINDOW_MS 5000
typedef struct {
    float busy_pct;
    uint32_t max_us;
} lvgl_port_stats_t;

void lvgl_port_get_stats(lvgl_port_stats_t *out);

/** How long the panel's refreshes take, from its TE pin; 0 without TE. */
int32_t lvgl_port_te_period_us(void);

#ifdef __cplusplus
}
#endif
