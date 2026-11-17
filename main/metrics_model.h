#pragma once

/* The latest snapshot of the computer, as the host sends it (M4): its clock
 * and how long since its last input (night brightness, sleep). From
 * DualEye's metrics_model.h, without the gauges. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define METRICS_STALE_MS_DEFAULT 3000

typedef enum {
    METRICS_UI_WAITING = 0,
    METRICS_UI_LIVE,
    METRICS_UI_STALE,
    METRICS_UI_ERROR,
} metrics_ui_state_t;

/* What the host can tell of the person at the computer (behaviour.c): the
 * local time and how long since they last touched the mouse or the
 * keyboard. */
typedef struct {
    bool valid;
    /* Minutes since local midnight, 0..1439. */
    int minute;
    /* Seconds without input. */
    int idle_s;
    /* Its POSIX time zone ("CET-1CEST,M3.5.0,M10.5.0/3"); empty if not sent. */
    char tz[48];
} metrics_host_t;

typedef struct {
    uint32_t ts;
    uint32_t updated_ms;
    metrics_host_t host;
    metrics_ui_state_t state;
} metrics_snapshot_t;

void metrics_model_init(void);
void metrics_model_get(metrics_snapshot_t *out);
void metrics_model_set(const metrics_snapshot_t *in);
/** A host is sending snapshots now: it is there. */
bool metrics_model_live(void);

#ifdef __cplusplus
}
#endif
