#include "board_touch.h"

#include "board.h"
#include "esp_check.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_log.h"

static const char *TAG = "board_touch";
static esp_lcd_touch_handle_t s_tp;

esp_err_t board_touch_init(void)
{
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(board_i2c_bus(&bus), TAG, "i2c");

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    io_cfg.scl_speed_hz = 400 * 1000;
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "panel io");

    // Mirrored on both axes, as in Waveshare's BSP and xiaozhi.
    const esp_lcd_touch_config_t cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = BOARD_TOUCH_RST_GPIO,
        .int_gpio_num = BOARD_TOUCH_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 1,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst9217(io, &cfg, &s_tp), TAG, "cst9217");
    ESP_LOGI(TAG, "CST9217 ready");
    return ESP_OK;
}

bool board_touch_read(uint16_t *x, uint16_t *y)
{
    if (s_tp == NULL || esp_lcd_touch_read_data(s_tp) != ESP_OK) {
        return false;
    }
    esp_lcd_touch_point_data_t point;
    uint8_t count = 0;
    if (esp_lcd_touch_get_data(s_tp, &point, &count, 1) != ESP_OK || count == 0) {
        return false;
    }
    *x = point.x;
    *y = point.y;
    return true;
}
