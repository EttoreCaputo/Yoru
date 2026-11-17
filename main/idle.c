#include "idle.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "eyes.h"
#include "idle_int.h"
#include "lvgl.h"
#include "playback.h"

static const char *TAG = "idle";

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
/* Between two acts it waits this long: something you notice now and then,
 * not all the time. Drowsy, longer. */
#define GAP_LO_MS 6000
#define GAP_HI_MS 15000
#define DROWSY_GAP_LO_MS 8000
#define DROWSY_GAP_HI_MS 16000
/* After an interrupt, a moment before the next act. */
#define RESUME_GAP_MS 1500

/* What interrupts the idle animations. */
typedef enum {
    IRQ_NONE,
    IRQ_CALL,  // up at the glass: something waits for you
    IRQ_NAP,   // yawns, finds a corner, settles down
    IRQ_SLEEP,
    IRQ_COME,  // called or touched: back to you
    IRQ_GREET, // you're back: it comes forward and waves
    IRQ_COUNT,
} irq_t;

static const char *const NAMES[IRQ_COUNT] = {"idle", "call", "nap", "sleep", "come", "greet"};

idle_tick_t IT;

static struct {
    irq_t act;
    int phase;
    uint32_t wait_until;
    int side;
    bool answered;
    /* Away long enough for a greeting; when someone came back. */
    bool missed;
    uint32_t back_ms;
    bool morning;
    int32_t greeted_day;
    bool was_free;
} L;

/* ---- Shared helpers (idle_int.h) --------------------------------------------- */

float idle_frange(float lo, float hi)
{
    return lo + (hi - lo) * (float) (esp_random() & 0xFFFF) / 65535.0f;
}

eyes_pos_t idle_at(float sx, float sy, float z)
{
    return (eyes_pos_t) {sx * z, sy * z, z};
}

bool idle_night(void)
{
    return IT.ctx.minute >= 0 && (IT.ctx.minute >= NIGHT_FROM || IT.ctx.minute < NIGHT_TO);
}

void idle_sound(sound_t sound)
{
    if (!IT.ctx.focus) {
        playback_sound(sound);
    }
}

/* ---- The picker: the next act, at random ---------------------------------------- */

typedef struct {
    act_id_t act;
    float w;
} pick_t;

/* Awake: mostly still or drifting; the livelier acts are rarer, and the
 * gestures (wave to love) rarer still, so that each still surprises. */
static const pick_t AWAKE[] = {
    {ACT_REST, 8},      {ACT_DRIFT, 3},    {ACT_LOOK, 1.5f},  {ACT_GAZE, 1.5f},    {ACT_TILT, 0.8f},
    {ACT_PEEK, 0.8f},   {ACT_SNIFF, 0.8f}, {ACT_WINK, 0.5f},  {ACT_STRETCH, 0.4f}, {ACT_DANCE, 0.5f},
    {ACT_LOOP, 0.4f},   {ACT_BOUNCE, 0.4f}, {ACT_STARS, 0.5f}, {ACT_HUM, 0.5f},     {ACT_FIREFLY, 0.5f},
    {ACT_NOD, 0.2f},    {ACT_SNEEZE, 0.15f}, {ACT_HICCUP, 0.15f}, {ACT_WAVE, 0.25f}, {ACT_HOP, 0.2f},
    {ACT_SHY, 0.2f},    {ACT_GIGGLE, 0.2f}, {ACT_CHEER, 0.15f}, {ACT_LOVE, 0.15f},
};
/* Drowsy (nobody around a while, or the battery low): slow and sleepy. */
static const pick_t DROWSY[] = {
    {ACT_REST, 6}, {ACT_NOD, 2}, {ACT_YAWN, 1.5f}, {ACT_DRIFT, 1}, {ACT_STRETCH, 0.5f},
};

#define LEN(a) (int) (sizeof(a) / sizeof(a[0]))

/* Its face and room: near you, unhurried; drowsy, lower and slower. */
static const act_style_t STYLE_AWAKE = {EYES_MOOD_NEUTRAL, 45, 30, 0.95f, 1.25f, 1.4f};
static const act_style_t STYLE_DROWSY = {EYES_MOOD_SLEEPY, 35, 30, 1.1f, 1.45f, 2};

static struct {
    uint32_t act_at[ACT_COUNT];
    bool ever[ACT_COUNT];
    act_id_t last;
    act_id_t next;      // played next whatever the weights; ACT_COUNT none
    bool drowsy;        // as it was at the last pick
    bool running;       // an act is in hand; else waiting between two
    uint32_t gap_until;
} P;

static bool cooled(act_id_t a)
{
    return !P.ever[a] || IT.now - P.act_at[a] >= (uint32_t) ACTS[a].cooldown_s * 1000;
}

/* Whether `a` may play now: cooled down, not the one just played (resting
 * may go on), not too lively for Do not disturb. */
static bool allowed(act_id_t a)
{
    if (IT.ctx.focus && (ACTS[a].flags & ACTF_LOUD)) {
        return false;
    }
    return cooled(a) && (a == ACT_REST || a != P.last);
}

/* The fireflies and the stars come out at night. */
static float weight(const pick_t *p)
{
    return (p->act == ACT_FIREFLY || p->act == ACT_STARS) && idle_night() ? p->w * 2 : p->w;
}

static act_id_t pick(void)
{
    const bool drowsy = IT.ctx.drowsy;
    if (drowsy && !P.drowsy) {
        P.drowsy = true;
        return ACT_YAWN; // getting sleepy
    }
    P.drowsy = drowsy;
    if (P.next != ACT_COUNT) {
        const act_id_t a = P.next;
        P.next = ACT_COUNT;
        return a;
    }
    const pick_t *table = drowsy ? DROWSY : AWAKE;
    const int n = drowsy ? LEN(DROWSY) : LEN(AWAKE);
    float total = 0;
    for (int i = 0; i < n; i++) {
        total += allowed(table[i].act) ? weight(&table[i]) : 0;
    }
    float r = idle_frange(0, total);
    for (int i = 0; i < n; i++) {
        if (!allowed(table[i].act)) {
            continue;
        }
        if (r < weight(&table[i])) {
            return table[i].act;
        }
        r -= weight(&table[i]);
    }
    return ACT_REST;
}

static void begin(act_id_t a)
{
    P.act_at[a] = IT.now;
    P.ever[a] = true;
    P.last = a;
    P.running = true;
    acts_start(a, IT.ctx.drowsy ? &STYLE_DROWSY : &STYLE_AWAKE);
    ESP_LOGD(TAG, "%s", ACTS[a].name);
}

static void gap(void)
{
    const bool drowsy = IT.ctx.drowsy;
    P.running = false;
    P.gap_until = IT.now + (uint32_t) idle_frange(drowsy ? DROWSY_GAP_LO_MS : GAP_LO_MS,
                                                  drowsy ? DROWSY_GAP_HI_MS : GAP_HI_MS);
}

static void pick_step(void)
{
    if (P.running) {
        if (acts_step()) {
            gap();
        }
        return;
    }
    if ((int32_t) (IT.now - P.gap_until) >= 0) {
        begin(pick());
    }
}

/* Something else had the eyes: the act in hand is dropped. With `with_you`,
 * someone is here: the next act is a look at them. */
static void interrupted(bool with_you)
{
    eyes_firefly(false, 0, 0);
    eyes_cross(0);
    P.running = false;
    P.gap_until = IT.now + RESUME_GAP_MS;
    if (with_you) {
        P.next = ACT_GAZE;
    }
}

/* ---- The interrupts ------------------------------------------------------------ */

static void wait_ms(uint32_t ms)
{
    L.wait_until = IT.now + ms;
}

static bool waiting(void)
{
    return (int32_t) (IT.now - L.wait_until) < 0;
}

static bool busy(void)
{
    return waiting() || eyes_moving() || eyes_playing();
}

static void next(int phase)
{
    L.phase = phase;
}

static void start(irq_t act)
{
    if (L.act != act) {
        ESP_LOGI(TAG, "%s -> %s", idle_activity(), NAMES[act]);
    }
    if (act != IRQ_NONE) {
        interrupted(false);
    }
    L.act = act;
    L.phase = 0;
    L.wait_until = IT.now;
    eyes_look_around();
}

/* Back to the idle animations. */
static void resume(bool with_you)
{
    L.act = IRQ_NONE;
    L.phase = 0;
    interrupted(with_you);
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
        // Knock, knock on the glass, a hopeful look, a little wave.
        eyes_play(CLIP_KNOCK_GLASS, false);
        playback_sound(SOUND_HMM);
        wait_ms(CALL_WAIT_MS + (uint32_t) idle_frange(0, 4000));
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
            resume(L.answered);
        }
        break;
    }
}

static idle_want_t nap(void)
{
    switch (L.phase) {
    case 0:
        eyes_set_mood(EYES_MOOD_SLEEPY);
        eyes_play(CLIP_YAWN, false);
        idle_sound(SOUND_YAWN);
        wait_ms(2200);
        next(1);
        break;
    case 1:
        if (waiting()) {
            return IDLE_NOTHING;
        }
        // A cosy corner, low down.
        L.side = (esp_random() & 1) ? 1 : -1;
        eyes_move_to((float) L.side * 90 * 1.35f, 55 * 1.35f, 1.35f, 3800, EYES_PATH_DRIFT);
        eyes_look_at(0, 0.7f);
        next(2);
        break;
    case 2:
        if (busy()) {
            return IDLE_NOTHING;
        }
        // A drowsy little wave goodnight, with the hand away from the edge.
        eyes_play(CLIP_SLEEPY_WAVE, L.side > 0);
        wait_ms(3000);
        next(3);
        break;
    case 3:
        if (waiting()) {
            return IDLE_NOTHING;
        }
        eyes_blink();
        wait_ms(1800);
        next(4);
        break;
    default:
        return waiting() ? IDLE_NOTHING : IDLE_SLEEP;
    }
    return IDLE_NOTHING;
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
        resume(true);
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
            eyes_play(CLIP_WAKE_STRETCH, false);
            idle_sound(SOUND_YAWN);
            wait_ms(3000);
        }
        next(1);
        break;
    case 1:
        if (busy()) {
            return;
        }
        eyes_play(CLIP_GREET_BACK, false);
        playback_sound(SOUND_HELLO);
        wait_ms(2400);
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
            resume(true);
        }
        break;
    }
}

/* Someone around again after a long while, or for the first time in the
 * morning: a greeting is due. */
static void keep_track(void)
{
    if (IT.ctx.alone_ms >= GREET_AFTER_MS) {
        L.missed = true;
    } else if (IT.ctx.alone_ms < BACK_MS && L.back_ms == 0) {
        const bool morning = IT.ctx.day >= 0 && IT.ctx.day != L.greeted_day && IT.ctx.minute >= MORNING_FROM
                             && IT.ctx.minute < MORNING_TO;
        if (L.missed || morning) {
            L.back_ms = IT.now;
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
    const bool fresh = IT.now - L.back_ms < GREET_WITHIN_MS;
    L.back_ms = 0;
    if (fresh && !IT.ctx.focus) {
        L.greeted_day = IT.ctx.day;
        return true;
    }
    return false;
}

static void tick(void)
{
    IT.now = lv_tick_get();
}

/* ---- API ----------------------------------------------------------------------- */

void idle_init(void)
{
    memset(&L, 0, sizeof(L));
    memset(&IT, 0, sizeof(IT));
    memset(&P, 0, sizeof(P));
    IT.ctx.minute = -1;
    IT.ctx.day = -1;
    tick();
    L.greeted_day = -1;
    L.act = IRQ_NONE;
    P.last = ACT_COUNT;
    P.next = ACT_COUNT;
    P.gap_until = IT.now + RESUME_GAP_MS;
}

idle_want_t idle_step(const idle_ctx_t *ctx)
{
    IT.ctx = *ctx;
    tick();
    keep_track();
    if (ctx->asleep) {
        if (L.act != IRQ_SLEEP) {
            idle_slept();
        }
        return IDLE_NOTHING;
    }
    if (L.act == IRQ_SLEEP) {
        idle_woke(); // woken some other way
    }
    if (!ctx->free) {
        // Something else has the eyes: whatever it was doing up close or
        // on its own starts over when they're free again.
        if (L.was_free && L.act == IRQ_NONE) {
            interrupted(false);
        }
        L.was_free = false;
        return IDLE_NOTHING;
    }
    L.was_free = true;
    if (ctx->away && L.act != IRQ_NAP) {
        start(IRQ_NAP);
    }
    if ((L.act == IRQ_NONE || L.act == IRQ_COME) && greeting_due()) {
        start(IRQ_GREET);
    }
    switch (L.act) {
    case IRQ_CALL:
        call();
        break;
    case IRQ_NAP:
        return nap();
    case IRQ_COME:
        come();
        break;
    case IRQ_GREET:
        greet();
        break;
    default:
        pick_step();
        break;
    }
    return IDLE_NOTHING;
}

void idle_attend(void)
{
    tick();
    if (L.act == IRQ_CALL && L.phase >= 1) {
        L.answered = true;
        return;
    }
    if (L.act == IRQ_SLEEP) {
        return;
    }
    start(IRQ_COME);
}

void idle_slept(void)
{
    tick();
    start(IRQ_SLEEP);
}

void idle_woke(void)
{
    tick();
    start(IRQ_COME);
}

bool idle_call(void)
{
    tick();
    if (L.act == IRQ_SLEEP || (L.act == IRQ_CALL && L.phase < 3) || (L.act == IRQ_NAP && L.phase >= 2)) {
        return false;
    }
    start(IRQ_CALL);
    return true;
}

bool idle_calling(void)
{
    return L.act == IRQ_CALL && L.phase >= 1 && L.phase < 3;
}

const char *idle_activity(void)
{
    if (L.act == IRQ_NONE) {
        return IT.ctx.drowsy ? "drowsy" : "idle";
    }
    return NAMES[L.act];
}
