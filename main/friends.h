#pragma once

/* Other Yorus nearby (docs/FRIENDS.md): every few seconds each board says
 * hello with ESP-NOW on its WiFi's channel, and keeps who it heard. Over the
 * radio the WiFi already has on, so it costs no RAM worth counting and needs
 * neither the host nor Bluetooth. A board met for the first time, or back
 * after a long while, is a friend to greet (behaviour.c). */

#include <stdbool.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The WiFi's radio came up with an address, or stopped (net.c, from the
 * event loop): ESP-NOW starts and stops with it. */
void friends_radio(bool on);

/** Every tick (behaviour.c, LVGL lock held): `awake` goes into our hello.
 * Returns true once for a friend just met, or back after a long while,
 * that Yoru should greet now; only while `free` (nothing else has the
 * eyes): until then the friend waits. */
bool friends_tick(bool awake, bool free);

/** How many friends are in range now. */
int friends_count(void);

/** get_state's `friends`: `[{id, rssi, seen_s, awake}]`. */
cJSON *friends_status(void);

#ifdef __cplusplus
}
#endif
