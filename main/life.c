#include "life.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "eyes.h"
#include "life_int.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "playback.h"

static const char *TAG = "life";

/* Calling you: how near it comes and how long it waits for an answer. */
#define CALL_Z 0.62f
#define CALL_WAIT_MS 11000
/* Away this long and back: a greeting, if it comes within GREET_WITHIN_MS
 * (not after a whole conversation). */
#define GREET_AFTER_MS (15 * 60 * 1000)
#define BACK_MS 5000
#define GREET_WITHIN_MS 6000
/* The morning's greeting: the first time someone is around in these hours. */
#define MORNING_FROM (5 * 60)
#define MORNING_TO (12 * 60)
#define NIGHT_FROM (22 * 60)
#define NIGHT_TO (6 * 60)
/* debug/life may run the life clock up to this much faster. */
#define SPEED_MAX 300

/* What interrupts its own life; MIND is that life (life_mind.c). */
typedef enum {
    ACT_MIND,
    ACT_CALL,  // up at the glass: something waits for you
    ACT_NAP,   // yawns, finds a corner, settles down
    ACT_SLEEP,
    ACT_COME,  // called or touched: back to you
    ACT_GREET, // you're back: it comes forward and waves
    ACT_IRQ_COUNT,
} irq_t;

static const char *const NAMES[ACT_IRQ_COUNT] = {"mind", "call", "nap", "sleep", "come", "greet"};

life_tick_t LT;

static struct {
    irq_t act;
    int phase;
    uint32_t wait_until;
    int side;
    bool answered;
    uint32_t last_tick;
    /* Away long enough for a greeting; when someone came back. */
    bool missed;
    uint32_t back_ms;
    bool morning;
    int32_t greeted_day;
    bool was_free;
} L;

/* ---- Shared helpers (life_int.h) --------------------------------------------- */

float life_frange(float lo, float hi)
{
    return lo + (hi - lo) * (float) (esp_random() & 0xFFFF) / 65535.0f;
}

eyes_pos_t life_at(float sx, float sy, float z)
{
    return (eyes_pos_t) {sx * z, sy * z, z};
}

bool life_night(void)
{
    return LT.ctx.minute >= 0 && (LT.ctx.minute >= NIGHT_FROM || LT.ctx.minute < NIGHT_TO);
}

void life_sound(sound_t sound)
{
    if (!LT.ctx.focus) {
        playback_sound(sound);
    }
}

/* ---- The interrupts ------------------------------------------------------------ */

static void wait_ms(uint32_t ms)
{
    L.wait_until = LT.now + ms;
}

static bool waiting(void)
{
    return (int32_t) (LT.now - L.wait_until) < 0;
}

static bool busy(void)
{
    return waiting() || eyes_moving();
}

static void next(int phase)
{
    L.phase = phase;
}

static void start(irq_t act)
{
    if (L.act != act) {
        ESP_LOGI(TAG, "%s -> %s", L.act == ACT_MIND ? mind_state() : NAMES[L.act], NAMES[act]);
    }
    if (act != ACT_MIND) {
        mind_interrupted(RESUME_HOLD);
    }
    L.act = act;
    L.phase = 0;
    L.wait_until = LT.now;
    eyes_look_around();
}

/* Back to its own life, picking up as `resume` says. */
static void resume(resume_t how)
{
    L.act = ACT_MIND;
    L.phase = 0;
    mind_interrupted(how);
}

static void call(void)
{
    switch (L.phase) {
    case 0:
        L.answered = false;
        eyes_set_mood(EYES_MOOD_CURIOUS);
        // Up to the glass, its face filling the screen.
        eyes_move_to(0, 22 * CALL_Z, CALL_Z, 1400, EYES_PATH_GLIDE);
        next(1);
        break;
    case 1: {
        if (busy()) {
            return;
        }
        eyes_look_at(0, -0.12f);
        // Knock, knock: two taps on the glass, then pointing at what
        // waits for you.
        const eyes_pos_t taps[] = {life_at(0, 22, 0.54f), life_at(0, 22, CALL_Z), life_at(0, 22, 0.54f),
                                   life_at(0, 22, CALL_Z)};
        eyes_move(taps, 4, 700, EYES_PATH_GLIDE);
        eyes_gesture(EYES_GESTURE_POINT);
        playback_sound(SOUND_HMM);
        wait_ms(CALL_WAIT_MS + (uint32_t) life_frange(0, 4000));
        next(2);
        break;
    }
    case 2:
        if (L.answered) {
            eyes_set_mood(EYES_MOOD_HAPPY);
        } else if (waiting()) {
            eyes_look_at(0, -0.12f);
            return;
        } else {
            mood_feel(LIFE_FEEL_IGNORED);
        }
        eyes_look_around();
        eyes_move_to(0, 0, 1, 1800, EYES_PATH_GLIDE);
        next(3);
        break;
    default:
        if (!busy()) {
            resume(L.answered ? RESUME_WITH : RESUME_HOLD);
        }
        break;
    }
}

static life_want_t nap(void)
{
    switch (L.phase) {
    case 0:
        eyes_set_mood(EYES_MOOD_SLEEPY);
        eyes_gesture(EYES_GESTURE_STRETCH);
        life_sound(SOUND_YAWN);
        wait_ms(2200);
        next(1);
        break;
    case 1:
        if (waiting()) {
            return LIFE_NOTHING;
        }
        // A cosy corner, low down.
        L.side = (esp_random() & 1) ? 1 : -1;
        eyes_move_to((float) L.side * 90 * 1.35f, 55 * 1.35f, 1.35f, 3800, EYES_PATH_DRIFT);
        eyes_look_at(0, 0.7f);
        next(2);
        break;
    case 2:
        if (busy()) {
            return LIFE_NOTHING;
        }
        eyes_blink();
        wait_ms(1800);
        next(3);
        break;
    case 3:
        if (waiting()) {
            return LIFE_NOTHING;
        }
        eyes_blink();
        wait_ms(1800);
        next(4);
        break;
    default:
        return waiting() ? LIFE_NOTHING : LIFE_SLEEP;
    }
    return LIFE_NOTHING;
}

static void come(void)
{
    if (L.phase == 0) {
        const eyes_pos_t me = eyes_where();
        eyes_set_mood(EYES_MOOD_NEUTRAL);
        // Off to a side or far: back to you.
        if (me.z > 1.4f || me.z < 0.8f || hypotf(me.x / me.z, me.y / me.z) > 90) {
            eyes_move_to(0, 0, 1, 800, EYES_PATH_GLIDE);
        }
        next(1);
        return;
    }
    if (!busy()) {
        resume(RESUME_WITH);
    }
}

/* You're back (or it's morning): it comes forward, stretches first in the
 * morning, and waves. */
static void greet(void)
{
    switch (L.phase) {
    case 0:
        eyes_set_mood(EYES_MOOD_HAPPY);
        eyes_look_at(0, 0);
        eyes_move_to(0, 12 * 0.78f, 0.78f, 900, EYES_PATH_GLIDE);
        if (L.morning) {
            eyes_gesture(EYES_GESTURE_STRETCH);
            life_sound(SOUND_YAWN);
            wait_ms(1900);
        }
        next(1);
        break;
    case 1:
        if (busy()) {
            return;
        }
        eyes_gesture(EYES_GESTURE_WAVE);
        playback_sound(SOUND_HELLO);
        mood_feel(LIFE_FEEL_GREETED);
        wait_ms(1900);
        next(2);
        break;
    case 2:
        if (waiting()) {
            return;
        }
        eyes_move_to(0, 0, 1, 1200, EYES_PATH_GLIDE);
        next(3);
        break;
    default:
        if (!busy()) {
            resume(RESUME_WITH);
        }
        break;
    }
}

/* Someone around again after a long while, or for the first time in the
 * morning: a greeting is due. */
static void keep_track(void)
{
    if (LT.ctx.alone_ms >= GREET_AFTER_MS) {
        L.missed = true;
    } else if (LT.ctx.alone_ms < BACK_MS && L.back_ms == 0) {
        const bool morning = LT.ctx.day >= 0 && LT.ctx.day != L.greeted_day && LT.ctx.minute >= MORNING_FROM
                             && LT.ctx.minute < MORNING_TO;
        if (L.missed || morning) {
            L.back_ms = LT.now;
            L.morning = morning;
        }
        L.missed = false;
    }
}

/* A greeting due, if it still fits: not after a whole conversation. */
static bool greeting_due(void)
{
    if (L.back_ms == 0) {
        return false;
    }
    const bool fresh = LT.now - L.back_ms < GREET_WITHIN_MS;
    L.back_ms = 0;
    if (fresh && !LT.ctx.focus) {
        L.greeted_day = LT.ctx.day;
        return true;
    }
    return false;
}

/* The clocks: the real one, and the life clock, faster in tests. */
static void tick(void)
{
    const uint32_t now = lv_tick_get();
    const uint32_t dt = now - L.last_tick;
    L.last_tick = now;
    LT.now = now;
    LT.vnow += (uint32_t) ((float) dt * LT.speed);
}

/* ---- API ----------------------------------------------------------------------- */

void life_init(void)
{
    memset(&L, 0, sizeof(L));
    memset(&LT, 0, sizeof(LT));
    LT.speed = 1;
    LT.ctx.minute = -1;
    LT.ctx.day = -1;
    L.last_tick = lv_tick_get();
    tick();
    L.greeted_day = -1;
    mood_init();
    mind_init();
    L.act = ACT_MIND;
}

life_want_t life_step(const life_ctx_t *ctx)
{
    LT.ctx = *ctx;
    tick();
    mood_step();
    keep_track();
    if (ctx->asleep) {
        if (L.act != ACT_SLEEP) {
            life_slept();
        }
        return LIFE_NOTHING;
    }
    if (L.act == ACT_SLEEP) {
        life_woke(); // woken some other way
    }
    if (!ctx->free) {
        // Something else has the eyes: whatever it was doing up close or
        // on its own starts over when they're free again.
        if (L.was_free && L.act == ACT_MIND) {
            mind_interrupted(RESUME_HOLD);
        }
        L.was_free = false;
        return LIFE_NOTHING;
    }
    L.was_free = true;
    if (ctx->lonely && L.act != ACT_NAP) {
        start(ACT_NAP);
    }
    if ((L.act == ACT_MIND || L.act == ACT_COME) && greeting_due()) {
        start(ACT_GREET);
    }
    switch (L.act) {
    case ACT_CALL:
        call();
        break;
    case ACT_NAP:
        return nap();
    case ACT_COME:
        come();
        break;
    case ACT_GREET:
        greet();
        break;
    default:
        if (mind_step()) {
            start(ACT_NAP);
        }
        break;
    }
    return LIFE_NOTHING;
}

void life_attend(void)
{
    tick();
    if (L.act == ACT_CALL && L.phase >= 1) {
        L.answered = true;
        return;
    }
    if (L.act == ACT_SLEEP) {
        return;
    }
    start(ACT_COME);
}

void life_feel(life_feel_t feel)
{
    tick();
    mood_feel(feel);
}

void life_slept(void)
{
    tick();
    start(ACT_SLEEP);
}

void life_woke(void)
{
    tick();
    mood_feel(LIFE_FEEL_WOKE);
    start(ACT_COME);
}

bool life_call(void)
{
    tick();
    if (L.act == ACT_SLEEP || (L.act == ACT_CALL && L.phase < 3) || (L.act == ACT_NAP && L.phase >= 2)) {
        return false;
    }
    start(ACT_CALL);
    return true;
}

bool life_calling(void)
{
    return L.act == ACT_CALL && L.phase >= 1 && L.phase < 3;
}

const char *life_activity(void)
{
    return L.act == ACT_MIND ? mind_state() : NAMES[L.act];
}

float life_cheer(void)
{
    return (mood(EMO_HAPPY) + 1) / 2;
}

cJSON *life_status(void)
{
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "activity", life_activity());
    mind_status(out);
    mood_status(cJSON_AddObjectToObject(out, "emotions"));
    if (LT.speed != 1) {
        cJSON_AddNumberToObject(out, "speed", LT.speed);
    }
    return out;
}

bool life_debug(const cJSON *params, char *err, size_t len)
{
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(params, "state");
    const cJSON *act = cJSON_GetObjectItemCaseSensitive(params, "act");
    const cJSON *feel = cJSON_GetObjectItemCaseSensitive(params, "feel");
    const cJSON *speed = cJSON_GetObjectItemCaseSensitive(params, "speed");
    bool ok = true;
    lvgl_port_lock();
    tick();
    if (cJSON_IsNumber(speed)) {
        LT.speed = speed->valuedouble < 1 ? 1 : speed->valuedouble > SPEED_MAX ? SPEED_MAX : (float) speed->valuedouble;
    }
    if (cJSON_IsString(feel)) {
        int f = 0;
        while (f < LIFE_FEEL_COUNT && strcmp(mood_feel_name((life_feel_t) f), feel->valuestring) != 0) {
            f++;
        }
        if (f == LIFE_FEEL_COUNT) {
            snprintf(err, len, "no feeling \"%s\"", feel->valuestring);
            ok = false;
        } else {
            mood_feel((life_feel_t) f);
        }
    }
    if (ok && (cJSON_IsString(state) || cJSON_IsString(act)) && L.act != ACT_MIND && L.act != ACT_SLEEP) {
        resume(RESUME_HOLD); // out of a call or a greeting, for the test
    }
    if (ok && cJSON_IsString(state) && !mind_force_state(state->valuestring)) {
        snprintf(err, len, "no state \"%s\"", state->valuestring);
        ok = false;
    }
    if (ok && cJSON_IsString(act) && !mind_force_act(act->valuestring)) {
        snprintf(err, len, "no act \"%s\"", act->valuestring);
        ok = false;
    }
    lvgl_port_unlock();
    return ok;
}
