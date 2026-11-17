#include "timers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "card.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "metrics_model.h"
#include "rpc.h"

static const char *TAG = "timers";

/* Ids of the board's own timers start with it; the host's are numbers. */
#define LOCAL_PREFIX 'b'
/* A ringing timer nobody stops goes after this, on the board as on the host. */
#define RING_MS 60000
/* A pomodoro's phase rings this long before the next one starts. */
#define PHASE_RING_MS 6000
#define POMODORO_WORK_S (25 * 60)
#define POMODORO_BREAK_S (5 * 60)
#define POMODORO_ROUNDS 4

typedef struct {
    timer_view_t v;
    /* Uptime in ms when `v.left_s` was true. */
    uint32_t at_ms;
    /* When it started ringing on the board. */
    uint32_t ring_ms;
    /* The board's own: its pomodoro round (0 for a plain timer), and
     * whether the host has been offered it since it came. */
    uint8_t round;
    bool sent;
} entry_t;

/* PSRAM: internal RAM is for WiFi and the voice. */
EXT_RAM_BSS_ATTR static entry_t s_list[TIMERS_MAX];
EXT_RAM_BSS_ATTR static entry_t s_new[TIMERS_MAX];
EXT_RAM_BSS_ATTR static entry_t s_local[TIMERS_MAX];
/* What the host took over (`was` in its list), one per entry of s_new. */
EXT_RAM_BSS_ATTR static char s_was[TIMERS_MAX][TIMERS_ID_MAX + 1];

static struct {
    int n;
    int nl;
    uint32_t version;
    /* The one that started ringing, until behaviour.c takes it. */
    bool ring;
    char ring_label[TIMERS_LABEL_CHARS + 1];
    /* The order timers_at() goes in, fixed when the list changes: the host's
     * as entries 0..n-1, the board's as n.. */
    int order[2 * TIMERS_MAX];
    int count;
    uint32_t order_version;
} s;

static uint32_t now_ms(void)
{
    return (uint32_t) (esp_timer_get_time() / 1000);
}

static bool is_local(const char *id)
{
    return id[0] == LOCAL_PREFIX;
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

static const char *kind_name(timer_kind_t kind)
{
    static const char *const NAMES[] = {"timer", "work", "break", "reminder"};
    return NAMES[kind];
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

static entry_t *find(entry_t *list, int n, const char *id)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].v.id, id) == 0) {
            return &list[i];
        }
    }
    return NULL;
}

/* Seconds left now. */
static float left_now(const entry_t *e)
{
    if (e->v.state != TIMER_RUN) {
        return e->v.left_s;
    }
    return fmaxf(0, e->v.left_s - (float) (now_ms() - e->at_ms) / 1000.0f);
}

static void changed(void)
{
    s.version++;
}

static bool parse_one(const cJSON *t, entry_t *e, char *was, char *err, size_t len)
{
    memset(e, 0, sizeof(*e));
    was[0] = '\0';
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(t, "id");
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(t, "kind");
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(t, "state");
    const cJSON *left = cJSON_GetObjectItemCaseSensitive(t, "left_s");
    const cJSON *total = cJSON_GetObjectItemCaseSensitive(t, "total_s");
    const cJSON *label = cJSON_GetObjectItemCaseSensitive(t, "label");
    const cJSON *at = cJSON_GetObjectItemCaseSensitive(t, "at");
    const cJSON *from = cJSON_GetObjectItemCaseSensitive(t, "was");
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
    if (cJSON_IsString(from) && strlen(from->valuestring) <= TIMERS_ID_MAX) {
        snprintf(was, TIMERS_ID_MAX + 1, "%s", from->valuestring);
    }
    e->at_ms = now_ms();
    return true;
}

static void ring(entry_t *e)
{
    e->v.left_s = 0;
    e->v.state = TIMER_RING;
    e->ring_ms = now_ms();
    s.ring = true;
    snprintf(s.ring_label, sizeof(s.ring_label), "%s", e->v.label);
    changed();
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
        if (!parse_one(t, &s_new[n], s_was[n], err, len)) {
            return false;
        }
        n++;
    }
    for (int i = 0; i < n; i++) {
        // A ring that is new to the board: Yoru jumps.
        const entry_t *was = find(s_list, s.n, s_new[i].v.id);
        if (s_new[i].v.state == TIMER_RING) {
            if (was == NULL || was->v.state != TIMER_RING) {
                ring(&s_new[i]);
            } else {
                s_new[i].ring_ms = was->ring_ms;
            }
        }
        // One of the board's own the host took over: it's the host's now.
        entry_t *mine = s_was[i][0] != '\0' ? find(s_local, s.nl, s_was[i]) : NULL;
        if (mine != NULL) {
            ESP_LOGI(TAG, "%s is the host's %s now", mine->v.id, s_new[i].v.id);
            memmove(mine, mine + 1, (size_t) (s_local + s.nl - mine - 1) * sizeof(entry_t));
            s.nl--;
        }
    }
    memcpy(s_list, s_new, (size_t) n * sizeof(entry_t));
    s.n = n;
    changed();
    ESP_LOGI(TAG, "%d timers", n);
    return true;
}

/* The ringing ones first, then the one that ends first. */
static float rank(int k)
{
    const entry_t *e = k < s.n ? &s_list[k] : &s_local[k - s.n];
    return e->v.state == TIMER_RING ? -1.0f : left_now(e);
}

static int order(const void *a, const void *b)
{
    const float x = rank(*(const int *) a), y = rank(*(const int *) b);
    return x < y ? -1 : x > y ? 1 : 0;
}

/* The order is set when the list changes, so rows don't swap places as a
 * paused timer is overtaken. */
static void sort_if_changed(void)
{
    if (s.order_version == s.version && s.count == s.n + s.nl) {
        return;
    }
    s.count = s.n + s.nl;
    for (int k = 0; k < s.count; k++) {
        s.order[k] = k;
    }
    qsort(s.order, (size_t) s.count, sizeof(int), order);
    s.order_version = s.version;
}

int timers_count(void)
{
    return s.n + s.nl;
}

bool timers_at(int i, timer_view_t *out)
{
    sort_if_changed();
    if (i < 0 || i >= s.count) {
        return false;
    }
    const int k = s.order[i];
    const entry_t *e = k < s.n ? &s_list[k] : &s_local[k - s.n];
    *out = e->v;
    out->left_s = left_now(e);
    return true;
}

uint32_t timers_version(void)
{
    return s.version;
}

/* ---- The board's own --------------------------------------------------------- */

static entry_t *add_local(timer_kind_t kind, int secs, const char *label)
{
    if (s.n + s.nl >= TIMERS_MAX) {
        return NULL;
    }
    entry_t *e = &s_local[s.nl++];
    memset(e, 0, sizeof(*e));
    snprintf(e->v.id, sizeof(e->v.id), "%c%08lx", LOCAL_PREFIX, (unsigned long) esp_random());
    e->v.kind = kind;
    e->v.state = TIMER_RUN;
    e->v.left_s = e->v.total_s = (float) secs;
    if (label != NULL) {
        card_ascii(label, e->v.label, TIMERS_LABEL_CHARS);
    }
    e->at_ms = now_ms();
    changed();
    return e;
}

bool timers_start(int secs, const char *label)
{
    if (secs <= 0 || add_local(TIMER_KIND_TIMER, secs, label) == NULL) {
        return false;
    }
    ESP_LOGI(TAG, "a timer of %d s on the board", secs);
    return true;
}

bool timers_start_pomodoro(void)
{
    // One pomodoro at a time: a new one starts over.
    for (int i = s.nl - 1; i >= 0; i--) {
        if (s_local[i].round > 0) {
            memmove(&s_local[i], &s_local[i + 1], (size_t) (s.nl - i - 1) * sizeof(entry_t));
            s.nl--;
        }
    }
    entry_t *e = add_local(TIMER_KIND_WORK, POMODORO_WORK_S, NULL);
    if (e == NULL) {
        return false;
    }
    e->round = 1;
    ESP_LOGI(TAG, "a pomodoro on the board");
    return true;
}

/* A pomodoro's phase is over: the break after work, the next round's work
 * after a break, nothing after the last round's work. */
static bool next_phase(entry_t *e)
{
    const bool work = e->v.kind == TIMER_KIND_WORK;
    if (work && e->round >= POMODORO_ROUNDS) {
        return false;
    }
    e->v.kind = work ? TIMER_KIND_BREAK : TIMER_KIND_WORK;
    e->round += work ? 0 : 1;
    e->v.state = TIMER_RUN;
    e->v.left_s = e->v.total_s = work ? POMODORO_BREAK_S : POMODORO_WORK_S;
    e->at_ms = now_ms();
    e->sent = false;
    changed();
    return true;
}

static void remove_local(entry_t *e)
{
    memmove(e, e + 1, (size_t) (s_local + s.nl - e - 1) * sizeof(entry_t));
    s.nl--;
    changed();
}

/* Offer the host one of the board's own (`timer_new`): a host that takes it
 * sends it back in its list, with `was` naming it. */
static void offer(entry_t *e)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "id", e->v.id);
    cJSON_AddStringToObject(p, "kind", kind_name(e->v.kind));
    cJSON_AddStringToObject(p, "state", e->v.state == TIMER_PAUSE ? "pause" : "run");
    cJSON_AddNumberToObject(p, "left_s", roundf(left_now(e) * 10) / 10);
    cJSON_AddNumberToObject(p, "total_s", e->v.total_s);
    if (e->v.label[0] != '\0') {
        cJSON_AddStringToObject(p, "label", e->v.label);
    }
    // What the host says when it rings: the board's language.
    cJSON_AddStringToObject(p, "language", card_italian() ? "it" : "en");
    if (e->round > 0) {
        cJSON *pomo = cJSON_AddObjectToObject(p, "pomodoro");
        cJSON_AddNumberToObject(pomo, "work_s", POMODORO_WORK_S);
        cJSON_AddNumberToObject(pomo, "break_s", POMODORO_BREAK_S);
        cJSON_AddNumberToObject(pomo, "round", e->round);
        cJSON_AddNumberToObject(pomo, "rounds", POMODORO_ROUNDS);
    }
    rpc_notify("timer_new", p);
    e->sent = true;
    ESP_LOGI(TAG, "%s offered to the host", e->v.id);
}

void timers_poll(void)
{
    const uint32_t now = now_ms();
    const bool host = metrics_model_live();
    for (int i = s.nl - 1; i >= 0; i--) {
        entry_t *e = &s_local[i];
        if (e->v.state == TIMER_RUN && left_now(e) <= 0) {
            ring(e);
        } else if (e->v.state == TIMER_RING && now - e->ring_ms >= (e->round > 0 ? PHASE_RING_MS : RING_MS)) {
            if (e->round == 0 || !next_phase(e)) {
                remove_local(e);
            }
        } else if (e->v.state != TIMER_RING && host && !e->sent) {
            offer(e);
        } else if (!host) {
            e->sent = false; // offered again to the next host
        }
    }
    // The host rings its own; once it has gone, the board does it for it.
    for (int i = s.n - 1; i >= 0 && !host; i--) {
        entry_t *e = &s_list[i];
        if (e->v.state == TIMER_RUN && left_now(e) <= 0) {
            ring(e);
        } else if (e->v.state == TIMER_RING && now - e->ring_ms >= RING_MS) {
            memmove(e, e + 1, (size_t) (s.n - i - 1) * sizeof(entry_t));
            s.n--;
            changed();
        }
    }
}

bool timers_take_ring(char *label, size_t len)
{
    if (!s.ring) {
        return false;
    }
    s.ring = false;
    snprintf(label, len, "%s", s.ring_label);
    return true;
}

/* What a tap does to an entry, on the board. */
static void apply(entry_t *e, const char *action)
{
    if (strcmp(action, "pause") == 0 && e->v.state == TIMER_RUN) {
        e->v.left_s = left_now(e);
        e->v.state = TIMER_PAUSE;
    } else if (strcmp(action, "resume") == 0 && e->v.state == TIMER_PAUSE) {
        e->v.state = TIMER_RUN;
        e->at_ms = now_ms();
    }
    e->sent = false; // a host is told how it stands now
    changed();
}

void timers_act(const char *id, const char *action)
{
    const bool gone = strcmp(action, "cancel") == 0 || strcmp(action, "dismiss") == 0;
    if (is_local(id)) {
        entry_t *e = find(s_local, s.nl, id);
        if (e == NULL) {
            return;
        }
        ESP_LOGI(TAG, "%s: %s", id, action);
        // Stopping a pomodoro's ring goes on to the next phase.
        if (gone && !(strcmp(action, "dismiss") == 0 && e->round > 0 && next_phase(e))) {
            remove_local(e);
        } else if (!gone) {
            apply(e, action);
        }
        return;
    }
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "id", id);
    cJSON_AddStringToObject(params, "action", action);
    rpc_notify("timer_action", params);
    ESP_LOGI(TAG, "%s: %s", id, action);
    entry_t *e = find(s_list, s.n, id);
    if (e == NULL) {
        return;
    }
    if (gone) {
        memmove(e, e + 1, (size_t) (s_list + s.n - e - 1) * sizeof(entry_t));
        s.n--;
        changed();
    } else {
        apply(e, action);
    }
}
