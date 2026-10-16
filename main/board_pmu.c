#include "board_pmu.h"

#include <string.h>

#include "board.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

static const char *TAG = "board_pmu";

/* Registers, as in X-Powers' datasheet and XPowersLib. */
#define AXP_ADDR 0x34
#define AXP_STATUS1 0x00   // bit 5 VBUS good, bit 3 battery present
#define AXP_STATUS2 0x01   // bits 6:5 current: 01 charging, 10 discharging
#define AXP_CHIP_ID 0x03
#define AXP_COMMON 0x10    // bit 0: power off
#define AXP_GAUGE_CTRL 0x18 // bit 3: fuel gauge on
#define AXP_ADC_CTRL 0x30  // bit 0: battery voltage
#define AXP_VBAT_H 0x34
#define AXP_INTEN2 0x41    // bit 3: PWR short press
#define AXP_INTSTS2 0x49   // the same bits; write 1 to clear
#define AXP_BAT_DET 0x68   // bit 0: battery detection on
#define AXP_PERCENT 0xA4

#define AXP2101_ID 0x4A
#define KEY_SHORT_BIT 0x08
/* The key's flag is polled rather than wired to an interrupt: a read every
 * 150 ms costs nothing and a press still feels immediate. The battery
 * changes slowly, so every few seconds is plenty. */
#define POLL_MS 150
#define BATTERY_EVERY 20

static i2c_master_dev_handle_t s_dev;
static board_pmu_key_cb_t s_on_key;
static board_pmu_state_t s_state;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t rd(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, len, 50);
}

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    const uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_dev, b, sizeof(b), 50);
}

static esp_err_t set_bits(uint8_t reg, uint8_t bits)
{
    uint8_t v;
    ESP_RETURN_ON_ERROR(rd(reg, &v, 1), TAG, "read %02x", reg);
    return (v & bits) == bits ? ESP_OK : wr(reg, v | bits);
}

static void read_battery(void)
{
    uint8_t st[2], vb[2], pct;
    if (rd(AXP_STATUS1, st, 2) != ESP_OK || rd(AXP_VBAT_H, vb, 2) != ESP_OK || rd(AXP_PERCENT, &pct, 1) != ESP_OK) {
        return;
    }
    board_pmu_state_t now = {
        .battery = (st[0] & 0x08) != 0,
        .usb = (st[0] & 0x20) != 0,
        .charging = ((st[1] >> 5) & 0x03) == 0x01,
        .mv = ((vb[0] & 0x1F) << 8) | vb[1],
        .percent = pct <= 100 ? pct : -1,
    };
    if (!now.battery) {
        now.percent = -1;
        now.charging = false;
    }
    portENTER_CRITICAL(&s_mux);
    s_state = now;
    portEXIT_CRITICAL(&s_mux);
}

static void poll_task(void *arg)
{
    int n = 0;
    while (true) {
        uint8_t irq;
        if (rd(AXP_INTSTS2, &irq, 1) == ESP_OK && (irq & KEY_SHORT_BIT)) {
            wr(AXP_INTSTS2, KEY_SHORT_BIT);
            if (s_on_key != NULL) {
                s_on_key();
            }
        }
        if (n++ % BATTERY_EVERY == 0) {
            read_battery();
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t board_pmu_init(board_pmu_key_cb_t on_key)
{
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(board_i2c_bus(&bus), TAG, "i2c");
    if (i2c_master_probe(bus, AXP_ADDR, 50) != ESP_OK) {
        ESP_LOGW(TAG, "no AXP2101 at 0x%02x", AXP_ADDR);
        return ESP_ERR_NOT_FOUND;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP_ADDR,
        .scl_speed_hz = 400 * 1000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "dev");
    uint8_t id = 0;
    rd(AXP_CHIP_ID, &id, 1);
    if (id != AXP2101_ID) {
        ESP_LOGW(TAG, "chip id 0x%02x, expected 0x%02x; trying anyway", id, AXP2101_ID);
    }
    set_bits(AXP_BAT_DET, 0x01);
    set_bits(AXP_ADC_CTRL, 0x01);
    set_bits(AXP_GAUGE_CTRL, 0x08);
    set_bits(AXP_INTEN2, KEY_SHORT_BIT);
    wr(AXP_INTSTS2, 0xFF); // presses from before the boot don't count
    s_on_key = on_key;
    read_battery();
    board_pmu_state_t st;
    board_pmu_get(&st);
    ESP_LOGI(TAG, "AXP2101: battery %s %d%% %d mV, usb %d, charging %d", st.battery ? "yes" : "no", st.percent,
             st.mv, st.usb, st.charging);
    // Its stack is small and it never writes the flash: PSRAM is fine.
    if (xTaskCreatePinnedToCoreWithCaps(poll_task, "pmu", 2560, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void board_pmu_get(board_pmu_state_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_state;
    portEXIT_CRITICAL(&s_mux);
}

bool board_pmu_present(void)
{
    return s_dev != NULL;
}

void board_pmu_power_off(void)
{
    if (s_dev == NULL) {
        return;
    }
    ESP_LOGI(TAG, "power off");
    uint8_t v = 0;
    rd(AXP_COMMON, &v, 1);
    wr(AXP_COMMON, v | 0x01);
}
