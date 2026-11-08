#include "perf.h"

#include <string.h>

#include "board_audio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "eyes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "life.h"
#include "lvgl_port.h"
#include "playback.h"
#include "voice.h"

static const char *TAG = "perf";

#define MAX_TASKS 48
#define CORES 2

typedef struct {
    char name[configMAX_TASK_NAME_LEN];
    int8_t core; /* -1: either */
    float pct;   /* of one core, last window */
    float peak;  /* worst window since the reset */
} task_load_t;

typedef struct {
    uint32_t tx_gaps, rx_lost, underruns, lost, dropped;
} gaps_t;

/* Written by the perf task, read by the RPC (the link task), under s_lock. */
static SemaphoreHandle_t s_lock;
static task_load_t s_tasks[MAX_TASKS];
static int s_n;
static float s_idle[CORES];
static float s_idle_min[CORES];
static lvgl_port_stats_t s_lvgl;
static float s_lvgl_busy_peak;
static uint32_t s_lvgl_max_peak;
static uint32_t s_windows;
static gaps_t s_base;
/* The panel's counters at the reset. */
static uint32_t s_sent_frames0;
static uint64_t s_sent_us0, s_sent_px0;
static int64_t s_reset_us;

/* Two snapshots of the run-time counters, swapped every window. PSRAM: big,
 * and only this task touches them. */
static TaskStatus_t *s_prev, *s_now;
static UBaseType_t s_prev_n;
static configRUN_TIME_COUNTER_TYPE s_prev_t;

static gaps_t gaps_now(void)
{
    gaps_t g;
    board_audio_gaps(&g.tx_gaps, &g.rx_lost);
    playback_totals(&g.underruns, &g.lost);
    g.dropped = voice_dropped_total();
    return g;
}

static task_load_t *slot(const char *name)
{
    for (int i = 0; i < s_n; i++) {
        if (strcmp(s_tasks[i].name, name) == 0) {
            return &s_tasks[i];
        }
    }
    if (s_n == MAX_TASKS) {
        return NULL;
    }
    task_load_t *t = &s_tasks[s_n++];
    memset(t, 0, sizeof(*t));
    strlcpy(t->name, name, sizeof(t->name));
    return t;
}

/* Each task's share of one core since the last snapshot. */
static void sample(void)
{
    configRUN_TIME_COUNTER_TYPE t = 0;
    UBaseType_t n = uxTaskGetSystemState(s_now, MAX_TASKS, &t);
    const configRUN_TIME_COUNTER_TYPE span = t - s_prev_t;
    if (s_prev_n > 0 && span > 0) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int i = 0; i < s_n; i++) {
            s_tasks[i].pct = 0;
        }
        for (UBaseType_t i = 0; i < n; i++) {
            for (UBaseType_t j = 0; j < s_prev_n; j++) {
                if (s_prev[j].xHandle != s_now[i].xHandle) {
                    continue;
                }
                task_load_t *tl = slot(s_now[i].pcTaskName);
                if (tl == NULL) {
                    break;
                }
                const BaseType_t core = xTaskGetCoreID(s_now[i].xHandle);
                tl->core = core == tskNO_AFFINITY ? -1 : (int8_t) core;
                tl->pct = 100.0f * (float) (s_now[i].ulRunTimeCounter - s_prev[j].ulRunTimeCounter) / (float) span;
                if (tl->pct > tl->peak) {
                    tl->peak = tl->pct;
                }
                for (int c = 0; c < CORES; c++) {
                    if (s_now[i].xHandle == xTaskGetIdleTaskHandleForCore(c)) {
                        s_idle[c] = tl->pct;
                        if (s_windows == 0 || tl->pct < s_idle_min[c]) {
                            s_idle_min[c] = tl->pct;
                        }
                    }
                }
                break;
            }
        }
        lvgl_port_get_stats(&s_lvgl);
        if (s_lvgl.busy_pct > s_lvgl_busy_peak) {
            s_lvgl_busy_peak = s_lvgl.busy_pct;
        }
        if (s_lvgl.max_us > s_lvgl_max_peak) {
            s_lvgl_max_peak = s_lvgl.max_us;
        }
        s_windows++;
        xSemaphoreGive(s_lock);
    }
    TaskStatus_t *swap = s_prev;
    s_prev = s_now;
    s_now = swap;
    s_prev_n = n;
    s_prev_t = t;
}

static void perf_task(void *arg)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(LVGL_PORT_STATS_WINDOW_MS));
        sample();
        lvgl_port_lock();
        const char *eyes = eyes_mood_name(eyes_get_mood());
        lvgl_port_unlock();
        const gaps_t g = gaps_now();
        // PSRAM runs short with ESP-SR, the sprites and the cloud's buffers.
        ESP_LOGI(TAG,
                 "lvgl %.1f%% busy, longest %lu us, idle %.0f%%/%.0f%%, eyes %s, %s; audio gaps tx %lu rx %lu; "
                 "PSRAM %u KB free, %u KB largest",
                 s_lvgl.busy_pct, (unsigned long) s_lvgl.max_us, s_idle[0], s_idle[1], eyes, life_activity(),
                 (unsigned long) (g.tx_gaps - s_base.tx_gaps), (unsigned long) (g.rx_lost - s_base.rx_lost),
                 (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                 (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
    }
}

void perf_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_prev = heap_caps_calloc(MAX_TASKS, sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM);
    s_now = heap_caps_calloc(MAX_TASKS, sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM);
    // Its stack can live in PSRAM: it never writes the flash (0.3.1).
    if (s_lock == NULL || s_prev == NULL || s_now == NULL
        || xTaskCreatePinnedToCoreWithCaps(perf_task, "perf", 3072, NULL, 2, NULL, 0,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
               != pdPASS) {
        ESP_LOGE(TAG, "failed to start");
    }
}

void perf_reset(void)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < s_n; i++) {
        s_tasks[i].peak = 0;
    }
    s_lvgl_busy_peak = 0;
    s_lvgl_max_peak = 0;
    s_windows = 0;
    s_base = gaps_now();
    xSemaphoreGive(s_lock);
    lvgl_port_get_sent(&s_sent_frames0, &s_sent_us0, &s_sent_px0);
    s_reset_us = esp_timer_get_time();
    lvgl_port_lock();
    cJSON_Delete(eyes_profile(true));
    lvgl_port_unlock();
}

cJSON *perf_report(void)
{
    cJSON *r = cJSON_CreateObject();
    if (s_lock == NULL) {
        return r;
    }
    const gaps_t g = gaps_now();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON_AddNumberToObject(r, "window_ms", LVGL_PORT_STATS_WINDOW_MS);
    cJSON_AddNumberToObject(r, "windows", s_windows);
    cJSON *idle = cJSON_AddArrayToObject(r, "idle_pct");
    cJSON *idle_min = cJSON_AddArrayToObject(r, "idle_min_pct");
    for (int c = 0; c < CORES; c++) {
        cJSON_AddItemToArray(idle, cJSON_CreateNumber(s_idle[c]));
        cJSON_AddItemToArray(idle_min, cJSON_CreateNumber(s_idle_min[c]));
    }
    cJSON *lvgl = cJSON_AddObjectToObject(r, "lvgl");
    cJSON_AddNumberToObject(lvgl, "busy_pct", s_lvgl.busy_pct);
    cJSON_AddNumberToObject(lvgl, "longest_us", s_lvgl.max_us);
    cJSON_AddNumberToObject(lvgl, "busy_peak_pct", s_lvgl_busy_peak);
    cJSON_AddNumberToObject(lvgl, "longest_peak_us", s_lvgl_max_peak);
    uint32_t frames;
    uint64_t sent_us, sent_px;
    lvgl_port_get_sent(&frames, &sent_us, &sent_px);
    const double secs = (double) (esp_timer_get_time() - s_reset_us) / 1e6;
    if (s_reset_us > 0 && secs > 0) {
        cJSON_AddNumberToObject(lvgl, "panel_fps", (frames - s_sent_frames0) / secs);
        cJSON_AddNumberToObject(lvgl, "send_ms_per_s", (double) (sent_us - s_sent_us0) / 1000.0 / secs);
        cJSON_AddNumberToObject(lvgl, "kpx_sent_per_s", (double) (sent_px - s_sent_px0) / 1000.0 / secs);
    }
    cJSON *audio = cJSON_AddObjectToObject(r, "audio");
    cJSON_AddNumberToObject(audio, "speaker_gaps", g.tx_gaps - s_base.tx_gaps);
    cJSON_AddNumberToObject(audio, "mic_lost", g.rx_lost - s_base.rx_lost);
    cJSON_AddNumberToObject(audio, "speech_underruns", g.underruns - s_base.underruns);
    cJSON_AddNumberToObject(audio, "speech_frames_lost", g.lost - s_base.lost);
    cJSON_AddNumberToObject(audio, "voice_frames_dropped", g.dropped - s_base.dropped);
    cJSON *tasks = cJSON_AddArrayToObject(r, "tasks");
    for (int i = 0; i < s_n; i++) {
        const task_load_t *t = &s_tasks[i];
        if (t->peak < 0.5f) {
            continue;
        }
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", t->name);
        cJSON_AddNumberToObject(item, "core", t->core);
        cJSON_AddNumberToObject(item, "pct", t->pct);
        cJSON_AddNumberToObject(item, "peak_pct", t->peak);
        cJSON_AddItemToArray(tasks, item);
    }
    xSemaphoreGive(s_lock);
    lvgl_port_lock();
    cJSON_AddItemToObject(r, "draw", eyes_profile(false));
    lvgl_port_unlock();
    cJSON_AddNumberToObject(r, "internal_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(r, "internal_min", heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(r, "psram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return r;
}
