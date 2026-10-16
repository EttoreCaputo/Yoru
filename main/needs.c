#include "needs.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

static const char *TAG = "needs";

#define NVS_NAMESPACE "pet"
#define NEEDS_KEY "needs"
/* Saved at most this often, when one moved at least SAVE_STEP. */
#define SAVE_EVERY_S 600
#define SAVE_STEP 0.02f
/* Awake at night, rest runs down this much faster. */
#define NIGHT_TIRING 1.6f
/* Someone around fills company slowly, music fills fun, both per hour. */
#define COMPANY_NEAR_H 0.12f
#define MUSIC_FUN_H 0.5f

/* How each need moves per hour, by what Yoru is doing: a desk day of
 * fourteen hours wants a nap or two, three or four snacks, some play and
 * some exploring, and you. */
static const float RATE_H[4][NEED_COUNT] = {
    //               rest    food    fun     company curiosity
    [NEEDS_CALM] = {-0.08f, -0.10f, -0.25f, -0.20f, -0.30f},
    [NEEDS_BUSY] = {-0.15f, -0.15f, -0.10f, -0.20f, -0.05f},
    [NEEDS_PLAY] = {-0.25f, -0.16f, 0.60f, -0.10f, -0.10f},
    [NEEDS_ASLEEP] = {0.60f, -0.03f, 0, 0, 0},
};

/* How much each counts for how well it feels. */
static const float WEIGHT[NEED_COUNT] = {0.2f, 0.3f, 0.2f, 0.2f, 0.1f};

static const char *const NAMES[NEED_COUNT] = {"rest", "food", "fun", "company", "curiosity"};

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static float s_level[NEED_COUNT] = {0.8f, 0.7f, 0.6f, 0.6f, 0.5f};
static float s_saved[NEED_COUNT];
static int64_t s_saved_us;
static volatile needs_doing_t s_doing = NEEDS_CALM;

static float clamp01(float v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

void needs_init(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t raw[NEED_COUNT];
        size_t len = sizeof(raw);
        if (nvs_get_blob(nvs, NEEDS_KEY, raw, &len) == ESP_OK && len == sizeof(raw)) {
            for (int i = 0; i < NEED_COUNT; i++) {
                s_level[i] = raw[i] / 255.0f;
            }
        }
        nvs_close(nvs);
    }
    memcpy(s_saved, s_level, sizeof(s_saved));
    s_saved_us = esp_timer_get_time();
    ESP_LOGI(TAG, "rest %.2f, food %.2f, fun %.2f, company %.2f, curiosity %.2f", s_level[0], s_level[1],
             s_level[2], s_level[3], s_level[4]);
}

static void save(const float level[NEED_COUNT])
{
    uint8_t raw[NEED_COUNT];
    for (int i = 0; i < NEED_COUNT; i++) {
        raw[i] = (uint8_t) (level[i] * 255.0f + 0.5f);
    }
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    esp_err_t err = nvs_set_blob(nvs, NEEDS_KEY, raw, sizeof(raw));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "saving the needs failed: %s", esp_err_to_name(err));
    }
}

void needs_tick(float dt, bool company, bool night, bool music)
{
    const needs_doing_t doing = s_doing;
    const bool awake = doing != NEEDS_ASLEEP;
    const float h = dt / 3600.0f;
    float rate[NEED_COUNT];
    memcpy(rate, RATE_H[doing], sizeof(rate));
    if (awake && night && rate[NEED_REST] < 0) {
        rate[NEED_REST] *= NIGHT_TIRING;
    }
    if (awake && company) {
        rate[NEED_COMPANY] = COMPANY_NEAR_H;
    }
    if (awake && music) {
        rate[NEED_FUN] += MUSIC_FUN_H;
    }

    bool due = false;
    float level[NEED_COUNT];
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < NEED_COUNT; i++) {
        s_level[i] = clamp01(s_level[i] + rate[i] * h);
    }
    if (now - s_saved_us >= (int64_t) SAVE_EVERY_S * 1000000) {
        for (int i = 0; i < NEED_COUNT; i++) {
            due = due || fabsf(s_level[i] - s_saved[i]) >= SAVE_STEP;
        }
        if (due) {
            memcpy(s_saved, s_level, sizeof(s_saved));
            s_saved_us = now;
        }
    }
    memcpy(level, s_level, sizeof(level));
    portEXIT_CRITICAL(&s_mux);
    if (due) {
        save(level);
    }
}

void needs_doing(needs_doing_t doing)
{
    s_doing = doing;
}

needs_doing_t needs_doing_now(void)
{
    return s_doing;
}

void needs_meet(need_t need, float amount)
{
    if (need >= NEED_COUNT) {
        return;
    }
    portENTER_CRITICAL(&s_mux);
    s_level[need] = clamp01(s_level[need] + amount);
    portEXIT_CRITICAL(&s_mux);
}

float needs_get(need_t need)
{
    return need < NEED_COUNT ? s_level[need] : 1;
}

void needs_get_all(needs_t *out)
{
    portENTER_CRITICAL(&s_mux);
    memcpy(out->level, s_level, sizeof(out->level));
    portEXIT_CRITICAL(&s_mux);
}

float needs_wellbeing(void)
{
    needs_t n;
    needs_get_all(&n);
    float w = 0;
    for (int i = 0; i < NEED_COUNT; i++) {
        w += WEIGHT[i] * n.level[i];
    }
    return w;
}

const char *needs_name(need_t need)
{
    return need < NEED_COUNT ? NAMES[need] : "?";
}

need_t needs_worst(float *urgency)
{
    needs_t n;
    needs_get_all(&n);
    need_t worst = NEED_REST;
    for (int i = 1; i < NEED_COUNT; i++) {
        if (n.level[i] < n.level[worst]) {
            worst = (need_t) i;
        }
    }
    if (urgency != NULL) {
        *urgency = 1 - n.level[worst];
    }
    return worst;
}
