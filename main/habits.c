#include "habits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "card.h"
#include "clock.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "nvs.h"
#include "rpc.h"

static const char *TAG = "habits";

/* Each habit's colour when the host gives none: the Activity rings' and a
 * few more, bright on the black. */
static const uint32_t PALETTE[] = {0xFF375F, 0x30D158, 0x40C8E0, 0xFFD60A, 0xBF5AF2, 0xFF9F0A, 0x64D2FF, 0xFF6482};
#define PALETTE_N ((int) (sizeof(PALETTE) / sizeof(PALETTE[0])))

#define NVS_NS "habits"
#define NVS_KEY "list"
/* Bumped when `saved_t` changes: an older one is dropped. */
#define SAVED_VERSION 1

/* What NVS keeps, so the list, today's ticks and the streaks outlive a
 * restart and go on without the host. */
typedef struct {
    uint32_t version;
    /* The local day (clock_day()) the counts are for; -1 if not known. */
    int32_t day;
    int32_t n;
    habit_t list[HABITS_MAX];
    /* Ticked on the board since the host last sent its list. */
    bool dirty[HABITS_MAX];
} saved_t;

EXT_RAM_BSS_ATTR static saved_t s_saved;
EXT_RAM_BSS_ATTR static habit_t s_new[HABITS_MAX];

static struct {
    uint32_t version;
} s;

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

void habits_init(void)
{
    s_saved.day = -1;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_saved);
    if (nvs_get_blob(h, NVS_KEY, &s_saved, &len) != ESP_OK || len != sizeof(s_saved)
        || s_saved.version != SAVED_VERSION || s_saved.n < 0 || s_saved.n > HABITS_MAX) {
        memset(&s_saved, 0, sizeof(s_saved));
        s_saved.day = -1;
    }
    nvs_close(h);
    ESP_LOGI(TAG, "%d habits kept, day %ld", (int) s_saved.n, (long) s_saved.day);
}

static bool parse_rgb(const char *text, uint32_t *out)
{
    if (text[0] == '#') {
        text++;
    }
    char *end;
    const unsigned long v = strtoul(text, &end, 16);
    if (strlen(text) != 6 || *end != '\0') {
        return false;
    }
    *out = (uint32_t) v;
    return true;
}

static bool parse_one(const cJSON *h, int i, habit_t *out, char *err, size_t len)
{
    memset(out, 0, sizeof(*out));
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(h, "id");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(h, "name");
    const cJSON *done = cJSON_GetObjectItemCaseSensitive(h, "done");
    const cJSON *goal = cJSON_GetObjectItemCaseSensitive(h, "goal");
    const cJSON *streak = cJSON_GetObjectItemCaseSensitive(h, "streak");
    const cJSON *color = cJSON_GetObjectItemCaseSensitive(h, "color");
    if (!cJSON_IsString(id) || id->valuestring[0] == '\0' || strlen(id->valuestring) > HABITS_ID_MAX) {
        snprintf(err, len, "each habit needs an id of at most %d bytes", HABITS_ID_MAX);
        return false;
    }
    if (!cJSON_IsString(name) || name->valuestring[0] == '\0') {
        snprintf(err, len, "each habit needs a name");
        return false;
    }
    snprintf(out->id, sizeof(out->id), "%s", id->valuestring);
    card_ascii(name->valuestring, out->name, HABITS_NAME_CHARS);
    out->goal = cJSON_IsNumber(goal) ? (int) goal->valuedouble : 1;
    if (out->goal < 1 || out->goal > HABITS_GOAL_MAX) {
        snprintf(err, len, "goal must be 1 to %d", HABITS_GOAL_MAX);
        return false;
    }
    out->done = cJSON_IsNumber(done) ? (int) done->valuedouble : 0;
    out->done = out->done < 0 ? 0 : out->done > out->goal ? out->goal : out->done;
    out->streak = cJSON_IsNumber(streak) && streak->valuedouble > 0 ? (int) streak->valuedouble : 0;
    out->rgb = PALETTE[i % PALETTE_N];
    if (cJSON_IsString(color) && !parse_rgb(color->valuestring, &out->rgb)) {
        snprintf(err, len, "color must be #RRGGBB");
        return false;
    }
    return true;
}

bool habits_set(const cJSON *args, char *err, size_t len)
{
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(args, "habits");
    if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) > HABITS_MAX) {
        snprintf(err, len, "habits must be an array of at most %d", HABITS_MAX);
        return false;
    }
    int n = 0;
    const cJSON *h;
    cJSON_ArrayForEach(h, list)
    {
        if (!parse_one(h, n, &s_new[n], err, len)) {
            return false;
        }
        n++;
    }
    // What was ticked on the board while the host wasn't there (the same
    // day) wins over the host's count, and the host is told.
    const int32_t today = clock_day();
    for (int i = 0; i < n; i++) {
        habit_t *hb = &s_new[i];
        const habit_t *was = NULL;
        for (int k = 0; k < s_saved.n && was == NULL; k++) {
            if (s_saved.dirty[k] && strcmp(s_saved.list[k].id, hb->id) == 0) {
                was = &s_saved.list[k];
            }
        }
        if (was == NULL || s_saved.day != today) {
            continue;
        }
        const int done = was->done > hb->goal ? hb->goal : was->done;
        if (done == hb->done) {
            continue;
        }
        if (done >= hb->goal && hb->done < hb->goal) {
            hb->streak++;
        } else if (done < hb->goal && hb->done >= hb->goal && hb->streak > 0) {
            hb->streak--;
        }
        hb->done = done;
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "id", hb->id);
        cJSON_AddNumberToObject(params, "done", done);
        rpc_notify("habit_check", params);
        ESP_LOGI(TAG, "%s: %d, ticked on the board", hb->id, done);
    }
    memcpy(s_saved.list, s_new, (size_t) n * sizeof(habit_t));
    memset(s_saved.dirty, 0, sizeof(s_saved.dirty));
    s_saved.n = n;
    s_saved.day = today;
    s.version++;
    save();
    ESP_LOGI(TAG, "%d habits, %d done", n, habits_done());
    return true;
}

void habits_poll(void)
{
    const int32_t today = clock_day();
    if (today < 0 || today == s_saved.day) {
        return;
    }
    // A new day without the host: the counts start afresh, and a habit not
    // done yesterday (or a day missed) breaks its streak.
    const bool known = s_saved.day >= 0 && today > s_saved.day;
    for (int i = 0; i < s_saved.n && known; i++) {
        habit_t *h = &s_saved.list[i];
        if (today - s_saved.day > 1 || h->done < h->goal) {
            h->streak = 0;
        }
        h->done = 0;
        s_saved.dirty[i] = false;
    }
    ESP_LOGI(TAG, "day %ld%s", (long) today, known ? ": a new day" : "");
    s_saved.day = today;
    if (known) {
        s.version++;
    }
    save();
}

int habits_count(void)
{
    return s_saved.n;
}

const habit_t *habits_at(int i)
{
    return i >= 0 && i < s_saved.n ? &s_saved.list[i] : NULL;
}

uint32_t habits_version(void)
{
    return s.version;
}

float habits_progress(void)
{
    if (s_saved.n == 0) {
        return 0;
    }
    float sum = 0;
    for (int i = 0; i < s_saved.n; i++) {
        sum += (float) s_saved.list[i].done / (float) s_saved.list[i].goal;
    }
    return sum / (float) s_saved.n;
}

int habits_done(void)
{
    int n = 0;
    for (int i = 0; i < s_saved.n; i++) {
        n += s_saved.list[i].done >= s_saved.list[i].goal;
    }
    return n;
}

int habits_tick(int i, int delta)
{
    if (i < 0 || i >= s_saved.n) {
        return -1;
    }
    habit_t *h = &s_saved.list[i];
    const int done = h->done + delta;
    if (done < 0 || done > h->goal) {
        return -1;
    }
    // Finishing it today makes today count in the streak, and undoing that
    // takes it back.
    if (done == h->goal && h->done < h->goal) {
        h->streak++;
    } else if (done < h->goal && h->done == h->goal && h->streak > 0) {
        h->streak--;
    }
    h->done = done;
    s_saved.dirty[i] = true;
    s.version++;
    save();
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "id", h->id);
    cJSON_AddNumberToObject(params, "done", done);
    rpc_notify("habit_check", params);
    ESP_LOGI(TAG, "%s: %d/%d", h->id, done, h->goal);
    return done;
}
