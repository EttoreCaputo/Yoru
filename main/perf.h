#pragma once

#include "cJSON.h"

/* How hard the board works, to find what makes the audio stutter: CPU per
 * task and per core, LVGL's share, and the audio's gaps, measured every
 * LVGL_PORT_STATS_WINDOW_MS (docs/protocol.md, debug/perf). */

void perf_start(void);

/** The last window's load and, since boot or the last reset, the worst
 *  window and the audio's gaps. */
cJSON *perf_report(void);

/** Starts the worst-of and the gaps' count afresh. */
void perf_reset(void);
