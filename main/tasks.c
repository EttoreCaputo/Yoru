#include "tasks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "card.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "metrics_model.h"
#include "nvs.h"
#include "rpc.h"

static const char *TAG = "tasks";

#define NVS_NS "tasks"
#define NVS_KEY "list"
/* Bumped when `saved_t` changes: an older one is dropped. */
#define SAVED_VERSION 1

/* What NVS keeps, so the list shows after a restart without the host. */
typedef struct {
    uint32_t version;
    int32_t n;
    task_t list[TASKS_MAX];
} saved_t;

EXT_RAM_BSS_ATTR static saved_t s_saved;
EXT_RAM_BSS_ATTR static task_t s_new[TASKS_MAX];

static uint32_t s_version;

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    s_saved.version = SAVED_VERSION;
    if (nvs_set_blob(h, NVS_KEY, &s_saved, sizeof(s_saved)) != ESP_OK || nvs_commit(h) != ESP_OK) {
        ESP_LOGW(TAG, "not saved");
    }
    nvs_close(h);
}

void tasks_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_saved);
    if (nvs_get_blob(h, NVS_KEY, &s_saved, &len) != ESP_OK || len != sizeof(s_saved)
        || s_saved.version != SAVED_VERSION || s_saved.n < 0 || s_saved.n > TASKS_MAX) {
        memset(&s_saved, 0, sizeof(s_saved));
    }
    nvs_close(h);
    // A run the board saw start can't still be going after a restart.
    for (int i = 0; i < s_saved.n; i++) {
        s_saved.list[i].running = false;
    }
    ESP_LOGI(TAG, "%d tasks kept", (int) s_saved.n);
}

static const char *get_str(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static double get_num(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}

static bool get_bool(const cJSON *o, const char *key, bool dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsBool(v) ? cJSON_IsTrue(v) : dflt;
}

static bool parse_one(const cJSON *o, task_t *out, char *err, size_t len)
{
    memset(out, 0, sizeof(*out));
    const char *id = get_str(o, "id"), *name = get_str(o, "name");
    if (id[0] == '\0' || strlen(id) > TASKS_ID_MAX) {
        snprintf(err, len, "each task needs an id of at most %d bytes", TASKS_ID_MAX);
        return false;
    }
    if (name[0] == '\0') {
        snprintf(err, len, "each task needs a name");
        return false;
    }
    snprintf(out->id, sizeof(out->id), "%s", id);
    card_ascii(name, out->name, TASKS_NAME_CHARS);
    card_ascii(get_str(o, "what"), out->what, TASKS_TEXT_CHARS);
    card_ascii(get_str(o, "result"), out->result, TASKS_TEXT_CHARS);
    const char *repeat = get_str(o, "repeat");
    out->repeat = strcmp(repeat, "once") == 0      ? TASK_ONCE
                  : strcmp(repeat, "monthly") == 0 ? TASK_MONTHLY
                  : strcmp(repeat, "hourly") == 0  ? TASK_HOURLY
                                                   : TASK_DAILY;
    const char *acting = get_str(o, "acting");
    out->acting = strcmp(acting, "never") == 0 ? TASK_READS : strcmp(acting, "free") == 0 ? TASK_ACTS : TASK_ASKS;
    snprintf(out->at, sizeof(out->at), "%.5s", get_str(o, "at"));
    snprintf(out->date, sizeof(out->date), "%.10s", get_str(o, "date"));
    const cJSON *days = cJSON_GetObjectItemCaseSensitive(o, "days");
    const cJSON *d;
    cJSON_ArrayForEach(d, days)
    {
        if (cJSON_IsNumber(d) && d->valueint >= 1 && d->valueint <= 7) {
            out->days |= (uint8_t) (1u << (d->valueint - 1));
        }
    }
    out->days = out->days == 0x7F ? 0 : out->days;
    const double day = get_num(o, "day"), every = get_num(o, "every");
    out->day = (uint8_t) (day < 0 ? 0 : day > 31 ? 31 : day);
    out->every = (uint8_t) (every < 0 ? 0 : every > 168 ? 168 : every);
    out->next = (int64_t) get_num(o, "next");
    out->last = (int64_t) get_num(o, "last");
    out->enabled = get_bool(o, "enabled", true);
    out->failed = get_bool(o, "failed", false);
    out->running = get_bool(o, "running", false);
    return true;
}

bool tasks_set(const cJSON *args, char *err, size_t len)
{
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(args, "tasks");
    if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) > TASKS_MAX) {
        snprintf(err, len, "tasks must be an array of at most %d", TASKS_MAX);
        return false;
    }
    int n = 0;
    const cJSON *o;
    cJSON_ArrayForEach(o, list)
    {
        if (!parse_one(o, &s_new[n], err, len)) {
            return false;
        }
        n++;
    }
    memcpy(s_saved.list, s_new, (size_t) n * sizeof(task_t));
    s_saved.n = n;
    s_version++;
    save();
    ESP_LOGI(TAG, "%d tasks", n);
    return true;
}

int tasks_count(void)
{
    return s_saved.n;
}

const task_t *tasks_at(int i)
{
    return i >= 0 && i < s_saved.n ? &s_saved.list[i] : NULL;
}

uint32_t tasks_version(void)
{
    return s_version;
}

int tasks_failed(void)
{
    int n = 0;
    for (int i = 0; i < s_saved.n; i++) {
        n += s_saved.list[i].enabled && s_saved.list[i].failed;
    }
    return n;
}

void tasks_when(const task_t *t, char *out, size_t len)
{
    static const char *const EN[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    static const char *const IT[] = {"Lun", "Mar", "Mer", "Gio", "Ven", "Sab", "Dom"};
    const char *const *names = card_italian() ? IT : EN;
    const char *at = card_tr("at", "alle");
    switch (t->repeat) {
    case TASK_ONCE:
        // "2026-11-04" as "4/11".
        snprintf(out, len, "%s %d/%d %s %s", card_tr("Once,", "Una volta, il"), atoi(t->date + 8), atoi(t->date + 5), at,
                 t->at);
        break;
    case TASK_MONTHLY:
        snprintf(out, len, card_tr("Day %d of the month at %s", "Il %d del mese alle %s"), t->day, t->at);
        break;
    case TASK_HOURLY:
        if (t->every <= 1) {
            snprintf(out, len, "%s", card_tr("Every hour", "Ogni ora"));
        } else {
            snprintf(out, len, card_tr("Every %d hours", "Ogni %d ore"), t->every);
        }
        break;
    default: {
        if (t->days == 0) {
            snprintf(out, len, "%s %s %s", card_tr("Every day", "Ogni giorno"), at, t->at);
            break;
        }
        int n = 0;
        for (int d = 0; d < 7 && n < (int) len; d++) {
            if (t->days & (1u << d)) {
                n += snprintf(out + n, len - (size_t) n, "%s%s", n ? ", " : "", names[d]);
            }
        }
        if (n < (int) len) {
            snprintf(out + n, len - (size_t) n, " %s %s", at, t->at);
        }
        break;
    }
    }
}

bool tasks_run(int i)
{
    task_t *t = i >= 0 && i < s_saved.n ? &s_saved.list[i] : NULL;
    if (t == NULL || !metrics_model_live()) {
        return false;
    }
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "id", t->id);
    rpc_notify("task_run", params);
    // Until the host's next list says how it went.
    t->running = true;
    s_version++;
    ESP_LOGI(TAG, "run %s", t->id);
    return true;
}
