#include "life.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "eyes.h"
#include "lvgl.h"
#include "playback.h"

static const char *TAG = "life";

/* Calling you: how near it comes and how long it waits for an answer. */
#define CALL_Z 0.62f
#define CALL_WAIT_MS 11000

typedef enum {
    ACT_LOUNGE, // floating about near you
    ACT_CALL,   // up at the glass: an agent waits for you
    ACT_NAP,    // yawns, finds a corner, settles down
    ACT_SLEEP,
    ACT_COME,   // called or touched: back to you
    ACT_COUNT,
} act_t;

static const char *const NAMES[ACT_COUNT] = {"lounge", "call", "nap", "sleep", "come"};

static struct {
    life_ctx_t ctx;
    uint32_t now;
    act_t act;
    int phase;
    uint32_t wait_until;
    int side;
    bool answered;
} L;

static float frange(float lo, float hi)
{
    return lo + (hi - lo) * (float) (esp_random() & 0xFFFF) / 65535.0f;
}

/* The point at distance `z` that shows (sx, sy) pixels from the middle. */
static eyes_pos_t at(float sx, float sy, float z)
{
    return (eyes_pos_t) {sx * z, sy * z, z};
}

static void wait_ms(uint32_t ms)
{
    L.wait_until = L.now + ms;
}

static bool waiting(void)
{
    return (int32_t) (L.now - L.wait_until) < 0;
}

/* Waiting, or still on the way. */
static bool busy(void)
{
    return waiting() || eyes_moving();
}

static void next(int phase)
{
    L.phase = phase;
}

static void start(act_t act)
{
    if (L.act != act) {
        ESP_LOGI(TAG, "%s -> %s", NAMES[L.act], NAMES[act]);
    }
    L.act = act;
    L.phase = 0;
    L.wait_until = L.now;
    eyes_look_around();
}

/* Drifting a little way off and pausing there; slower when drowsy. */
static void lounge(void)
{
    const bool calm = L.ctx.drowsy || L.ctx.focus;
    eyes_set_mood(L.ctx.drowsy ? EYES_MOOD_SLEEPY : EYES_MOOD_NEUTRAL);
    if (busy()) {
        return;
    }
    const eyes_pos_t p = calm ? at(frange(-35, 35), frange(0, 30), frange(1.0f, 1.25f))
                              : at(frange(-65, 65), frange(-28, 40), frange(0.88f, 1.3f));
    const uint32_t ms = (uint32_t) frange(2600, 4600) * (calm ? 3 : 2) / 2;
    eyes_move_to(p.x, p.y, p.z, ms, EYES_PATH_DRIFT);
    wait_ms(ms + (uint32_t) frange(2500, 7000));
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
        // Knock, knock: two taps on the glass, then pointing back at the
        // computer, where something needs you.
        const eyes_pos_t taps[] = {at(0, 22, 0.54f), at(0, 22, CALL_Z), at(0, 22, 0.54f), at(0, 22, CALL_Z)};
        eyes_move(taps, 4, 700, EYES_PATH_GLIDE);
        eyes_gesture(EYES_GESTURE_POINT);
        if (L.ctx.agent_sounds) {
            playback_sound(SOUND_HMM);
        }
        wait_ms(CALL_WAIT_MS + (uint32_t) frange(0, 4000));
        next(2);
        break;
    }
    case 2:
        if (L.answered) {
            eyes_set_mood(EYES_MOOD_HAPPY);
        } else if (waiting()) {
            eyes_look_at(0, -0.12f);
            return;
        }
        eyes_look_around();
        eyes_move_to(0, 0, 1, 1800, EYES_PATH_GLIDE);
        next(3);
        break;
    default:
        if (!busy()) {
            start(ACT_LOUNGE);
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
        if (!L.ctx.focus) {
            playback_sound(SOUND_YAWN);
        }
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
        start(ACT_LOUNGE);
    }
}

void life_init(void)
{
    memset(&L, 0, sizeof(L));
    L.now = lv_tick_get();
    start(ACT_LOUNGE);
}

life_want_t life_step(const life_ctx_t *ctx)
{
    L.ctx = *ctx;
    L.now = lv_tick_get();
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
        return LIFE_NOTHING;
    }
    if (ctx->lonely && L.act != ACT_NAP) {
        start(ACT_NAP);
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
    default:
        lounge();
        break;
    }
    return LIFE_NOTHING;
}

void life_attend(void)
{
    L.now = lv_tick_get();
    if (L.act == ACT_CALL && L.phase >= 1) {
        L.answered = true;
        return;
    }
    if (L.act == ACT_SLEEP) {
        return;
    }
    start(ACT_COME);
}

void life_slept(void)
{
    L.now = lv_tick_get();
    start(ACT_SLEEP);
}

void life_woke(void)
{
    L.now = lv_tick_get();
    start(ACT_COME);
}

bool life_call(void)
{
    L.now = lv_tick_get();
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
    return NAMES[L.act];
}
