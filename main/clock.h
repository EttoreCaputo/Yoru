#pragma once

/* The board's own clock, so it knows the time of day without the host: from
 * the internet (SNTP) once the WiFi is up, and from the host's snapshots
 * (`ts`, `host.min`, `host.tz`) whenever they come. The time zone comes from
 * the host and is kept in NVS; until a host has said it, Central European
 * Time. The RTC keeps counting through light sleep. */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Load the time zone (NVS must be up). */
void clock_init(void);
/** Start SNTP; net.c calls it once the WiFi's netif exists. */
void clock_start_sntp(void);
/** A host's snapshot: its UTC seconds, its local minute of the day (-1 if
 * not sent) and its POSIX time zone (NULL or "" if not sent). */
void clock_from_host(uint32_t ts, int minute, const char *tz);

/** The time is known (SNTP or a host set it since boot). */
bool clock_valid(void);
/** Minutes since local midnight, 0..1439, or -1 when the time isn't known. */
int clock_minute(void);
/** Local days since 1970-01-01, or -1 when the time isn't known. */
int32_t clock_day(void);

#ifdef __cplusplus
}
#endif
