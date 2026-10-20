#pragma once

/* The AXP2101 power chip at 0x34 on the shared I2C bus: the battery's level
 * and voltage, whether it is charging or on USB, the PWR key (taps and
 * holds), and powering the board off. Only the fuel gauge, the ADC, the
 * key's interrupt flags and its long-press power-off are touched: the rails
 * stay as the chip's defaults set them, which is what the board was designed
 * around. */

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* A battery is connected; its charge, 0..100 (-1 until the gauge
     * knows), and its voltage. */
    bool battery;
    int percent;
    int mv;
    /* USB power is there, and the battery is taking it. */
    bool usb;
    bool charging;
} board_pmu_state_t;

typedef enum {
    BOARD_PMU_KEY_SHORT,   // pressed and let go within HOLD_MS
    BOARD_PMU_KEY_HOLD,    // still down after HOLD_MS (push-to-talk starts)
    BOARD_PMU_KEY_RELEASE, // let go after a HOLD
} board_pmu_key_t;

/** The PWR key, from the PMU's poll task. A long press no longer powers off
 * in hardware: holding PWR is push-to-talk. */
typedef void (*board_pmu_key_cb_t)(board_pmu_key_t ev);

/** Find the chip and switch on its gauge and ADC. Polls it from a small
 * task afterwards; ESP_ERR_NOT_FOUND without one. */
esp_err_t board_pmu_init(board_pmu_key_cb_t on_key);

/** The last reading, from any task. All zero without the chip. */
void board_pmu_get(board_pmu_state_t *out);
bool board_pmu_present(void);

/** Switch the board off. USB power keeps it on. */
void board_pmu_power_off(void);

#ifdef __cplusplus
}
#endif
