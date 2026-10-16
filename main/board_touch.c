#include "board_touch.h"

#include "board.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "board_touch";

/* The touch report: status, x, y of the first point at 0xD000, the point
 * count at byte 5 and an 0xAB ack at byte 6. */
#define CST9217_REPORT_REG 0xD000
#define CST9217_REPORT_LEN 10
#define CST9217_ACK 0xAB
#define CST9217_STATUS_DOWN 0x06

/* If an INT edge is ever lost, look anyway this often. */
#define TOUCH_FALLBACK_US (1000 * 1000)

static esp_lcd_touch_handle_t s_tp;
static i2c_master_dev_handle_t s_dev;
static volatile bool s_irq;
static bool s_down;
static int64_t s_last_read_us;

/* The controller pulls INT low for every report it has, so with no edge and
 * no finger down there is nothing to read. */
static void IRAM_ATTR touch_isr(esp_lcd_touch_handle_t tp)
{
    s_irq = true;
}

esp_err_t board_touch_init(void)
{
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(board_i2c_bus(&bus), TAG, "i2c");

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    io_cfg.scl_speed_hz = 400 * 1000;
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "panel io");

    // Waveshare's driver resets the chip and sets up INT; reads are ours.
    const esp_lcd_touch_config_t cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = BOARD_TOUCH_RST_GPIO,
        .int_gpio_num = BOARD_TOUCH_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .interrupt_callback = touch_isr,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst9217(io, &cfg, &s_tp), TAG, "cst9217");

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ESP_LCD_TOUCH_IO_I2C_CST9217_ADDRESS,
        .scl_speed_hz = 400 * 1000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG, "touch dev");
    ESP_LOGI(TAG, "CST9217 ready");
    return ESP_OK;
}

/* One write-then-read transaction. Waveshare's driver waits 2 ms between
 * the two halves, which held the LVGL task for 2.3 ms on every poll; the
 * chip answers fine without it. */
static bool read_report(uint16_t *x, uint16_t *y)
{
    const uint8_t reg[2] = {CST9217_REPORT_REG >> 8, CST9217_REPORT_REG & 0xFF};
    uint8_t d[CST9217_REPORT_LEN];
    if (i2c_master_transmit_receive(s_dev, reg, sizeof(reg), d, sizeof(d), 20) != ESP_OK) {
        return false;
    }
    if (d[6] != CST9217_ACK || (d[5] & 0x7F) == 0 || (d[0] & 0x0F) != CST9217_STATUS_DOWN) {
        return false;
    }
    uint16_t raw_x = (d[1] << 4) | (d[3] >> 4);
    uint16_t raw_y = (d[2] << 4) | (d[3] & 0x0F);
    if (raw_x > BOARD_LCD_H_RES || raw_y > BOARD_LCD_V_RES) {
        return false;
    }
    // Mirrored on both axes, as in Waveshare's BSP and xiaozhi.
    *x = BOARD_LCD_H_RES - raw_x;
    *y = BOARD_LCD_V_RES - raw_y;
    return true;
}

bool board_touch_read(uint16_t *x, uint16_t *y)
{
    if (s_dev == NULL) {
        return false;
    }
    int64_t now = esp_timer_get_time();
    // While a finger is down keep reading, to see it move and lift.
    if (!s_irq && !s_down && now - s_last_read_us < TOUCH_FALLBACK_US) {
        return false;
    }
    s_irq = false;
    s_last_read_us = now;
    s_down = read_report(x, y);
    return s_down;
}
