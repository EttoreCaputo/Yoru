#include "pet.h"

#include <math.h>
#include <string.h>

#include "ears.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "pet";

#define NVS_NAMESPACE "pet"
#define MOOD_KEY "mood"
/* Saved at most this often, when it moved at least SAVE_STEP. */
#define SAVE_EVERY_S 600
#define SAVE_STEP 0.02f

#define TICK_US 1000000
/* How fast each drifts back to where it rests, in seconds. */
#define ENERGY_TAU_S 1200.0f
#define HAPPY_TAU_S 1800.0f
#define AFFECTION_TAU_S 21600.0f
#define HAPPY_REST 0.6f
#define AFFECTION_REST 0.35f

/* Nobody at the computer after this long without input. */
#define AWAY_S 900
/* Back after this long: really glad. */
#define LONG_AWAY_S (3 * 3600)
/* No conversation, no scene asked for, no music: bored after this long. */
#define BORED_S 3600

/* Hot from HOT_C for HOT_FOR_S; cool again under COOL_C for COOL_FOR_S. */
#define HOT_C 88.0f
#define HOT_FOR_S 15
#define COOL_C 75.0f
#define COOL_FOR_S 20
/* Music playing this long before it dances; again every DANCE_EVERY_S. */
#define MUSIC_FOR_S 8
#define DANCE_EVERY_S 300
/* Claude worked at least this long: proud when it's done. */
#define CLAUDE_WORKED_S 45
/* Claude starting after this long off: the pet gets focused too. */
#define CLAUDE_REST_S 600
#define BATTERY_LOW_PCT 15.0f
/* A reaction behaviour.c doesn't take (asleep, focus mode) is dropped after this. */
#define PENDING_S 20
/* Three errors this close: sad. */
#define ERRORS_SAD 3
#define ERRORS_WINDOW_S 300
/* Three bangs this close: it's had enough of the noise. */
#define BANGS_NOISY 3
#define BANGS_WINDOW_S 300
/* Claps in a row for a dance rather than a smile. */
#define CLAPS_DANCE 3
/* A room this quiet for this long makes it drowsy. */
#define QUIET_DROWSY_S 1800
#define QUIET_ENERGY 0.12f

typedef enum {
    R_HOT,
    R_COOL,
    R_MUSIC,
    R_CLAUDE_DONE,
    R_CLAUDE_START,
    R_BATTERY_LOW,
    R_CHARGING,
    R_GOODNIGHT,
    R_GREETING,
    R_SAD,
    R_BANG,
    R_CLAPS,
    R_NOISY,
    R_COUNT,
} reaction_t;

/* Between two of the same, at least. */
static const uint16_t COOLDOWN_S[R_COUNT] = {
    [R_HOT] = 900,       [R_COOL] = 300,       [R_MUSIC] = 240,        [R_CLAUDE_DONE] = 300,
    [R_CLAUDE_START] = 1200, [R_BATTERY_LOW] = 1200, [R_CHARGING] = 120, [R_GOODNIGHT] = 3600,
    [R_GREETING] = 600,  [R_SAD] = 600,         [R_BANG] = 30,          [R_CLAPS] = 8,
    [R_NOISY] = 900,
};

static const char *const REACTION_NAMES[R_COUNT] = {
    [R_HOT] = "hot",
    [R_COOL] = "cool",
    [R_MUSIC] = "music",
    [R_CLAUDE_DONE] = "claude_done",
    [R_CLAUDE_START] = "claude_start",
    [R_BATTERY_LOW] = "battery_low",
    [R_CHARGING] = "charging",
    [R_GOODNIGHT] = "goodnight",
    [R_GREETING] = "greeting",
    [R_SAD] = "errors",
    [R_BANG] = "bang",
    [R_CLAPS] = "claps",
    [R_NOISY] = "noisy",
};

static const char *const MOOD_NAMES[PET_MOOD_COUNT] = {
    [PET_MOOD_CONTENT] = "content", [PET_MOOD_HAPPY] = "happy",   [PET_MOOD_EXCITED] = "excited",
    [PET_MOOD_LOVING] = "loving",   [PET_MOOD_BORED] = "bored",   [PET_MOOD_GRUMPY] = "grumpy",
    [PET_MOOD_SAD] = "sad",         [PET_MOOD_SLEEPY] = "sleepy", [PET_MOOD_HOT] = "hot",
};

/* The whims each mood has; behaviour.c knows how each looks. */
static const char *const POOL_CONTENT[] = {"look_around", "curious", "thinking", "wink", "blink"};
static const char *const POOL_HAPPY[] = {"happy", "wink", "curious", "look_around"};
static const char *const POOL_EXCITED[] = {"excited", "happy", "surprised"};
static const char *const POOL_LOVING[] = {"love", "happy", "wink"};
static const char *const POOL_BORED[] = {"bored", "sigh", "yawn", "look_around"};
static const char *const POOL_GRUMPY[] = {"angry", "sigh", "curious"};
static const char *const POOL_SAD[] = {"sad", "sigh", "thinking"};
static const char *const POOL_SLEEPY[] = {"yawn", "sleepy", "sigh"};
static const char *const POOL_HOT[] = {"hot", "sigh"};

typedef struct {
    const char *const *names;
    uint8_t count;
} pool_t;

#define POOL(p) {p, sizeof(p) / sizeof(p[0])}
static const pool_t POOLS[PET_MOOD_COUNT] = {
    [PET_MOOD_CONTENT] = POOL(POOL_CONTENT), [PET_MOOD_HAPPY] = POOL(POOL_HAPPY), [PET_MOOD_EXCITED] = POOL(POOL_EXCITED),
    [PET_MOOD_LOVING] = POOL(POOL_LOVING),   [PET_MOOD_BORED] = POOL(POOL_BORED), [PET_MOOD_GRUMPY] = POOL(POOL_GRUMPY),
    [PET_MOOD_SAD] = POOL(POOL_SAD),         [PET_MOOD_SLEEPY] = POOL(POOL_SLEEPY), [PET_MOOD_HOT] = POOL(POOL_HOT),
};

static SemaphoreHandle_t s_lock;
static float s_energy = 0.7f;
static float s_happy = HAPPY_REST;
static float s_affection = 0.5f;
static float s_saved[3];
static int64_t s_saved_us;
static volatile bool s_react = true;
static volatile pet_mood_t s_mood = PET_MOOD_CONTENT;
static volatile bool s_away;
static volatile int s_minute = -1;

static int64_t s_tick_us;
static int64_t s_interacted_us;
static int64_t s_away_since_us;
static int64_t s_last_us[R_COUNT];
/* What was seen last tick, to react to changes. */
static bool s_seen;
static bool s_hot;
static int s_hot_s, s_cool_s;
static int s_music_s;
static int64_t s_danced_us;
static metrics_claude_state_t s_claude;
static int64_t s_claude_since_us;
static int64_t s_claude_off_us;
static bool s_plugged;
static bool s_battery_low;
static bool s_night_said;
static int s_errors;
static int64_t s_first_error_us;
static int s_bangs;
static int64_t s_first_bang_us;
static bool s_claude_working;
static float s_pitch = 1.0f;
static int s_idle_s = -1;
/* The last reactions, a ring: the newest just before s_recent_next. */
static struct {
    reaction_t r;
    const char *scene;
    int64_t us;
} s_recent[PET_RECENT];
static int s_recent_next, s_recent_count;
/* A reaction waiting for the eyes, until `s_pending_until_us`. */
static const char *s_pending;
static int64_t s_pending_until_us;

static float clamp01(float v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

static void load(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    uint8_t mood[3];
    size_t len = sizeof(mood);
    if (nvs_get_blob(nvs, MOOD_KEY, mood, &len) == ESP_OK && len == sizeof(mood)) {
        s_energy = mood[0] / 255.0f;
        s_happy = mood[1] / 255.0f;
        s_affection = mood[2] / 255.0f;
    }
    nvs_close(nvs);
}

static void save(void)
{
    uint8_t mood[3] = {(uint8_t) (s_energy * 255.0f + 0.5f), (uint8_t) (s_happy * 255.0f + 0.5f),
                       (uint8_t) (s_affection * 255.0f + 0.5f)};
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    esp_err_t err = nvs_set_blob(nvs, MOOD_KEY, mood, sizeof(mood));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "saving the mood failed: %s", esp_err_to_name(err));
    }
}

void pet_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    load();
    s_saved[0] = s_energy;
    s_saved[1] = s_happy;
    s_saved[2] = s_affection;
    int64_t now = esp_timer_get_time();
    s_saved_us = now;
    s_interacted_us = now;
    ESP_LOGI(TAG, "energy %.2f, happiness %.2f, affection %.2f", s_energy, s_happy, s_affection);
}

static void react(reaction_t r, const char *scene, int64_t now);

/** With s_lock held. */
static void nudge(float energy, float happy, float affection)
{
    s_energy = clamp01(s_energy + energy);
    s_happy = clamp01(s_happy + happy);
    s_affection = clamp01(s_affection + affection);
}

void pet_event(pet_event_t event)
{
    if (s_lock == NULL) {
        return;
    }
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    switch (event) {
    case PET_EVENT_TALKED:
        nudge(0.04f, 0.05f, 0.04f);
        s_interacted_us = now;
        break;
    case PET_EVENT_ERROR:
        nudge(0, -0.06f, 0);
        if (s_errors == 0 || now - s_first_error_us > (int64_t) ERRORS_WINDOW_S * 1000000) {
            s_errors = 0;
            s_first_error_us = now;
        }
        s_errors++;
        break;
    case PET_EVENT_PLAYED:
        nudge(0.01f, 0.02f, 0.02f);
        s_interacted_us = now;
        break;
    case PET_EVENT_POKED:
        nudge(0.02f, 0.01f, 0.01f);
        s_interacted_us = now;
        break;
    case PET_EVENT_STROKED:
        nudge(0, 0.04f, 0.05f);
        s_interacted_us = now;
        break;
    case PET_EVENT_ANNOYED:
        nudge(0.01f, -0.05f, -0.01f);
        s_interacted_us = now;
        break;
    }
    xSemaphoreGive(s_lock);
}

void pet_heard(pet_heard_t what, int count)
{
    if (s_lock == NULL) {
        return;
    }
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    // The computer's speakers, more likely than the room: not for a reaction.
    bool music = s_music_s > 0;
    switch (what) {
    case PET_HEARD_BANG:
        if (music) {
            break;
        }
        nudge(0.06f, -0.03f, 0);
        if (s_bangs == 0 || now - s_first_bang_us > (int64_t) BANGS_WINDOW_S * 1000000) {
            s_bangs = 0;
            s_first_bang_us = now;
        }
        if (++s_bangs >= BANGS_NOISY) {
            s_bangs = 0;
            nudge(0, -0.06f, 0);
            react(R_NOISY, "angry", now);
        } else {
            react(R_BANG, s_energy < 0.35f || s_mood == PET_MOOD_SAD ? "scared" : "surprised", now);
        }
        break;
    case PET_HEARD_CLAPS:
        if (music) {
            break;
        }
        // Someone's calling it.
        nudge(0.05f, 0.04f, 0.03f);
        s_interacted_us = now;
        react(R_CLAPS,
              count >= CLAPS_DANCE && s_energy > 0.3f ? "dance"
              : s_energy < 0.3f                       ? "yawn"
              : s_mood == PET_MOOD_LOVING             ? "love"
                                                      : "happy",
              now);
        break;
    case PET_HEARD_CHATTER:
        // Company: not bored, a bit livelier. No scene: they aren't talking to it.
        nudge(0.03f, 0.01f, 0);
        s_interacted_us = now;
        break;
    }
    xSemaphoreGive(s_lock);
}

void pet_set_reactions(bool on)
{
    s_react = on;
    if (!on) {
        s_pending = NULL;
    }
}

bool pet_reactions(void)
{
    return s_react;
}

const char *pet_mood_name(pet_mood_t mood)
{
    return mood < PET_MOOD_COUNT ? MOOD_NAMES[mood] : "content";
}

static const char *const NOW_NAMES[] = {"hot",  "music", "claude", "battery_low", "charging",
                                        "night", "away", "bored",  "quiet",       "chatter"};

const char *pet_now_name(uint32_t bit)
{
    for (int i = 0; i < (int) (sizeof(NOW_NAMES) / sizeof(NOW_NAMES[0])); i++) {
        if (bit == 1u << i) {
            return NOW_NAMES[i];
        }
    }
    return NULL;
}

static bool is_night(int minute);

void pet_get(pet_state_t *out)
{
    memset(out, 0, sizeof(*out));
    int64_t now = esp_timer_get_time();
    ears_state_t ears;
    ears_get(&ears);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    out->energy = s_energy;
    out->happiness = s_happy;
    out->affection = s_affection;
    out->mood = s_mood;
    out->away = s_away;
    out->minute = s_minute;
    out->idle_s = s_idle_s;
    out->lonely_s = (uint32_t) ((now - s_interacted_us) / 1000000);
    out->pitch = s_pitch;
    out->pace = 1.6f - s_energy;
    out->now = (s_hot ? PET_NOW_HOT : 0) | (s_music_s > 0 ? PET_NOW_MUSIC : 0) | (s_claude_working ? PET_NOW_CLAUDE : 0)
               | (s_battery_low ? PET_NOW_BATTERY_LOW : 0) | (s_plugged ? PET_NOW_CHARGING : 0)
               | (s_minute >= 0 && is_night(s_minute) ? PET_NOW_NIGHT : 0) | (s_away ? PET_NOW_AWAY : 0)
               | (s_mood == PET_MOOD_BORED ? PET_NOW_BORED : 0)
               | (ears.quiet_s >= QUIET_DROWSY_S ? PET_NOW_QUIET : 0) | (ears.chatter ? PET_NOW_CHATTER : 0);
    for (int i = 0; i < s_recent_count; i++) {
        int k = (s_recent_next - 1 - i + 2 * PET_RECENT) % PET_RECENT;
        out->recent[i].what = REACTION_NAMES[s_recent[k].r];
        out->recent[i].scene = s_recent[k].scene;
        out->recent[i].ago_s = (uint32_t) ((now - s_recent[k].us) / 1000000);
    }
    out->recent_count = s_recent_count;
    xSemaphoreGive(s_lock);
}

/** How lively it should be at `minute` past midnight: up in the morning, a
 * dip after lunch, winding down in the evening, asleep at night. */
static float day_energy(int minute)
{
    float h = minute / 60.0f;
    if (h < 7.0f) {
        return 0.15f;
    }
    if (h < 9.0f) {
        return 0.5f + 0.4f * (h - 7.0f) / 2.0f;
    }
    if (h < 13.0f) {
        return 0.9f;
    }
    if (h < 15.0f) {
        return 0.7f;
    }
    if (h < 19.0f) {
        return 0.85f;
    }
    if (h < 23.0f) {
        return 0.8f - 0.4f * (h - 19.0f) / 4.0f;
    }
    return 0.15f;
}

static bool is_night(int minute)
{
    return minute >= 23 * 60 || minute < 6 * 60;
}

/** With s_lock held. */
static pet_mood_t mood_now(int64_t now)
{
    if (s_hot) {
        return PET_MOOD_HOT;
    }
    if (s_energy < 0.3f) {
        return PET_MOOD_SLEEPY;
    }
    if (s_happy < 0.35f) {
        return s_affection > 0.6f ? PET_MOOD_SAD : PET_MOOD_GRUMPY;
    }
    if (s_happy > 0.75f && s_energy > 0.65f) {
        return PET_MOOD_EXCITED;
    }
    if (s_affection > 0.75f && s_happy > 0.6f) {
        return PET_MOOD_LOVING;
    }
    if (!s_away && now - s_interacted_us > (int64_t) BORED_S * 1000000) {
        return PET_MOOD_BORED;
    }
    return s_happy > 0.65f ? PET_MOOD_HAPPY : PET_MOOD_CONTENT;
}

/** With s_lock held: the reaction `r`, unless it's off or too soon. */
static void react(reaction_t r, const char *scene, int64_t now)
{
    if (!s_react || (s_last_us[r] != 0 && now - s_last_us[r] < (int64_t) COOLDOWN_S[r] * 1000000)) {
        return;
    }
    s_last_us[r] = now;
    s_recent[s_recent_next].r = r;
    s_recent[s_recent_next].scene = scene;
    s_recent[s_recent_next].us = now;
    s_recent_next = (s_recent_next + 1) % PET_RECENT;
    if (s_recent_count < PET_RECENT) {
        s_recent_count++;
    }
    s_pending = scene;
    s_pending_until_us = now + (int64_t) PENDING_S * 1000000;
    ESP_LOGI(TAG, "reacting: %s", scene);
}

/** With s_lock held: what changed since the last tick. */
static void notice(const metrics_snapshot_t *snap, int64_t now)
{
    bool live = snap->state == METRICS_UI_LIVE;
    if (!live) {
        s_seen = false;
        return;
    }
    const metrics_host_t *host = &snap->host;

    // You, at the computer or not.
    bool away = host->valid && host->idle_s >= AWAY_S;
    if (away && !s_away) {
        s_away_since_us = now - (int64_t) host->idle_s * 1000000;
    } else if (!away && s_away && s_seen) {
        int64_t gone_s = (now - s_away_since_us) / 1000000;
        bool morning = host->valid && host->minute >= 5 * 60 && host->minute < 12 * 60;
        nudge(0.05f, 0.06f, 0.03f);
        s_interacted_us = now;
        react(R_GREETING, gone_s >= LONG_AWAY_S || morning ? "excited" : s_affection > 0.5f ? "happy" : "wink", now);
    }
    s_away = away;
    s_minute = host->valid ? host->minute : -1;
    s_idle_s = host->valid ? host->idle_s : -1;

    // Bedtime, once a night, while you're still there.
    if (host->valid) {
        if (!is_night(host->minute)) {
            s_night_said = false;
        } else if (!s_night_said && !away && host->minute >= 23 * 60) {
            s_night_said = true;
            nudge(-0.1f, 0, 0);
            react(R_GOODNIGHT, "yawn", now);
        }
    }

    // Too hot, and cool again.
    float hottest = -1000.0f;
    if (snap->cpu.valid) {
        hottest = snap->cpu.temp_c;
    }
    if (snap->gpu.valid && snap->gpu.temp_c > hottest) {
        hottest = snap->gpu.temp_c;
    }
    s_hot_s = hottest >= HOT_C ? s_hot_s + 1 : 0;
    s_cool_s = hottest <= COOL_C ? s_cool_s + 1 : 0;
    if (!s_hot && s_hot_s >= HOT_FOR_S) {
        s_hot = true;
        nudge(-0.05f, -0.08f, 0);
        react(R_HOT, "hot", now);
    } else if (s_hot && s_cool_s >= COOL_FOR_S) {
        s_hot = false;
        nudge(0, 0.05f, 0);
        react(R_COOL, "relieved", now);
    }

    // Music: a dance once it's really playing, and now and then while it does.
    bool music = snap->music.valid && snap->music.playing;
    s_music_s = music ? s_music_s + 1 : 0;
    if (music) {
        s_interacted_us = now;
    }
    if (s_music_s >= MUSIC_FOR_S && !away && s_energy > 0.3f
        && (s_music_s == MUSIC_FOR_S || now - s_danced_us >= (int64_t) DANCE_EVERY_S * 1000000)) {
        s_danced_us = now;
        nudge(0.02f, 0.05f, 0);
        react(R_MUSIC, "dance", now);
    }

    // Claude: focused when it starts after a rest, proud when it's done.
    if (snap->claude.valid) {
        metrics_claude_state_t state = snap->claude.state;
        if (s_seen && state != s_claude) {
            if (state == METRICS_CLAUDE_WORK) {
                if (now - s_claude_off_us >= (int64_t) CLAUDE_REST_S * 1000000) {
                    react(R_CLAUDE_START, "focus", now);
                }
                s_claude_since_us = now;
            } else if (s_claude == METRICS_CLAUDE_WORK) {
                s_claude_off_us = now;
                if (now - s_claude_since_us >= (int64_t) CLAUDE_WORKED_S * 1000000) {
                    nudge(0, 0.04f, 0.01f);
                    react(R_CLAUDE_DONE, "proud", now);
                }
            }
        }
        s_claude = state;
        s_claude_working = state == METRICS_CLAUDE_WORK;
    } else {
        s_claude_working = false;
    }

    // The laptop's battery: running out, and plugged in.
    if (snap->battery.valid) {
        bool low = !snap->battery.plugged && snap->battery.pct <= BATTERY_LOW_PCT;
        if (low && !s_battery_low) {
            nudge(-0.1f, -0.03f, 0);
            react(R_BATTERY_LOW, "tired", now);
        }
        if (s_seen && snap->battery.plugged && !s_plugged) {
            nudge(0.08f, 0.04f, 0);
            react(R_CHARGING, "charged", now);
        }
        s_battery_low = low;
        s_plugged = snap->battery.plugged;
    }

    // Too many things went wrong.
    if (s_errors >= ERRORS_SAD) {
        s_errors = 0;
        react(R_SAD, "sad", now);
    }
    s_seen = true;
}

/** With s_lock held: one second on. */
static void drift(const metrics_snapshot_t *snap, float dt)
{
    float energy_rest = s_minute >= 0 ? day_energy(s_minute) : 0.7f;
    ears_state_t ears;
    ears_get(&ears);
    if (ears.quiet_s >= QUIET_DROWSY_S && s_music_s == 0) {
        // Nothing to hear for half an hour: drowsy.
        energy_rest = fmaxf(0.1f, energy_rest - QUIET_ENERGY);
    }
    float happy_rest = s_away && esp_timer_get_time() - s_away_since_us > 2LL * 3600 * 1000000 ? 0.5f : HAPPY_REST;
    s_energy += (energy_rest - s_energy) * dt / ENERGY_TAU_S;
    s_happy += (happy_rest - s_happy) * dt / HAPPY_TAU_S;
    s_affection += (AFFECTION_REST - s_affection) * dt / AFFECTION_TAU_S;
    if (s_music_s > 0) {
        s_happy += 0.0004f * dt;
    }
    if (s_hot) {
        s_happy -= 0.0003f * dt;
    }
    nudge(0, 0, 0);
}

void pet_update(const metrics_snapshot_t *snap)
{
    if (s_lock == NULL) {
        return;
    }
    int64_t now = esp_timer_get_time();
    // The mood moves once a second; a reaction plays as soon as it comes (a
    // bang in the room makes it jump now, not in a second).
    bool tick = s_tick_us == 0 || now - s_tick_us >= TICK_US;
    float dt = s_tick_us != 0 ? (float) (now - s_tick_us) / 1000000.0f : 1.0f;
    bool due = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (tick) {
        s_tick_us = now;
        notice(snap, now);
        drift(snap, dt);
        s_mood = mood_now(now);
        s_pitch = 1.0f + 0.16f * (s_happy - 0.5f) + 0.10f * (s_energy - 0.5f);
        due = now - s_saved_us >= (int64_t) SAVE_EVERY_S * 1000000
              && (fabsf(s_energy - s_saved[0]) >= SAVE_STEP || fabsf(s_happy - s_saved[1]) >= SAVE_STEP
                  || fabsf(s_affection - s_saved[2]) >= SAVE_STEP);
        if (due) {
            s_saved[0] = s_energy;
            s_saved[1] = s_happy;
            s_saved[2] = s_affection;
            s_saved_us = now;
        }
    }
    if (s_pending != NULL && now > s_pending_until_us) {
        s_pending = NULL;
    }
    xSemaphoreGive(s_lock);

    if (due) {
        save();
    }
}

const char *pet_take_reaction(void)
{
    if (s_lock == NULL) {
        return NULL;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const char *scene = s_pending;
    s_pending = NULL;
    xSemaphoreGive(s_lock);
    return scene;
}

pet_mood_t pet_mood(void)
{
    return s_mood;
}

bool pet_quiet(void)
{
    return s_away;
}

const char *pet_idle_scene(void)
{
    const pool_t *pool = &POOLS[s_mood < PET_MOOD_COUNT ? s_mood : PET_MOOD_CONTENT];
    return pool->names[esp_random() % pool->count];
}

float pet_idle_pace(void)
{
    // Lively: every 20-70 s; worn out: every 45-170 s.
    return 1.6f - s_energy;
}
