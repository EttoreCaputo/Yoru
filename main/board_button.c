#include "board_button.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "button";

#define BOOT_GPIO GPIO_NUM_0
/* Read this often; two reads in a row agree before a change counts. */
#define POLL_MS 20
#define LONG_MS 1000

static struct {
    board_button_cb_t cb;
    bool down;     // debounced
    bool raw;      // last read
    bool fired;    // the long press already went off
    uint32_t down_ms;
} s;

static void poll_cb(lv_timer_t *t)
{
    // Pulled up on the board: low while pressed.
    const bool raw = gpio_get_level(BOOT_GPIO) == 0;
    const bool stable = raw == s.raw;
    s.raw = raw;
    if (!stable) {
        return;
    }
    if (raw && !s.down) {
        s.down = true;
        s.fired = false;
        s.down_ms = lv_tick_get();
    } else if (raw && s.down && !s.fired && lv_tick_elaps(s.down_ms) >= LONG_MS) {
        s.fired = true;
        ESP_LOGI(TAG, "BOOT held");
        s.cb(true);
    } else if (!raw && s.down) {
        s.down = false;
        if (!s.fired) {
            ESP_LOGI(TAG, "BOOT pressed");
            s.cb(false);
        }
    }
}

void board_button_init(board_button_cb_t cb)
{
    s.cb = cb;
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    s.raw = gpio_get_level(BOOT_GPIO) == 0;
    s.down = s.raw; // held at start: wait for the release
    s.fired = true;
    lv_timer_create(poll_cb, POLL_MS, NULL);
}
