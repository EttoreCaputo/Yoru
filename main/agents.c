#include "agents.h"

#include <stdio.h>
#include <string.h>

#include "card.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "agents";

/* The host resends every 10 s; three missed and they're gone. */
#define STALE_MS 30000

static const char *const STATE_NAMES[] = {"idle", "work", "wait", "done", "error"};

/* A few KB, read by the LVGL task: PSRAM (CLAUDE.md). The parse goes into
 * `next`, so a wrong call leaves the last one standing. */
EXT_RAM_BSS_ATTR static agents_t s_now;
EXT_RAM_BSS_ATTR static agents_t s_next;

static SemaphoreHandle_t s_lock;
static uint32_t s_version;
static uint32_t s_events;
static bool s_stale_seen;

static uint32_t now_ms(void)
{
    return (uint32_t) (esp_timer_get_time() / 1000);
}

static bool state_from(const char *name, agent_state_t *out)
{
    for (int i = 0; i < (int) (sizeof(STATE_NAMES) / sizeof(STATE_NAMES[0])); i++) {
        if (strcmp(name, STATE_NAMES[i]) == 0) {
            *out = (agent_state_t) i;
            return true;
        }
    }
    return false;
}

const char *agents_state_name(agent_state_t state)
{
    return (unsigned) state < sizeof(STATE_NAMES) / sizeof(STATE_NAMES[0]) ? STATE_NAMES[state] : "idle";
}

/* A string field as ASCII, or "" when it's missing. */
static void text_of(const cJSON *obj, const char *key, char *out, size_t size)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    card_ascii(cJSON_IsString(v) ? v->valuestring : "", out, size - 1);
}

static int int_of(const cJSON *obj, const char *key, int none)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? (int) v->valuedouble : none;
}

static void parse_step(const cJSON *j, agent_step_t *st)
{
    text_of(j, "kind", st->kind, sizeof(st->kind));
    text_of(j, "text", st->text, sizeof(st->text));
    st->add = (int16_t) int_of(j, "add", -1);
    st->del = (int16_t) int_of(j, "del", -1);
    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(j, "ok");
    st->ok = cJSON_IsBool(ok) ? (int8_t) cJSON_IsTrue(ok) : -1;
}

static bool parse_session(const cJSON *j, agent_session_t *a, char *err, size_t len)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(j, "id");
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(j, "state");
    if (!cJSON_IsString(id) || id->valuestring[0] == '\0' || strlen(id->valuestring) > AGENTS_ID_MAX) {
        snprintf(err, len, "each session needs an id of 1 to %d bytes", AGENTS_ID_MAX);
        return false;
    }
    if (!cJSON_IsString(state) || !state_from(state->valuestring, &a->state)) {
        snprintf(err, len, "state must be idle, work, wait, done or error");
        return false;
    }
    snprintf(a->id, sizeof(a->id), "%s", id->valuestring);
    text_of(j, "agent", a->agent, sizeof(a->agent));
    text_of(j, "project", a->project, sizeof(a->project));
    text_of(j, "task", a->task, sizeof(a->task));
    a->since_s = int_of(j, "since_s", 0);
    a->nsteps = 0;
    const cJSON *steps = cJSON_GetObjectItemCaseSensitive(j, "steps");
    const int n = cJSON_IsArray(steps) ? cJSON_GetArraySize(steps) : 0;
    // The last ones, if the host sent more.
    for (int i = n > AGENTS_STEPS ? n - AGENTS_STEPS : 0; i < n; i++) {
        const cJSON *st = cJSON_GetArrayItem(steps, i);
        if (cJSON_IsObject(st)) {
            parse_step(st, &a->steps[a->nsteps++]);
        }
    }
    return true;
}

static const agent_session_t *find(const agents_t *in, const char *id)
{
    for (int i = 0; i < in->n; i++) {
        if (strcmp(in->sessions[i].id, id) == 0) {
            return &in->sessions[i];
        }
    }
    return NULL;
}

/* What Yoru should notice going from `was` to `now`. */
static uint32_t changes(const agents_t *was, const agents_t *now)
{
    uint32_t ev = 0;
    for (int i = 0; i < now->n; i++) {
        const agent_session_t *a = &now->sessions[i];
        const agent_session_t *b = was->valid ? find(was, a->id) : NULL;
        const agent_state_t before = b != NULL ? b->state : AGENT_IDLE;
        if (a->state == AGENT_WAIT && before != AGENT_WAIT) {
            ev |= AGENTS_EV_WAIT;
        } else if (a->state == AGENT_DONE && before == AGENT_WORK) {
            ev |= AGENTS_EV_DONE;
        } else if (a->state == AGENT_ERROR && b != NULL && before != AGENT_ERROR) {
            ev |= AGENTS_EV_ERROR;
        }
    }
    if (was->valid && was->quiet_min == 0 && now->quiet_min > 0) {
        ev |= AGENTS_EV_QUIET_ON;
    } else if (was->valid && was->quiet_min > 0 && now->quiet_min == 0) {
        ev |= AGENTS_EV_QUIET_OFF;
    }
    return ev;
}

void agents_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_now, 0, sizeof(s_now));
}

bool agents_set(const cJSON *args, char *err, size_t len)
{
    err[0] = '\0';
    const cJSON *sessions = cJSON_GetObjectItemCaseSensitive(args, "sessions");
    if (!cJSON_IsArray(sessions)) {
        snprintf(err, len, "sessions must be an array");
        return false;
    }
    // Only the link task parses, so `next` needs no lock of its own.
    memset(&s_next, 0, sizeof(s_next));
    const cJSON *j;
    cJSON_ArrayForEach(j, sessions)
    {
        if (s_next.n >= AGENTS_MAX) {
            break; // the most urgent ones come first
        }
        if (!cJSON_IsObject(j) || !parse_session(j, &s_next.sessions[s_next.n], err, len)) {
            if (err[0] == '\0') {
                snprintf(err, len, "each session must be an object");
            }
            return false;
        }
        s_next.n++;
    }
    const int quiet = int_of(args, "quiet_min", 0);
    s_next.quiet_min = quiet < 0 ? 0 : quiet;
    s_next.focus = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(args, "focus"));
    s_next.valid = true;
    s_next.at_ms = now_ms();

    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool was_valid = s_now.valid && now_ms() - s_now.at_ms < STALE_MS;
    if (!was_valid) {
        s_now.valid = false;
    }
    s_events |= changes(&s_now, &s_next);
    s_now = s_next;
    s_version++;
    s_stale_seen = false;
    xSemaphoreGive(s_lock);
    ESP_LOGD(TAG, "%d session(s), quiet %d min", s_next.n, s_next.quiet_min);
    return true;
}

/* With the lock held: forget the sessions once the host has gone quiet. */
static void check_stale(void)
{
    if (s_now.valid && now_ms() - s_now.at_ms >= STALE_MS) {
        s_now.valid = false;
        s_now.n = 0;
        s_now.quiet_min = 0;
        if (!s_stale_seen) {
            s_stale_seen = true;
            s_version++;
            ESP_LOGI(TAG, "no news from the host: sessions forgotten");
        }
    }
}

void agents_get(agents_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    check_stale();
    *out = s_now;
    xSemaphoreGive(s_lock);
}

void agents_summary(agents_summary_t *out)
{
    memset(out, 0, sizeof(*out));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    check_stale();
    out->n = s_now.n;
    out->quiet_min = s_now.quiet_min;
    out->focus = s_now.focus;
    for (int i = 0; i < s_now.n; i++) {
        const agent_session_t *a = &s_now.sessions[i];
        out->working += a->state == AGENT_WORK;
        out->errors += a->state == AGENT_ERROR;
        if (a->state == AGENT_WAIT) {
            if (out->waiting++ == 0) {
                snprintf(out->wait_id, sizeof(out->wait_id), "%s", a->id);
            }
        }
    }
    xSemaphoreGive(s_lock);
}

uint32_t agents_version(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    check_stale();
    const uint32_t v = s_version;
    xSemaphoreGive(s_lock);
    return v;
}

uint32_t agents_take_events(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint32_t ev = s_events;
    s_events = 0;
    xSemaphoreGive(s_lock);
    return ev;
}
