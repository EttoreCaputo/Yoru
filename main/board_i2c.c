#include "board.h"

#include "esp_check.h"

static const char *TAG = "board_i2c";
static i2c_master_bus_handle_t s_bus;

esp_err_t board_i2c_bus(i2c_master_bus_handle_t *out)
{
    // Called from app_main only, before any task uses the bus.
    if (s_bus == NULL) {
        // The board has 2.2k pull-ups on SDA and SCL.
        const i2c_master_bus_config_t cfg = {
            .i2c_port = BOARD_I2C_PORT,
            .sda_io_num = BOARD_I2C_SDA_GPIO,
            .scl_io_num = BOARD_I2C_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_bus), TAG, "bus");
    }
    *out = s_bus;
    return ESP_OK;
}
