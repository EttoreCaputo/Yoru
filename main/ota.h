#pragma once

/* Updates over the link (docs/protocol.md, OTA): the host sends the app's
 * image in chunks (`ota/begin`, `ota/write`, `ota/end`), the board writes it
 * to the app slot it isn't running from and boots it. Only a Yoru image whose
 * SHA-256 matches is taken. Over WiFi it's sealed with the pairing key, so
 * only a paired host can update the board.
 *
 * A new firmware stays on probation until it joins the WiFi or a host says
 * hello: then it can be updated again. If neither happens within a few
 * minutes the board restarts and the bootloader goes back to the one before;
 * so it does if the board restarts before (a crash, or the USB port opened). */

#include <stdbool.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/** At boot: a firmware on probation starts its clock. */
void ota_init(void);

/** Updates are possible: the partition table has two app slots. */
bool ota_available(void);

/** On the WiFi, or a host said hello: a firmware on probation is kept.
 * From any task. */
void ota_confirm(void);

/** `ota/...` requests (rpc.c). Returns the result, or NULL with `*code` and
 * `*message` set. */
cJSON *ota_rpc(const char *method, const cJSON *params, int *code, const char **message);

#ifdef __cplusplus
}
#endif
