#include "remote.h"

#include <stdio.h>
#include <string.h>

#include "card.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "rpc.h"

static const char *TAG = "remote";

/* The reply alone is over a KB, read by the LVGL task: PSRAM (CLAUDE.md).
 * Parsed into `next`, so a wrong call leaves the last one standing. */
EXT_RAM_BSS_ATTR static remote_projects_t s_projects;
EXT_RAM_BSS_ATTR static remote_projects_t s_projects_next;
EXT_RAM_BSS_ATTR static remote_chat_t s_chat;
EXT_RAM_BSS_ATTR static remote_chat_t s_chat_next;

static SemaphoreHandle_t s_lock;
static uint32_t s_projects_version;
static uint32_t s_chat_version;

/* A string field as ASCII in `out` (holding chars + 1), or "" without one. */
static void text_of(const cJSON *obj, const char *key, char *out, size_t chars)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    card_ascii(cJSON_IsString(v) ? v->valuestring : "", out, chars);
}

/* An id the board sends back as it came: no conversion, and within bounds. */
static bool id_of(const cJSON *obj, const char *key, char *out, char *err, size_t len)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(v) || v->valuestring[0] == '\0' || strlen(v->valuestring) > REMOTE_ID_MAX) {
        snprintf(err, len, "%s must be a string of 1 to %d bytes", key, REMOTE_ID_MAX);
        return false;
    }
    strcpy(out, v->valuestring);
    return true;
}

void remote_init(void)
{
    s_lock = xSemaphoreCreateMutex();
}

bool remote_set_projects(const cJSON *args, char *err, size_t len)
{
    const cJSON *agents = cJSON_GetObjectItemCaseSensitive(args, "agents");
    const cJSON *projects = cJSON_GetObjectItemCaseSensitive(args, "projects");
    if (!cJSON_IsArray(agents) || !cJSON_IsArray(projects)) {
        snprintf(err, len, "agents and projects must be arrays");
        return false;
    }
    // Only the link task parses, so `next` needs no lock of its own.
    remote_projects_t *p = &s_projects_next;
    memset(p, 0, sizeof(*p));
    const cJSON *j;
    cJSON_ArrayForEach(j, agents)
    {
        if (p->nagents >= REMOTE_AGENTS) {
            break;
        }
        if (!cJSON_IsString(j) || j->valuestring[0] == '\0' || strlen(j->valuestring) > REMOTE_AGENT_CHARS) {
            snprintf(err, len, "agents must be names of 1 to %d bytes", REMOTE_AGENT_CHARS);
            return false;
        }
        strcpy(p->agents[p->nagents++], j->valuestring);
    }
    cJSON_ArrayForEach(j, projects)
    {
        if (p->nprojects >= REMOTE_PROJECTS) {
            break; // the most recent come first
        }
        remote_project_t *pr = &p->projects[p->nprojects];
        if (!cJSON_IsObject(j) || !id_of(j, "id", pr->id, err, len)) {
            if (!cJSON_IsObject(j)) {
                snprintf(err, len, "each project must be an object");
            }
            return false;
        }
        text_of(j, "name", pr->name, REMOTE_NAME_CHARS);
        p->nprojects++;
    }
    p->valid = true;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_projects = *p;
    s_projects_version++;
    xSemaphoreGive(s_lock);
    ESP_LOGD(TAG, "%d agent(s), %d project(s)", p->nagents, p->nprojects);
    return true;
}

bool remote_set_chat(const cJSON *args, char *err, size_t len)
{
    remote_chat_t *c = &s_chat_next;
    memset(c, 0, sizeof(*c));
    if (!id_of(args, "session", c->session, err, len)) {
        return false;
    }
    const cJSON *messages = cJSON_GetObjectItemCaseSensitive(args, "messages");
    if (!cJSON_IsArray(messages)) {
        snprintf(err, len, "messages must be an array");
        return false;
    }
    // The latest ones, if the host sent more than fit.
    const int total = cJSON_GetArraySize(messages);
    for (int i = total > REMOTE_MESSAGES ? total - REMOTE_MESSAGES : 0; i < total; i++) {
        const cJSON *m = cJSON_GetArrayItem(messages, i);
        const cJSON *from = cJSON_GetObjectItemCaseSensitive(m, "from");
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(m, "text");
        if (!cJSON_IsString(from) || !cJSON_IsString(text)) {
            snprintf(err, len, "each message needs from and text");
            return false;
        }
        remote_message_t *out = &c->messages[c->n++];
        out->you = strcmp(from->valuestring, "you") == 0;
        card_ascii_lines(text->valuestring, out->text, REMOTE_MESSAGE_CHARS);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_chat = *c;
    s_chat_version++;
    xSemaphoreGive(s_lock);
    return true;
}

void remote_get_projects(remote_projects_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_projects;
    xSemaphoreGive(s_lock);
}

void remote_get_chat(remote_chat_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_chat;
    xSemaphoreGive(s_lock);
}

uint32_t remote_projects_version(void)
{
    return s_projects_version;
}

uint32_t remote_chat_version(void)
{
    return s_chat_version;
}

void remote_chat_open(const char *session, bool open)
{
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "session", session);
    cJSON_AddBoolToObject(params, "open", open);
    rpc_notify("chat", params);
    ESP_LOGI(TAG, "chat %s %s", session, open ? "open" : "closed");
}
