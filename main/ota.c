#include "ota.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "lvgl_port.h"
#include "mbedtls/base64.h"
#include "psa/crypto.h"
#include "toast.h"

static const char *TAG = "ota";

#define INVALID_PARAMS -32602
#define FAILED -32603

/* Raw bytes per `ota/write`: base64 of it and the JSON around fit one frame
 * (LINK_MAX_PAYLOAD, 4 KB) with room to spare. */
#define CHUNK 2048
/* With no host saying hello this long after an update, back to the firmware
 * before. */
#define PROBATION_US (5 * 60 * 1000000LL)
/* Long enough for the reply to `ota/end` to leave. */
#define RESTART_DELAY_US (800 * 1000)
/* Where esp_app_desc_t's project_name sits in an image: after the 24-byte
 * image header, the 8-byte segment header and 48 bytes into the desc. */
#define PROJECT_AT (24 + 8 + 48)
#define PROJECT "yoru"

static struct {
    bool active;
    esp_ota_handle_t handle;
    const esp_partition_t *slot;
    size_t size;
    size_t written;
    uint8_t sha[32];
    psa_hash_operation_t hash;
    uint8_t *buf; // CHUNK bytes, in PSRAM
    char version[32];
} s;

static esp_timer_handle_t s_probation;

static void restart_cb(void *arg)
{
    esp_restart();
}

static void probation_cb(void *arg)
{
    ESP_LOGW(TAG, "no WiFi and no host after the update: back to the firmware before");
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

void ota_init(void)
{
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "new firmware on probation until the WiFi or a host");
        const esp_timer_create_args_t args = {.callback = probation_cb, .name = "ota_probation"};
        if (esp_timer_create(&args, &s_probation) == ESP_OK) {
            esp_timer_start_once(s_probation, PROBATION_US);
        }
    }
}

bool ota_available(void)
{
    return esp_ota_get_next_update_partition(NULL) != NULL;
}

void ota_confirm(void)
{
    // The WiFi's event task and the link task may both get here.
    static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
    esp_timer_handle_t t = s_probation;
    s_probation = NULL;
    portEXIT_CRITICAL(&mux);
    if (t == NULL) {
        return;
    }
    esp_timer_stop(t);
    esp_timer_delete(t);
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "new firmware kept");
}

static void toast(const char *text, toast_tone_t tone)
{
    lvgl_port_lock();
    toast_show(text, 6, tone);
    lvgl_port_unlock();
}

static void stop(void)
{
    if (s.active) {
        esp_ota_abort(s.handle);
        psa_hash_abort(&s.hash);
    }
    s.active = false;
}

static bool hex32(const char *hex, uint8_t out[32])
{
    if (hex == NULL || strlen(hex) != 64) {
        return false;
    }
    for (int i = 0; i < 32; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1) {
            return false;
        }
        out[i] = (uint8_t) v;
    }
    return true;
}

static cJSON *begin(const cJSON *params, int *code, const char **message)
{
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(params, "size");
    const cJSON *sha = cJSON_GetObjectItemCaseSensitive(params, "sha256");
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(params, "version");
    uint8_t digest[32];
    if (!cJSON_IsNumber(size) || size->valuedouble < 1024 || !cJSON_IsString(sha) || !hex32(sha->valuestring, digest)) {
        *code = INVALID_PARAMS;
        *message = "expected {\"size\": bytes, \"sha256\": hex, \"version\"?}";
        return NULL;
    }
    stop(); // a new begin drops one left half done
    const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);
    if (slot == NULL) {
        *code = FAILED;
        *message = "one app slot only: flash this firmware over USB once";
        return NULL;
    }
    if ((size_t) size->valuedouble > slot->size) {
        *code = INVALID_PARAMS;
        *message = "image larger than the app slot";
        return NULL;
    }
    if (s.buf == NULL) {
        s.buf = heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    // Sequential writes erase as they go: no ten-second erase up front.
    esp_err_t err = s.buf == NULL ? ESP_ERR_NO_MEM : esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &s.handle);
    if (err != ESP_OK) {
        *code = FAILED;
        *message = esp_err_to_name(err);
        return NULL;
    }
    s.active = true;
    s.slot = slot;
    s.size = (size_t) size->valuedouble;
    s.written = 0;
    memcpy(s.sha, digest, sizeof(digest));
    strlcpy(s.version, cJSON_IsString(version) ? version->valuestring : "", sizeof(s.version));
    // PSA is set up at boot (IDF's mbedtls port).
    s.hash = psa_hash_operation_init();
    psa_hash_setup(&s.hash, PSA_ALG_SHA_256);
    ESP_LOGI(TAG, "update %s: %u bytes into %s", s.version, (unsigned) s.size, slot->label);
    toast("Updating…", TOAST_WAIT);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "slot", slot->label);
    cJSON_AddNumberToObject(r, "chunk", CHUNK);
    return r;
}

static cJSON *write_chunk(const cJSON *params, int *code, const char **message)
{
    const cJSON *offset = cJSON_GetObjectItemCaseSensitive(params, "offset");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(params, "data");
    if (!s.active) {
        *code = FAILED;
        *message = "no update under way: ota/begin first";
        return NULL;
    }
    if (!cJSON_IsNumber(offset) || !cJSON_IsString(data)) {
        *code = INVALID_PARAMS;
        *message = "expected {\"offset\": bytes, \"data\": base64}";
        return NULL;
    }
    if ((size_t) offset->valuedouble != s.written) {
        // A chunk sent again after a lost reply: say where it stands.
        *code = INVALID_PARAMS;
        *message = "out of order: ota/write at the offset written";
        return NULL;
    }
    size_t n = 0;
    const char *b64 = data->valuestring;
    if (mbedtls_base64_decode(s.buf, CHUNK, &n, (const unsigned char *) b64, strlen(b64)) != 0 || n == 0
        || s.written + n > s.size) {
        *code = INVALID_PARAMS;
        *message = "bad chunk";
        return NULL;
    }
    // Before a byte reaches the flash: only Yoru's firmware is taken.
    if (s.written <= PROJECT_AT && s.written + n >= PROJECT_AT + sizeof(PROJECT)) {
        if (memcmp(s.buf + (PROJECT_AT - s.written), PROJECT, sizeof(PROJECT)) != 0) {
            stop();
            *code = INVALID_PARAMS;
            *message = "not Yoru's firmware";
            return NULL;
        }
    }
    esp_err_t err = esp_ota_write(s.handle, s.buf, n);
    if (err != ESP_OK) {
        stop();
        *code = FAILED;
        *message = esp_err_to_name(err);
        return NULL;
    }
    psa_hash_update(&s.hash, s.buf, n);
    s.written += n;
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "written", (double) s.written);
    return r;
}

static cJSON *end(int *code, const char **message)
{
    if (!s.active || s.written != s.size) {
        *code = FAILED;
        *message = s.active ? "image not all written" : "no update under way";
        return NULL;
    }
    uint8_t digest[32];
    size_t digest_len = 0;
    const bool hashed = psa_hash_finish(&s.hash, digest, sizeof(digest), &digest_len) == PSA_SUCCESS;
    if (!hashed || memcmp(digest, s.sha, sizeof(digest)) != 0) {
        stop();
        *code = INVALID_PARAMS;
        *message = "SHA-256 doesn't match: the image came damaged";
        toast("Update failed", TOAST_ERROR);
        return NULL;
    }
    s.active = false;
    // esp_ota_end checks the image itself (its checksum and hash).
    esp_err_t err = esp_ota_end(s.handle);
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(s.slot);
    }
    if (err != ESP_OK) {
        *code = FAILED;
        *message = esp_err_to_name(err);
        toast("Update failed", TOAST_ERROR);
        return NULL;
    }
    ESP_LOGI(TAG, "update %s written, restarting", s.version);
    toast("Updated: restarting", TOAST_DONE);
    esp_timer_handle_t t;
    const esp_timer_create_args_t args = {.callback = restart_cb, .name = "ota_restart"};
    if (esp_timer_create(&args, &t) == ESP_OK) {
        esp_timer_start_once(t, RESTART_DELAY_US);
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "restarting", true);
    return r;
}

cJSON *ota_rpc(const char *method, const cJSON *params, int *code, const char **message)
{
    if (strcmp(method, "ota/begin") == 0) {
        return begin(params, code, message);
    }
    if (strcmp(method, "ota/write") == 0) {
        return write_chunk(params, code, message);
    }
    if (strcmp(method, "ota/end") == 0) {
        return end(code, message);
    }
    if (strcmp(method, "ota/abort") == 0) {
        stop();
        return cJSON_CreateObject();
    }
    if (strcmp(method, "ota/status") == 0) {
        cJSON *r = cJSON_CreateObject();
        const esp_partition_t *running = esp_ota_get_running_partition();
        cJSON_AddStringToObject(r, "running", running != NULL ? running->label : "?");
        cJSON_AddBoolToObject(r, "available", ota_available());
        cJSON_AddBoolToObject(r, "probation", s_probation != NULL);
        cJSON_AddBoolToObject(r, "active", s.active);
        cJSON_AddNumberToObject(r, "written", (double) s.written);
        return r;
    }
    *code = -32601;
    *message = "method not found";
    return NULL;
}
