#include "board_display.h"

#include "board.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_log.h"

static const char *TAG = "board_display";

/* The largest transfer is one LVGL draw buffer (lvgl_port.c). */
#define LCD_MAX_TRANSFER (BOARD_LCD_H_RES * 48 * 2)

/* QSPI command words: 0x02 = write, the DCS command in bits 8..15. */
#define LCD_QSPI_CMD(cmd) ((0x02u << 24) | ((uint32_t) (cmd) << 8))

/* Waveshare's sequence for this panel (BSP and xiaozhi agree). Brightness
 * (0x51) starts at 0 here, unlike theirs: the screen stays black until
 * the first frame is drawn. */
static const co5300_lcd_init_cmd_t s_init_cmds[] = {
    {0xFE, (uint8_t[]) {0x20}, 1, 0},
    {0x19, (uint8_t[]) {0x10}, 1, 0},
    {0x1C, (uint8_t[]) {0xA0}, 1, 0},
    {0xFE, (uint8_t[]) {0x00}, 1, 0},
    {0xC4, (uint8_t[]) {0x80}, 1, 0},
    {0x3A, (uint8_t[]) {0x55}, 1, 0},       // RGB565
    {0x35, (uint8_t[]) {0x00}, 1, 0},       // TE on V-blank
    {0x53, (uint8_t[]) {0x20}, 1, 0},
    {0x51, (uint8_t[]) {0x00}, 1, 0},       // brightness
    {0x63, (uint8_t[]) {0xFF}, 1, 0},
    {0x2A, (uint8_t[]) {0x00, 0x06, 0x01, 0xD7}, 4, 0}, // columns 6..471
    {0x2B, (uint8_t[]) {0x00, 0x00, 0x01, 0xD1}, 4, 600}, // rows 0..465
    {0x11, NULL, 0, 600},                   // sleep out
    {0x29, NULL, 0, 0},                     // display on
};

static esp_lcd_panel_io_handle_t s_io;

esp_err_t board_display_init(board_lcd_t *out)
{
    const spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(
        BOARD_LCD_CLK_GPIO, BOARD_LCD_D0_GPIO, BOARD_LCD_D1_GPIO, BOARD_LCD_D2_GPIO, BOARD_LCD_D3_GPIO,
        LCD_MAX_TRANSFER);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    // The flush-done callback is registered by lvgl_port.c.
    const esp_lcd_panel_io_spi_config_t io_cfg = CO5300_PANEL_IO_QSPI_CONFIG(BOARD_LCD_CS_GPIO, NULL, NULL);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) BOARD_LCD_SPI_HOST, &io_cfg, &s_io),
                        TAG, "panel io");

    const co5300_vendor_config_t vendor = {
        .init_cmds = s_init_cmds,
        .init_cmds_size = sizeof(s_init_cmds) / sizeof(s_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *) &vendor,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(s_io, &panel_cfg, &panel), TAG, "co5300");
    // The panel's RAM starts 6 columns in.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(panel, 6, 0), TAG, "gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "on");

    out->panel = panel;
    out->io = s_io;
    ESP_LOGI(TAG, "CO5300 %dx%d ready", BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return ESP_OK;
}

esp_err_t board_display_set_brightness(int percent)
{
    if (percent < 0 || percent > 100 || s_io == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t level = (uint8_t) (percent * 255 / 100);
    return esp_lcd_panel_io_tx_param(s_io, (int) LCD_QSPI_CMD(0x51), &level, 1);
}
