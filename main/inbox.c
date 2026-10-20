#include "inbox.h"

#include <stdio.h>
#include <string.h>

#include "agents.h"
#include "board_settings.h"
#include "card.h"
#include "cJSON.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rpc.h"

static const char *TAG = "inbox";

/* About 11 KB: PSRAM, internal RAM is for WiFi and the voice. Newest first. */
EXT_RAM_BSS_ATTR static inbox_item_t s_items[INBOX_MAX];
/* One being built, kept out of the stack for the same reason. */
EXT_RAM_BSS_ATTR static inbox_item_t s_new;

static struct {
    int n;
    uint32_t version;
    inbox_urgency_t alert;
} s = {.alert = INBOX_NONE};

uint32_t inbox_now_s(void)
{
    return (uint32_t) (esp_timer_get_time() / 1000000);
}

bool inbox_urgency_from_name(const char *name, inbox_urgency_t *out)
{
    static const char *const NAMES[] = {"low", "normal", "high", "urgent"};
    for (int i = 0; i < 4; i++) {
        if (strcmp(name, NAMES[i]) == 0) {
            *out = (inbox_urgency_t) i;
            return true;
        }
    }
    return false;
}

static int index_of(const char *id)
{
    for (int i = 0; i < s.n; i++) {
        if (strcmp(s_items[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

static void drop_at(int i)
{
    memmove(&s_items[i], &s_items[i + 1], (size_t) (s.n - i - 1) * sizeof(inbox_item_t));
    s.n--;
}

/* The same words from the same place: a resend, not news. */
static bool same(const inbox_item_t *a, const inbox_item_t *b)
{
    if (strcmp(a->source, b->source) != 0 || strcmp(a->title, b->title) != 0 || strcmp(a->text, b->text) != 0
        || a->nactions != b->nactions || a->urgency != b->urgency) {
        return false;
    }
    for (int i = 0; i < a->nactions; i++) {
        if (strcmp(a->actions[i], b->actions[i]) != 0) {
            return false;
        }
    }
    return true;
}

/* Do not disturb (the board's) and quiet (the host's, for the agents) hold
 * back all but the urgent. */
static inbox_urgency_t loudness(inbox_urgency_t u)
{
    agents_summary_t sum;
    agents_summary(&sum);
    const bool hushed = board_settings_on(SETTING_FOCUS) || sum.quiet_min > 0;
    return hushed && u != INBOX_URGENT ? INBOX_LOW : u;
}

inbox_urgency_t inbox_add(const char *id, const char *source, const char *title, const char *text,
                          const char *const *actions, int nactions, inbox_urgency_t urgency)
{
    inbox_item_t *it = &s_new;
    memset(it, 0, sizeof(*it));
    snprintf(it->id, sizeof(it->id), "%s", id);
    card_ascii(source, it->source, INBOX_SOURCE_CHARS);
    card_ascii(title, it->title, INBOX_TITLE_CHARS);
    card_ascii(text, it->text, INBOX_TEXT_CHARS);
    it->nactions = nactions > INBOX_ACTIONS_MAX ? INBOX_ACTIONS_MAX : nactions;
    for (int i = 0; i < it->nactions; i++) {
        card_ascii(actions[i], it->actions[i], INBOX_ACTION_CHARS);
    }
    it->urgency = urgency;
    it->unread = true;
    it->arrived_s = inbox_now_s();

    const int old = index_of(id);
    if (old >= 0 && same(&s_items[old], it)) {
        return INBOX_NONE; // where it was, read or not
    }
    if (old >= 0) {
        drop_at(old);
    } else if (s.n == INBOX_MAX) {
        ESP_LOGI(TAG, "full: %s goes", s_items[INBOX_MAX - 1].id);
        s.n--; // the oldest
    }
    memmove(&s_items[1], &s_items[0], (size_t) s.n * sizeof(inbox_item_t));
    s_items[0] = *it;
    s.n++;
    s.version++;
    const inbox_urgency_t loud = loudness(urgency);
    if (loud > s.alert) {
        s.alert = loud;
    }
    ESP_LOGI(TAG, "%s %s (%d), %d in the inbox", old >= 0 ? "replaced" : "new", id, (int) loud, s.n);
    return loud;
}

bool inbox_remove(const char *id)
{
    const int i = index_of(id);
    if (i < 0) {
        return false;
    }
    drop_at(i);
    s.version++;
    return true;
}

void inbox_clear(void)
{
    s.n = 0;
    s.version++;
}

int inbox_count(void)
{
    return s.n;
}

int inbox_unread(inbox_urgency_t *top)
{
    int n = 0;
    inbox_urgency_t worst = INBOX_NONE;
    for (int i = 0; i < s.n; i++) {
        if (s_items[i].unread) {
            n++;
            worst = s_items[i].urgency > worst ? s_items[i].urgency : worst;
        }
    }
    if (top != NULL) {
        *top = worst;
    }
    return n;
}

const inbox_item_t *inbox_at(int i)
{
    return i >= 0 && i < s.n ? &s_items[i] : NULL;
}

const inbox_item_t *inbox_find(const char *id)
{
    return inbox_at(index_of(id));
}

uint32_t inbox_version(void)
{
    return s.version;
}

inbox_urgency_t inbox_take_alert(void)
{
    const inbox_urgency_t a = s.alert;
    s.alert = INBOX_NONE;
    return a;
}

void inbox_act(const char *id, const char *action)
{
    const int i = index_of(id);
    if (i < 0) {
        return;
    }
    const bool read = strcmp(action, "read") == 0;
    if (read && !s_items[i].unread) {
        return; // the host knows already
    }
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "id", id);
    cJSON_AddStringToObject(params, "action", action);
    rpc_notify("notification_action", params);
    ESP_LOGI(TAG, "%s: %s", id, action);
    if (strcmp(action, "dismiss") == 0) {
        drop_at(i);
    } else {
        s_items[i].unread = false;
    }
    s.version++;
}
