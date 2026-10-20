#include "habits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "card.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "rpc.h"

static const char *TAG = "habits";

/* Each habit's colour when the host gives none: the Activity rings' and a
 * few more, bright on the black. */
static const uint32_t PALETTE[] = {0xFF375F, 0x30D158, 0x40C8E0, 0xFFD60A, 0xBF5AF2, 0xFF9F0A, 0x64D2FF, 0xFF6482};
#define PALETTE_N ((int) (sizeof(PALETTE) / sizeof(PALETTE[0])))

EXT_RAM_BSS_ATTR static habit_t s_list[HABITS_MAX];
EXT_RAM_BSS_ATTR static habit_t s_new[HABITS_MAX];

static struct {
    int n;
    uint32_t version;
} s;

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
    memcpy(s_list, s_new, (size_t) n * sizeof(habit_t));
    s.n = n;
    s.version++;
    ESP_LOGI(TAG, "%d habits, %d done", n, habits_done());
    return true;
}

int habits_count(void)
{
    return s.n;
}

const habit_t *habits_at(int i)
{
    return i >= 0 && i < s.n ? &s_list[i] : NULL;
}

uint32_t habits_version(void)
{
    return s.version;
}

float habits_progress(void)
{
    if (s.n == 0) {
        return 0;
    }
    float sum = 0;
    for (int i = 0; i < s.n; i++) {
        sum += (float) s_list[i].done / (float) s_list[i].goal;
    }
    return sum / (float) s.n;
}

int habits_done(void)
{
    int n = 0;
    for (int i = 0; i < s.n; i++) {
        n += s_list[i].done >= s_list[i].goal;
    }
    return n;
}

int habits_tick(int i, int delta)
{
    if (i < 0 || i >= s.n) {
        return -1;
    }
    habit_t *h = &s_list[i];
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
    s.version++;
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "id", h->id);
    cJSON_AddNumberToObject(params, "done", done);
    rpc_notify("habit_check", params);
    ESP_LOGI(TAG, "%s: %d/%d", h->id, done, h->goal);
    return done;
}
