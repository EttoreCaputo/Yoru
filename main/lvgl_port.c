#include "lvgl_port.h"

#include <sys/lock.h>

#include "board.h"
#include "board_touch.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Two buffers of 20 lines in internal DMA RAM: 18 KB each. They must fit
 * the panel's largest transfer (board_display.c). The eyes cover a small
 * part of the screen, so 40 lines cost 37 KB more internal RAM for little
 * speed (0.3.1). */
#define LVGL_DRAW_BUF_LINES 20
#define LVGL_TICK_PERIOD_MS 2
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define LVGL_TASK_PRIORITY 5

static const char *TAG = "lvgl_port";
static _lock_t s_lvgl_lock;

static int64_t s_window_start_us;
static int64_t s_window_busy_us;
static uint32_t s_window_max_us;
static lvgl_port_stats_t s_stats;
static portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;

static void account(int64_t start_us, int64_t end_us)
{
    uint32_t took = (uint32_t) (end_us - start_us);
    s_window_busy_us += took;
    if (took > s_window_max_us) {
        s_window_max_us = took;
    }
    int64_t span = end_us - s_window_start_us;
    if (span >= LVGL_PORT_STATS_WINDOW_MS * 1000) {
        portENTER_CRITICAL(&s_stats_mux);
        s_stats.busy_pct = 100.0f * (float) s_window_busy_us / (float) span;
        s_stats.max_us = s_window_max_us;
        portEXIT_CRITICAL(&s_stats_mux);
        s_window_start_us = end_us;
        s_window_busy_us = 0;
        s_window_max_us = 0;
    }
}

void lvgl_port_get_stats(lvgl_port_stats_t *out)
{
    portENTER_CRITICAL(&s_stats_mux);
    *out = s_stats;
    portEXIT_CRITICAL(&s_stats_mux);
}

static bool notify_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                               esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_flush_ready((lv_display_t *) user_ctx);
    return false;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t) lv_display_get_user_data(disp);
    const int w = lv_area_get_width(area);
    const int h = lv_area_get_height(area);
    // The panel wants big-endian RGB565 on the wire.
    lv_draw_sw_rgb565_swap(px_map, w * h);
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
}

/* The CO5300 only takes areas that start on an even pixel and are an even
 * number of pixels wide and tall. */
static void round_area_cb(lv_event_t *e)
{
    lv_area_t *area = lv_event_get_invalidated_area(e);
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x;
    uint16_t y;
    if (board_touch_read(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_task(void *arg)
{
    const uint32_t min_delay_ms = 1000 / configTICK_RATE_HZ;
    s_window_start_us = esp_timer_get_time();
    while (true) {
        _lock_acquire(&s_lvgl_lock);
        int64_t start = esp_timer_get_time();
        uint32_t delay_ms = lv_timer_handler();
        account(start, esp_timer_get_time());
        _lock_release(&s_lvgl_lock);
        if (delay_ms < min_delay_ms) {
            delay_ms = min_delay_ms;
        } else if (delay_ms > 500) {
            delay_ms = 500;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void lvgl_port_lock(void)
{
    _lock_acquire(&s_lvgl_lock);
}

void lvgl_port_unlock(void)
{
    _lock_release(&s_lvgl_lock);
}

esp_err_t lvgl_port_init(const board_lcd_t *lcd, lv_display_t **out_display)
{
    lv_init();

    lv_display_t *disp = lv_display_create(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    const size_t buf_size = BOARD_LCD_H_RES * LVGL_DRAW_BUF_LINES * sizeof(lv_color16_t);
    void *buf1 = spi_bus_dma_memory_alloc(BOARD_LCD_SPI_HOST, buf_size, 0);
    void *buf2 = spi_bus_dma_memory_alloc(BOARD_LCD_SPI_HOST, buf_size, 0);
    ESP_RETURN_ON_FALSE(buf1 && buf2, ESP_ERR_NO_MEM, TAG, "draw buffers");

    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(disp, lcd->panel);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_add_event_cb(disp, round_area_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_flush_ready,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(lcd->io, &cbs, disp), TAG, "io callbacks");

    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read_cb);
    lv_indev_set_display(touch, disp);

    const esp_timer_create_args_t tick_args = {
        .callback = &tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &tick_timer), TAG, "tick timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000), TAG, "tick start");

    // Core 0: core 1 is kept for the audio front end and the wake word (M3).
    BaseType_t ok = xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_TASK_STACK_SIZE, NULL,
                                            LVGL_TASK_PRIORITY, NULL, 0);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create");

    *out_display = disp;
    return ESP_OK;
}
