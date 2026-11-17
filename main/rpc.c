#include "rpc.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "audio_selftest.h"
#include "behaviour.h"
#include "board_tools.h"
#include "cloud.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "perf.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "errand.h"
#include "eyes.h"
#include "link.h"
#include "lvgl_port.h"
#include "net.h"
#include "ota.h"
#include "playback.h"
#include "voice.h"

#define RPC_PROTOCOL 2
/* Yoru on the Waveshare AMOLED 1.75C; the host tells boards apart by it. */
#define RPC_BOARD "yoru-amoled-1.75c"

#define RPC_PARSE_ERROR -32700
#define RPC_INVALID_REQUEST -32600
#define RPC_METHOD_NOT_FOUND -32601
#define RPC_INVALID_PARAMS -32602
#define RPC_INTERNAL_ERROR -32603

static const char *TAG = "rpc";

static void *psram_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static cJSON *envelope(const cJSON *id);

static void send_message(cJSON *msg)
{
    char *text = cJSON_PrintUnformatted(msg);
    if (text == NULL) {
        cJSON_Delete(msg);
        ESP_LOGE(TAG, "out of memory for a reply");
        return;
    }
    size_t len = strlen(text);
    esp_err_t err = len <= LINK_MAX_PAYLOAD ? link_send(LINK_CHAN_CTRL, text, len) : ESP_ERR_INVALID_SIZE;
    cJSON_free(text);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "reply not sent (%u bytes): %s", (unsigned) len, esp_err_to_name(err));
    }
    // A reply too long for a frame: say so, rather than leave the host waiting.
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(msg, "id");
    if (err == ESP_ERR_INVALID_SIZE && id != NULL && !cJSON_IsNull(id)) {
        cJSON *reply = envelope(id);
        cJSON *e = cJSON_AddObjectToObject(reply, "error");
        cJSON_AddNumberToObject(e, "code", RPC_INTERNAL_ERROR);
        cJSON_AddStringToObject(e, "message", "reply too long for a frame");
        send_message(reply);
    }
    cJSON_Delete(msg);
}

static cJSON *envelope(const cJSON *id)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "jsonrpc", "2.0");
    cJSON_AddItemToObject(msg, "id", id != NULL ? cJSON_Duplicate(id, true) : cJSON_CreateNull());
    return msg;
}

static void reply_result(const cJSON *id, cJSON *result)
{
    cJSON *msg = envelope(id);
    cJSON_AddItemToObject(msg, "result", result);
    send_message(msg);
}

static void reply_error(const cJSON *id, int code, const char *message)
{
    cJSON *msg = envelope(id);
    cJSON *err = cJSON_AddObjectToObject(msg, "error");
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message);
    send_message(msg);
}

/** The `hello` result, also the params of `ready`. */
static cJSON *identity(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    cJSON *id = cJSON_CreateObject();
    cJSON_AddNumberToObject(id, "protocol", RPC_PROTOCOL);
    cJSON_AddStringToObject(id, "firmware", app->version);
    cJSON_AddStringToObject(id, "idf", app->idf_ver);
    cJSON_AddStringToObject(id, "board", RPC_BOARD);
    // Which board this is, so a host finds it again on WiFi, and which way
    // this host reaches it.
    cJSON_AddStringToObject(id, "id", net_board_id());
    cJSON_AddStringToObject(id, "transport", link_transport_name(link_active()));
    cJSON_AddNumberToObject(id, "max_payload", LINK_MAX_PAYLOAD);
    cJSON *channels = cJSON_AddArrayToObject(id, "channels");
    cJSON *caps = cJSON_AddArrayToObject(id, "capabilities");
    const char *base[] = {"ctrl", "metrics", "log"};
    for (int i = 0; i < 3; i++) {
        cJSON_AddItemToArray(channels, cJSON_CreateString(base[i]));
    }
    cJSON_AddItemToArray(caps, cJSON_CreateString("tools"));
    // Eyes that follow the host's pointer.
    cJSON_AddItemToArray(caps, cJSON_CreateString("gaze"));
    cJSON_AddItemToArray(caps, cJSON_CreateString("wifi"));
    // The inbox: show_notification and notification_action (docs/protocol.md, Inbox).
    cJSON_AddItemToArray(caps, cJSON_CreateString("inbox"));
    // The menu's Timers and Tasks apps, and the errands (docs/protocol.md,
    // Timers, Tasks, Fetch).
    cJSON_AddItemToArray(caps, cJSON_CreateString("timers"));
    cJSON_AddItemToArray(caps, cJSON_CreateString("tasks"));
    cJSON_AddItemToArray(caps, cJSON_CreateString("fetch"));
    // The apps' icons and the meeting being recorded (docs/protocol.md, Icons, Meetings).
    cJSON_AddItemToArray(caps, cJSON_CreateString("icons"));
    cJSON_AddItemToArray(caps, cJSON_CreateString("meetings"));
    if (ota_available()) {
        // Updates over the link: ota/begin, ota/write, ota/end.
        cJSON_AddItemToArray(caps, cJSON_CreateString("ota"));
    }
    if (voice_available()) {
        cJSON_AddItemToArray(channels, cJSON_CreateString("audio_up"));
        cJSON_AddItemToArray(caps, cJSON_CreateString("voice"));
    }
    if (playback_available()) {
        cJSON_AddItemToArray(channels, cJSON_CreateString("audio_down"));
        cJSON_AddItemToArray(caps, cJSON_CreateString("speaker"));
    }
    if (voice_available() && playback_available()) {
        // Talking without the host, through services it hands over (cloud/set).
        cJSON_AddItemToArray(caps, cJSON_CreateString("cloud"));
    }
    return id;
}

void rpc_init(void)
{
    // Requests and replies are short-lived; keep them out of internal RAM.
    cJSON_Hooks hooks = {.malloc_fn = psram_malloc, .free_fn = free};
    cJSON_InitHooks(&hooks);
}

void rpc_notify(const char *method, cJSON *params)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "jsonrpc", "2.0");
    cJSON_AddStringToObject(msg, "method", method);
    if (params != NULL) {
        cJSON_AddItemToObject(msg, "params", params);
    }
    send_message(msg);
}

void rpc_announce(void)
{
    rpc_notify("ready", identity());
}

void rpc_handle(uint8_t *payload, size_t len)
{
    cJSON *req = cJSON_ParseWithLength((const char *) payload, len);
    if (req == NULL) {
        reply_error(NULL, RPC_PARSE_ERROR, "parse error");
        return;
    }
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(req, "id");
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(req, "method");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(req, "params");
    if (!cJSON_IsObject(req) || !cJSON_IsString(method)) {
        reply_error(id, RPC_INVALID_REQUEST, "invalid request");
        cJSON_Delete(req);
        return;
    }
    // Requests without an id are notifications: run, don't answer.
    const bool answer = id != NULL;
    const char *name = method->valuestring;
    cJSON *result = NULL;
    int code = 0;
    const char *message = NULL;

    if (strcmp(name, "hello") == 0) {
        result = identity();
        // A host reached the board: a firmware just updated works (ota.c).
        ota_confirm();
    } else if (strncmp(name, "ota/", 4) == 0) {
        // Updates over the link (ota.c).
        result = ota_rpc(name, params, &code, &message);
    } else if (strcmp(name, "tools/list") == 0) {
        // Room for the envelope and nextCursor around the tools.
        result = board_tools_list(params, LINK_MAX_PAYLOAD - 128);
    } else if (strcmp(name, "tools/call") == 0) {
        const cJSON *tool = cJSON_GetObjectItemCaseSensitive(params, "name");
        const cJSON *args = cJSON_GetObjectItemCaseSensitive(params, "arguments");
        if (!cJSON_IsString(tool) || (args != NULL && !cJSON_IsObject(args))) {
            code = RPC_INVALID_PARAMS;
            message = "expected {\"name\": string, \"arguments\": object}";
        } else if ((result = board_tools_call(tool->valuestring, args)) == NULL) {
            code = RPC_INVALID_PARAMS;
            message = "unknown tool";
        }
    } else if (strcmp(name, "voice/state") == 0) {
        // The host's voice pipeline says what the eyes show (thinking, speaking, ...).
        const cJSON *state = cJSON_GetObjectItemCaseSensitive(params, "state");
        voice_state_t s;
        if (!cJSON_IsString(state) || !voice_state_from_name(state->valuestring, &s)) {
            code = RPC_INVALID_PARAMS;
            message = "expected {\"state\": \"idle\" | \"listening\" | \"thinking\" | \"speaking\" | \"error\"}";
        } else if (!voice_available()) {
            code = RPC_INVALID_PARAMS;
            message = "voice not available";
        } else {
            voice_set_state(s);
            result = cJSON_CreateObject();
        }
    } else if (strcmp(name, "voice/listen") == 0) {
        // Push-to-talk, or the follow-up after a reply: stream an utterance
        // without the wake word.
        const cJSON *follow_up = cJSON_GetObjectItemCaseSensitive(params, "follow_up");
        const cJSON *wait = cJSON_GetObjectItemCaseSensitive(params, "wait_ms");
        const cJSON *pause = cJSON_GetObjectItemCaseSensitive(params, "pause_ms");
        if (!voice_listen(cJSON_IsTrue(follow_up), cJSON_IsNumber(wait) ? wait->valueint : 0,
                          cJSON_IsNumber(pause) ? pause->valueint : 0)) {
            code = RPC_INVALID_PARAMS;
            message = voice_available() ? "microphone muted" : "voice not available";
        } else {
            result = cJSON_CreateObject();
        }
    } else if (strcmp(name, "voice/pause") == 0) {
        // The host judges when the sentence is finished: the board's own
        // pause only ends it if the host says nothing.
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(params, "id");
        const cJSON *pause = cJSON_GetObjectItemCaseSensitive(params, "pause_ms");
        if (!cJSON_IsNumber(id) || !cJSON_IsNumber(pause)) {
            code = RPC_INVALID_PARAMS;
            message = "expected {\"id\": n, \"pause_ms\": 200..3000}";
        } else if (!voice_set_pause((uint8_t) id->valueint, pause->valueint)) {
            code = RPC_INVALID_PARAMS;
            message = "voice not available";
        } else {
            result = cJSON_CreateObject();
        }
    } else if (strcmp(name, "voice/stop") == 0) {
        voice_stop_listening();
        result = cJSON_CreateObject();
    } else if (strcmp(name, "audio/stop") == 0) {
        // Stop talking: what's buffered is dropped.
        playback_stop();
        result = cJSON_CreateObject();
    } else if (strcmp(name, "cloud/set") == 0) {
        // The services Yoru talks to without the host, with their keys: a
        // method of its own, not a tool, so no model ever sees it. Only
        // USB or the sealed WiFi link of a paired host carry it.
        static char err[96];
        if (cloud_set(params, err, sizeof(err))) {
            result = cloud_status();
        } else {
            code = RPC_INVALID_PARAMS;
            message = err;
        }
    } else if (strcmp(name, "cloud/get") == 0) {
        result = cloud_status();
    } else if (strncmp(name, "wifi/", 5) == 0) {
        // Joining a network, and pairing hosts with the board for it.
        result = net_rpc(name, params, link_rx_transport() == LINK_USB, &code, &message);
    } else if (strcmp(name, "eyes/gaze") == 0) {
        // Where the host's pointer is, -1..1 each way; sent as a notification
        // many times a second while it moves.
        const cJSON *x = cJSON_GetObjectItemCaseSensitive(params, "x");
        const cJSON *y = cJSON_GetObjectItemCaseSensitive(params, "y");
        if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y)) {
            code = RPC_INVALID_PARAMS;
            message = "expected {\"x\": -1..1, \"y\": -1..1}";
        } else {
            behaviour_gaze((float) x->valuedouble, (float) y->valuedouble);
            result = cJSON_CreateObject();
        }
    } else if (strcmp(name, "debug/cloud") == 0) {
        // Ask the services something as if it had been said (tests).
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(params, "text");
        if (!cJSON_IsString(text) || !cloud_ask(text->valuestring)) {
            code = RPC_INVALID_PARAMS;
            message = cloud_ready() ? "expected {\"text\": string}" : "no services or no WiFi";
        } else {
            result = cJSON_CreateObject();
        }
    } else if (strcmp(name, "debug/clips") == 0) {
        // The clips this firmware has, for a host to list them.
        result = cJSON_CreateObject();
        cJSON *list = cJSON_AddArrayToObject(result, "clips");
        for (int i = CLIP_NONE + 1; i < CLIP_COUNT; i++) {
            const clip_t *c = clips_get((clip_id_t) i);
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "name", c->name);
            cJSON_AddNumberToObject(item, "seconds", round((double) (c->n - 1) * 100.0 / 30.0) / 100.0);
            cJSON_AddBoolToObject(item, "loop", c->loop);
            cJSON_AddItemToArray(list, item);
        }
        // And the one playing now, so a test can tell what a call showed.
        lvgl_port_lock();
        const clip_t *now = clips_get(eyes_clip());
        lvgl_port_unlock();
        if (now != NULL) {
            cJSON_AddStringToObject(result, "playing", now->name);
        } else {
            cJSON_AddNullToObject(result, "playing");
        }
    } else if (strcmp(name, "debug/clip") == 0 && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(params, "stop"))) {
        lvgl_port_lock();
        eyes_stop_clip();
        lvgl_port_unlock();
        result = cJSON_CreateObject();
        cJSON_AddBoolToObject(result, "stopped", true);
    } else if (strcmp(name, "debug/clip") == 0) {
        // Play one of the clips keyed in the Rive file (docs/animations.md),
        // to see it on the board.
        const cJSON *clip = cJSON_GetObjectItemCaseSensitive(params, "name");
        const cJSON *mirror = cJSON_GetObjectItemCaseSensitive(params, "mirror");
        const cJSON *source = cJSON_GetObjectItemCaseSensitive(params, "source");
        const clip_id_t id = cJSON_IsString(clip) ? clips_find(clip->valuestring) : CLIP_NONE;
        if (id == CLIP_NONE || (mirror != NULL && !cJSON_IsBool(mirror)) || (source != NULL && !cJSON_IsString(source))) {
            code = RPC_INVALID_PARAMS;
            message = "expected {\"name\": a clip in docs/animations.md, \"mirror\"?: bool, \"source\"?: string}";
        } else {
            lvgl_port_lock();
            if (cJSON_IsString(source)) {
                // In a service's colour, with its glyph on the prop.
                uint32_t rgb;
                const char *glyph;
                errand_look(source->valuestring, &rgb, &glyph);
                eyes_play_fx(id, cJSON_IsTrue(mirror), rgb, glyph);
            } else {
                eyes_play(id, cJSON_IsTrue(mirror));
            }
            lvgl_port_unlock();
            result = cJSON_CreateObject();
            cJSON_AddStringToObject(result, "playing", clip->valuestring);
        }
    } else if (strcmp(name, "debug/perf") == 0) {
        // How hard the board works and the audio's gaps; {"reset": true}
        // starts the worst-of afresh, to measure one thing at a time.
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(params, "reset"))) {
            perf_reset();
        }
        result = perf_report();
    } else if (strcmp(name, "debug/audio") == 0) {
        const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(params, "cmd");
        if (!cJSON_IsString(cmd)) {
            code = RPC_INVALID_PARAMS;
            message = "expected {\"cmd\": string}";
        } else {
            audio_selftest_command(cmd->valuestring);
            result = cJSON_CreateObject();
        }
    } else {
        code = RPC_METHOD_NOT_FOUND;
        message = "method not found";
    }

    if (!answer) {
        cJSON_Delete(result);
    } else if (result != NULL) {
        reply_result(id, result);
    } else {
        reply_error(id, code, message);
    }
    cJSON_Delete(req);
}
