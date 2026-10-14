#pragma once

/* Waveshare ESP32-S3-Touch-AMOLED-1.75C. Pins from Waveshare's BSP
 * (waveshare/esp32_s3_touch_amoled_1_75c), its schematic and xiaozhi-esp32;
 * they all agree. */

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CO5300 AMOLED over QSPI. */
#define BOARD_LCD_H_RES 466
#define BOARD_LCD_V_RES 466
#define BOARD_LCD_SPI_HOST SPI2_HOST
#define BOARD_LCD_CS_GPIO 12
#define BOARD_LCD_CLK_GPIO 38
#define BOARD_LCD_D0_GPIO 4
#define BOARD_LCD_D1_GPIO 5
#define BOARD_LCD_D2_GPIO 6
#define BOARD_LCD_D3_GPIO 7
#define BOARD_LCD_RST_GPIO 1
/* Wired but unused, like in Waveshare's examples. */
#define BOARD_LCD_TE_GPIO 13

/* One I2C bus for everything: codecs, touch, PMU (AXP2101 0x34), IMU
 * (QMI8658 0x6B). */
#define BOARD_I2C_PORT I2C_NUM_0
#define BOARD_I2C_SDA_GPIO 15
#define BOARD_I2C_SCL_GPIO 14

/* CST9217 at 0x5A. */
#define BOARD_TOUCH_INT_GPIO 11
#define BOARD_TOUCH_RST_GPIO 2

#define BOARD_BOOT_GPIO 0
#define BOARD_IMU_INT1_GPIO 21

/** The shared I2C bus, created on first use. */
esp_err_t board_i2c_bus(i2c_master_bus_handle_t *out);

#ifdef __cplusplus
}
#endif
