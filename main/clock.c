#include "clock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "nvs.h"

static const char *TAG = "clock";

#define NVS_NS "clock"
#define NVS_TZ "tz"
/* Until a host says otherwise: the owner's, Italy's. */
#define DEFAULT_TZ "CET-1CEST,M3.5.0,M10.5.0/3"
#define TZ_MAX 48
/* Before this the RTC only counts from boot (2025-01-01). */
#define VALID_AFTER 1735689600
/* A host's clock this far from ours sets it; closer, SNTP's is as good. */
#define HOST_SKEW_S 2

static char s_tz[TZ_MAX] = DEFAULT_TZ;

static void apply_tz(const char *tz)
{
    snprintf(s_tz, sizeof(s_tz), "%s", tz);
    setenv("TZ", s_tz, 1);
    tzset();
}

static void save_tz(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_str(h, NVS_TZ, s_tz);
    nvs_commit(h);
    nvs_close(h);
}

void clock_init(void)
{
    char tz[TZ_MAX] = DEFAULT_TZ;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(tz);
        if (nvs_get_str(h, NVS_TZ, tz, &len) != ESP_OK) {
            snprintf(tz, sizeof(tz), "%s", DEFAULT_TZ);
        }
        nvs_close(h);
    }
    apply_tz(tz);
    ESP_LOGI(TAG, "time zone %s", s_tz);
}

static void on_sync(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP: %lld", (long long) tv->tv_sec);
}

void clock_start_sntp(void)
{
    static bool started;
    if (started) {
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = on_sync;
    // It waits for an address by itself, and syncs again every hour.
    if (esp_netif_sntp_init(&cfg) == ESP_OK) {
        started = true;
    } else {
        ESP_LOGW(TAG, "SNTP not started");
    }
}

/* A fixed offset as POSIX writes it, east of UTC negative: "<+0130>-1:30". */
static void fixed_tz(int offset_min, char *out, size_t len)
{
    const int a = abs(offset_min);
    snprintf(out, len, "<%c%02d%02d>%c%d:%02d", offset_min < 0 ? '-' : '+', a / 60, a % 60, offset_min < 0 ? '+' : '-',
             a / 60, a % 60);
}

void clock_from_host(uint32_t ts, int minute, const char *tz)
{
    if (ts > VALID_AFTER) {
        struct timeval now;
        gettimeofday(&now, NULL);
        if (llabs((long long) now.tv_sec - (long long) ts) > HOST_SKEW_S) {
            const struct timeval tv = {.tv_sec = (time_t) ts};
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "set from the host: %lu", (unsigned long) ts);
        }
    }
    char want[TZ_MAX] = "";
    if (tz != NULL && tz[0] != '\0' && strlen(tz) < TZ_MAX) {
        snprintf(want, sizeof(want), "%s", tz);
    } else if (minute >= 0 && ts > VALID_AFTER && clock_minute() != minute) {
        // An older host sends no zone: its minute against UTC gives the
        // offset (to a quarter of an hour), until the next change of time.
        int off = minute - (int) (ts / 60 % 1440);
        off = off > 840 ? off - 1440 : off < -720 ? off + 1440 : off;
        fixed_tz((off + (off < 0 ? -7 : 7)) / 15 * 15, want, sizeof(want));
    }
    if (want[0] != '\0' && strcmp(want, s_tz) != 0) {
        apply_tz(want);
        save_tz();
        ESP_LOGI(TAG, "time zone %s", s_tz);
    }
}

bool clock_valid(void)
{
    return time(NULL) > VALID_AFTER;
}

static bool local_now(struct tm *out)
{
    const time_t now = time(NULL);
    return now > VALID_AFTER && localtime_r(&now, out) != NULL;
}

int clock_minute(void)
{
    struct tm tm;
    return local_now(&tm) ? tm.tm_hour * 60 + tm.tm_min : -1;
}

int32_t clock_day(void)
{
    struct tm tm;
    if (!local_now(&tm)) {
        return -1;
    }
    const int y = tm.tm_year + 1900, m = tm.tm_mon + 1;
    // The local date's days since the epoch (Howard Hinnant's days from
    // civil), whatever the time zone.
    const int yy = y - (m <= 2);
    const int era = (yy >= 0 ? yy : yy - 399) / 400;
    const int yoe = yy - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + tm.tm_mday - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
