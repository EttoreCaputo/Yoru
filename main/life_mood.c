/* Yoru's emotions, after Vector's mood manager: five dimensions from -1 to
 * 1, which what happens pushes (a table of affectors per event) and which
 * drift back to a baseline along a curve of their own. Unlike Vector's,
 * the baselines move: energy follows the hour and how long it has been
 * awake, company follows how long it has been alone, cheer follows trust. */

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "life_int.h"
#include "nvs.h"

static const char *TAG = "mood";

#define NVS_NS "life"
#define NVS_TRUST "trust"
/* Trust is written when it moved this much, at most this often. */
#define TRUST_SAVE_DELTA 0.01f
#define TRUST_SAVE_EVERY_MS (10 * 60 * 1000)
/* Time together builds it, slowly: this much an hour with someone around. */
#define TRUST_PER_HOUR 0.004f
/* Someone touched, talked or used the computer this recently: company. */
#define WITH_MS 60000
/* Alone this long, it's as lonely as it gets. */
#define LONELY_FULL_MS (45 * 60 * 1000)
/* Each hour awake takes this much energy, up to FATIGUE_MAX. */
#define FATIGUE_PER_HOUR 0.06f
#define FATIGUE_MAX 0.4f

/* How a push fades: (seconds since it, share of it left), Vector's
 * TimeRatio graphs. */
typedef struct {
    float s, k;
} node_t;

static const node_t DECAY_HAPPY[] = {{0, 1}, {60, 1}, {300, 0.5f}, {900, 0}};
static const node_t DECAY_ENERGY[] = {{0, 1}, {300, 0.6f}, {1200, 0}};
static const node_t DECAY_SOCIAL[] = {{0, 1}, {30, 1}, {120, 0.6f}, {600, 0}};
static const node_t DECAY_STIM[] = {{0, 1}, {10, 1}, {40, 0.6f}, {120, 0.2f}, {240, 0}};

static const struct {
    const node_t *nodes;
    int n;
} DECAY[EMO_COUNT] = {
    [EMO_HAPPY] = {DECAY_HAPPY, 4},
    [EMO_ENERGY] = {DECAY_ENERGY, 3},
    [EMO_SOCIAL] = {DECAY_SOCIAL, 4},
    [EMO_STIM] = {DECAY_STIM, 5},
};

static const char *const EMO_NAMES[EMO_COUNT] = {"happy", "energy", "social", "stim", "trust"};

/* What each event does to each emotion (Vector's emotion events). */
static const struct {
    const char *name;
    float d[EMO_COUNT];
} FEELS[LIFE_FEEL_COUNT] = {
    //                         happy  energy social stim   trust
    [LIFE_FEEL_POKE] = {"poke", {0.10f, 0, 0.30f, 0.25f, 0}},
    [LIFE_FEEL_STROKE] = {"stroke", {0.35f, 0, 0.50f, 0.10f, 0.010f}},
    [LIFE_FEEL_HOLD] = {"hold", {0.50f, 0, 0.70f, 0.10f, 0.020f}},
    [LIFE_FEEL_TALK] = {"talk", {0.20f, 0.05f, 0.60f, 0.30f, 0.005f}},
    [LIFE_FEEL_SLAP] = {"slap", {-0.20f, 0, 0.10f, 0.40f, -0.010f}},
    [LIFE_FEEL_SHAKE] = {"shake", {-0.30f, -0.10f, 0, 0.60f, -0.020f}},
    [LIFE_FEEL_DROP] = {"drop", {-0.50f, 0, 0, 0.80f, -0.030f}},
    [LIFE_FEEL_LIFT] = {"lift", {0.10f, 0, 0.30f, 0.30f, 0}},
    [LIFE_FEEL_PLAYED] = {"played", {0.50f, -0.10f, 0.40f, 0.60f, 0.005f}},
    [LIFE_FEEL_TIMER] = {"timer", {0, 0, 0, 0.20f, 0}},
    [LIFE_FEEL_TASK] = {"task", {0.30f, 0, 0.20f, 0.10f, 0}},
    [LIFE_FEEL_WOKE] = {"woke", {0.10f, 0.50f, 0, -0.20f, 0}},
    [LIFE_FEEL_GREETED] = {"greeted", {0.40f, 0.10f, 0.60f, 0.30f, 0.005f}},
    [LIFE_FEEL_IGNORED] = {"ignored", {-0.30f, 0, -0.40f, -0.10f, -0.005f}},
    [LIFE_FEEL_FUN] = {"fun", {0.15f, -0.05f, 0, 0.20f, 0}},
    [LIFE_FEEL_SOUND] = {"sound", {0, 0, 0, 0.20f, 0}},
    [LIFE_FEEL_PLUGGED] = {"plugged", {0.10f, 0.20f, 0, 0.30f, 0}},
};

/* Energy through the day, hour by hour: sleepy at night, a dip after
 * lunch. */
static const float ENERGY_BY_HOUR[24] = {
    -0.7f, -0.75f, -0.8f, -0.8f, -0.75f, -0.6f, -0.3f, 0.1f, 0.35f, 0.4f, 0.4f, 0.35f,
    0.2f,  0.05f,  0.0f,  0.2f,  0.3f,   0.3f,  0.25f, 0.15f, 0.05f, -0.1f, -0.3f, -0.5f,
};

static struct {
    /* Each emotion is base + (v0 - base) * decay(since t0). */
    float v0[EMO_COUNT];
    uint32_t t0[EMO_COUNT];
    float base[EMO_COUNT];
    float trust;
    float trust_saved;
    uint32_t trust_saved_ms;
    uint32_t last_ms;
    uint32_t woke_ms;
} M;

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static float decay(emo_t e, uint32_t ms)
{
    const node_t *n = DECAY[e].nodes;
    const float s = (float) ms / 1000.0f;
    for (int i = 1; i < DECAY[e].n; i++) {
        if (s <= n[i].s) {
            const float k = (s - n[i - 1].s) / (n[i].s - n[i - 1].s);
            return n[i - 1].k + (n[i].k - n[i - 1].k) * k;
        }
    }
    return n[DECAY[e].n - 1].k;
}

float mood(emo_t e)
{
    if (e == EMO_TRUST) {
        return M.trust;
    }
    if (e >= EMO_COUNT) {
        return 0;
    }
    return clampf(M.base[e] + (M.v0[e] - M.base[e]) * decay(e, LT.vnow - M.t0[e]), -1, 1);
}

const char *mood_name(emo_t e)
{
    return e < EMO_COUNT ? EMO_NAMES[e] : "?";
}

const char *mood_feel_name(life_feel_t feel)
{
    return feel < LIFE_FEEL_COUNT ? FEELS[feel].name : "?";
}

static void trust_load(void)
{
    nvs_handle_t nvs;
    int16_t v = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_i16(nvs, NVS_TRUST, &v);
        nvs_close(nvs);
    }
    M.trust = M.trust_saved = clampf((float) v / 1000.0f, -1, 1);
}

static void trust_save(void)
{
    if (fabsf(M.trust - M.trust_saved) < TRUST_SAVE_DELTA || LT.now - M.trust_saved_ms < TRUST_SAVE_EVERY_MS) {
        return;
    }
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_i16(nvs, NVS_TRUST, (int16_t) lroundf(M.trust * 1000));
        nvs_commit(nvs);
        nvs_close(nvs);
        M.trust_saved = M.trust;
        M.trust_saved_ms = LT.now;
        ESP_LOGI(TAG, "trust %.3f kept", M.trust);
    }
}

/* Where each emotion settles now. */
static void baselines(void)
{
    const life_ctx_t *c = &LT.ctx;
    const float hour = c->minute >= 0 ? ENERGY_BY_HOUR[(c->minute / 60) % 24] : 0.2f;
    const float awake_h = (float) (LT.vnow - M.woke_ms) / 3600000.0f;
    float energy = hour - fminf(FATIGUE_MAX, awake_h * FATIGUE_PER_HOUR);
    if (c->drowsy) {
        energy -= 0.35f; // alone a long while, or the battery low
    }
    M.base[EMO_ENERGY] = clampf(energy, -1, 1);
    // Alone it gets lonelier and lonelier.
    const float alone = c->alone_ms < WITH_MS ? 0 : fminf(1, (float) (c->alone_ms - WITH_MS) / LONELY_FULL_MS);
    M.base[EMO_SOCIAL] = c->alone_ms < WITH_MS ? 0.15f : -0.85f * alone;
    // Happy-go-lucky by default, more so the more it trusts you.
    M.base[EMO_HAPPY] = 0.15f + 0.25f * M.trust - 0.2f * alone;
    M.base[EMO_STIM] = life_night() ? -0.2f : 0;
}

void mood_init(void)
{
    memset(&M, 0, sizeof(M));
    trust_load();
    M.trust_saved_ms = LT.now;
    M.woke_ms = LT.vnow;
    M.last_ms = LT.vnow;
    baselines();
    for (int e = 0; e < EMO_TRUST; e++) {
        M.v0[e] = M.base[e];
        M.t0[e] = LT.vnow;
    }
}

void mood_step(void)
{
    const uint32_t dt = LT.vnow - M.last_ms;
    M.last_ms = LT.vnow;
    if (LT.ctx.asleep) {
        M.woke_ms = LT.vnow; // sleep is rest: awake time starts over
    } else if (LT.ctx.alone_ms < WITH_MS) {
        M.trust = clampf(M.trust + TRUST_PER_HOUR * (float) dt / 3600000.0f, -1, 1);
    }
    baselines();
    trust_save();
}

void mood_feel(life_feel_t feel)
{
    if (feel >= LIFE_FEEL_COUNT) {
        return;
    }
    const float *d = FEELS[feel].d;
    for (int e = 0; e < EMO_TRUST; e++) {
        if (d[e] != 0) {
            M.v0[e] = clampf(mood((emo_t) e) + d[e], -1, 1);
            M.t0[e] = LT.vnow;
        }
    }
    M.trust = clampf(M.trust + d[EMO_TRUST], -1, 1);
    ESP_LOGI(TAG, "%s: happy %.2f energy %.2f social %.2f stim %.2f trust %.3f", FEELS[feel].name, mood(EMO_HAPPY),
             mood(EMO_ENERGY), mood(EMO_SOCIAL), mood(EMO_STIM), M.trust);
}

void mood_status(cJSON *out)
{
    for (int e = 0; e < EMO_COUNT; e++) {
        cJSON_AddNumberToObject(out, EMO_NAMES[e], lroundf(mood((emo_t) e) * 100) / 100.0);
    }
}
