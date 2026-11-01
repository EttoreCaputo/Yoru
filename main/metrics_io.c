#include "metrics_io.h"

#include "clock.h"
#include "esp_log.h"
#include "metrics_model.h"
#include "metrics_parser.h"

static const char *TAG = "metrics_io";

void metrics_io_handle(uint8_t *payload, size_t len)
{
    metrics_snapshot_t snap;
    if (metrics_parse_line((const char *) payload, &snap) != ESP_OK) {
        ESP_LOGW(TAG, "ignored snapshot");
        return;
    }
    metrics_model_set(&snap);
    clock_from_host(snap.ts, snap.host.valid ? snap.host.minute : -1, snap.host.valid ? snap.host.tz : NULL);
    ESP_LOGD(TAG, "host %s, plan %s", snap.host.valid ? "yes" : "no", snap.claude.valid ? "yes" : "no");
}
