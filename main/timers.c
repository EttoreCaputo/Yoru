#include "timers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "card.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "metrics_model.h"
#include "rpc.h"

static const char *TAG = "timers";

typedef struct {
    timer_view_t v;
    /* Uptime in ms when `v.left_s` was true. */
    uint32_t at_ms;
} entry_t;

/* PSRAM: internal RAM is for WiFi and the voice. */
EXT_RAM_BSS_ATTR static entry_t s_list[TIMERS_MAX];
EXT_RAM_BSS_ATTR static entry_t s_new[TIMERS_MAX];

static struct {
    int n;
    /* The host has sent a list: the snapshot's timer is no longer needed. */
    bool hosted;
    uint32_t version;
    /* The one that started ringing, until behaviour.c takes it. */
    bool ring;
    char ring_label[TIMERS_LABEL_CHARS + 1];
    /* The snapshot's timer was ringing last time we looked. */
    bool snap_ringing;
} s;

static uint32_t now_ms(void)
{
    return (uint32_t) (esp_timer_get_time() / 1000);
}

static bool kind_from_name(const char *name, timer_kind_t *out)
{
    static const char *const NAMES[] = {"timer", "work", "break", "reminder"};
    for (int i = 0; i < 4; i++) {
        if (strcmp(name, NAMES[i]) == 0) {
            *out = (timer_kind_t) i;
            return true;
        }
    }
    return false;
}

static bool state_from_name(const char *name, timer_state_t *out)
{
    static const char *const NAMES[] = {"run", "pause", "ring"};
    for (int i = 0; i < 3; i++) {
        if (strcmp(name, NAMES[i]) == 0) {
            *out = (timer_state_t) i;
            return true;
        }
    }
    return false;
}

static const entry_t *find(const entry_t *list, int n, const char *id)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].v.id, id) == 0) {
            return &list[i];
        }
    }
    return NULL;
}

static bool parse_one(const cJSON *t, entry_t *e, char *err, size_t len)
{
    memset(e, 0, sizeof(*e));
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(t, "id");
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(t, "kind");
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(t, "state");
    const cJSON *left = cJSON_GetObjectItemCaseSensitive(t, "left_s");
    const cJSON *total = cJSON_GetObjectItemCaseSensitive(t, "total_s");
    const cJSON *label = cJSON_GetObjectItemCaseSensitive(t, "label");
    const cJSON *at = cJSON_GetObjectItemCaseSensitive(t, "at");
    // The host's ids are numbers; strings are fine too.
    if (cJSON_IsNumber(id)) {
        snprintf(e->v.id, sizeof(e->v.id), "%d", (int) id->valuedouble);
    } else if (cJSON_IsString(id) && id->valuestring[0] != '\0' && strlen(id->valuestring) <= TIMERS_ID_MAX) {
        snprintf(e->v.id, sizeof(e->v.id), "%s", id->valuestring);
    } else {
        snprintf(err, len, "each timer needs an id (a number, or a string of at most %d bytes)", TIMERS_ID_MAX);
        return false;
    }
    if (kind != NULL && (!cJSON_IsString(kind) || !kind_from_name(kind->valuestring, &e->v.kind))) {
        snprintf(err, len, "kind must be timer, work, break or reminder");
        return false;
    }
    if (!cJSON_IsString(state) || !state_from_name(state->valuestring, &e->v.state)) {
        snprintf(err, len, "state must be run, pause or ring");
        return false;
    }
    if (!cJSON_IsNumber(left) || !cJSON_IsNumber(total) || left->valuedouble < 0 || total->valuedouble <= 0) {
        snprintf(err, len, "left_s and total_s must be numbers, total_s above 0");
        return false;
    }
    e->v.left_s = (float) left->valuedouble;
    e->v.total_s = (float) total->valuedouble;
    if (cJSON_IsString(label)) {
        card_ascii(label->valuestring, e->v.label, TIMERS_LABEL_CHARS);
    }
    if (cJSON_IsString(at) && strlen(at->valuestring) == 5) {
        snprintf(e->v.at, sizeof(e->v.at), "%s", at->valuestring);
    }
    e->at_ms = now_ms();
    return true;
}

/* The one that ends first first, the ringing ones before all. */
static int order(const void *a, const void *b)
{
    const timer_view_t *x = &((const entry_t *) a)->v, *y = &((const entry_t *) b)->v;
    if ((x->state == TIMER_RING) != (y->state == TIMER_RING)) {
        return x->state == TIMER_RING ? -1 : 1;
    }
    return x->left_s < y->left_s ? -1 : x->left_s > y->left_s ? 1 : 0;
}

bool timers_set(const cJSON *args, char *err, size_t len)
{
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(args, "timers");
    if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) > TIMERS_MAX) {
        snprintf(err, len, "timers must be an array of at most %d", TIMERS_MAX);
        return false;
    }
    int n = 0;
    const cJSON *t;
    cJSON_ArrayForEach(t, list)
    {
        if (!parse_one(t, &s_new[n], err, len)) {
            return false;
        }
        n++;
    }
    qsort(s_new, (size_t) n, sizeof(entry_t), order);
    // A ring that is new to the board: Yoru jumps.
    for (int i = 0; i < n; i++) {
        const entry_t *was = find(s_list, s.n, s_new[i].v.id);
        if (s_new[i].v.state == TIMER_RING && (was == NULL || was->v.state != TIMER_RING)) {
            s.ring = true;
            snprintf(s.ring_label, sizeof(s.ring_label), "%s", s_new[i].v.label);
        }
    }
    memcpy(s_list, s_new, (size_t) n * sizeof(entry_t));
    s.n = n;
    s.hosted = true;
    s.version++;
    ESP_LOGI(TAG, "%d timers", n);
    return true;
}

/* The snapshot's timer, for a host that doesn't send the list. */
static bool snapshot_timer(timer_view_t *out)
{
    static metrics_snapshot_t snap; // big for the LVGL task's stack
    metrics_model_get(&snap);
    const metrics_timer_t *t = &snap.timer;
    if (snap.state != METRICS_UI_LIVE || !t->valid) {
        s.snap_ringing = false;
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->kind = (timer_kind_t) t->kind;
    out->state = t->ringing ? TIMER_RING : t->paused ? TIMER_PAUSE : TIMER_RUN;
    out->total_s = t->total_s;
    out->left_s = t->left_s;
    if (out->state == TIMER_RUN) {
        out->left_s = fmaxf(0, t->left_s - (float) (now_ms() - snap.updated_ms) / 1000.0f);
    }
    snprintf(out->label, sizeof(out->label), "%s", t->label);
    if (t->ringing && !s.snap_ringing) {
        s.ring = true;
        snprintf(s.ring_label, sizeof(s.ring_label), "%s", t->label);
    }
    s.snap_ringing = t->ringing;
    return true;
}

int timers_count(void)
{
    if (s.hosted) {
        return s.n;
    }
    timer_view_t v;
    return snapshot_timer(&v) ? 1 : 0;
}

bool timers_at(int i, timer_view_t *out)
{
    if (!s.hosted) {
        return i == 0 && snapshot_timer(out);
    }
    if (i < 0 || i >= s.n) {
        return false;
    }
    *out = s_list[i].v;
    if (out->state == TIMER_RUN) {
        out->left_s = fmaxf(0, out->left_s - (float) (now_ms() - s_list[i].at_ms) / 1000.0f);
    }
    return true;
}

uint32_t timers_version(void)
{
    return s.version;
}

bool timers_take_ring(char *label, size_t len)
{
    if (!s.hosted) {
        timer_view_t v;
        snapshot_timer(&v); // notices a ring of the snapshot's timer
    }
    if (!s.ring) {
        return false;
    }
    s.ring = false;
    snprintf(label, len, "%s", s.ring_label);
    return true;
}

void timers_act(const char *id, const char *action)
{
    if (id[0] == '\0') {
        return; // the snapshot's: nowhere to send it
    }
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "id", id);
    cJSON_AddStringToObject(params, "action", action);
    rpc_notify("timer_action", params);
    ESP_LOGI(TAG, "%s: %s", id, action);
    for (int i = 0; i < s.n; i++) {
        entry_t *e = &s_list[i];
        if (strcmp(e->v.id, id) != 0) {
            continue;
        }
        if (strcmp(action, "cancel") == 0 || strcmp(action, "dismiss") == 0) {
            memmove(e, e + 1, (size_t) (s.n - i - 1) * sizeof(entry_t));
            s.n--;
        } else if (strcmp(action, "pause") == 0 && e->v.state == TIMER_RUN) {
            timer_view_t now;
            timers_at(i, &now);
            e->v.left_s = now.left_s;
            e->v.state = TIMER_PAUSE;
        } else if (strcmp(action, "resume") == 0 && e->v.state == TIMER_PAUSE) {
            e->v.state = TIMER_RUN;
            e->at_ms = now_ms();
        }
        s.version++;
        return;
    }
}
