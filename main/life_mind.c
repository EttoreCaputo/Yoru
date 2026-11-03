/* The state Yoru is in, after Vector's high-level AI: each state has a
 * look, a repertoire of acts with weights, how long it lasts and how soon it
 * may come back, and weighted ways out (Shimeji's NextBehavior). The
 * emotions scale every weight: weight x (1 + k x emotion) for each modifier.
 * Some ways out don't wait for the state to end (Vector's interrupting
 * transitions): too tired, too lonely, too excited, too bored.
 *
 * Between two acts it waits a little, as long as the state says. An act up
 * close takes Yoru to the glass first and back after; a state that lives up
 * close (with you, looking for you) stays there between its acts. */

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "life_int.h"

static const char *TAG = "mind";

/* The tables leave out the modifiers a weight doesn't have. */
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

typedef enum {
    ST_LOUNGE,  // floating about near you
    ST_WATCH,   // up at the glass, with you
    ST_EXPLORE, // the edges, the corners, a firefly
    ST_PLAY,    // on its own, for fun
    ST_DREAM,   // slow, eyes on the sky it came from
    ST_BORED,
    ST_SEEK,    // lonely: at the glass, looking for you
    ST_SULK,    // looked for you and nobody came
    ST_DROWSY,
    ST_COUNT,
} state_id_t;

/* A weight's modifier: x max(MOD_FLOOR, 1 + k x the emotion). MOD_NIGHT is
 * 1 at night and -1 by day. */
#define MOD_NIGHT EMO_COUNT
#define MOD_FLOOR 0.05f

typedef struct {
    uint8_t e;
    float k;
} mod_t;

typedef struct {
    act_id_t act;
    float w;
    mod_t m[2];
} pick_t;

typedef struct {
    state_id_t to;
    float w;
    mod_t m[2];
} way_t;

typedef struct {
    const char *name;
    act_style_t style;
    bool close;    // lives up close
    bool quiet;    // fits Do not disturb
    uint16_t dwell_lo_s, dwell_hi_s;
    uint16_t gap_lo_ms, gap_hi_ms;
    uint16_t cooldown_s;
    act_id_t enter; // played first; ACT_COUNT: none
    const pick_t *acts;
    int n_acts;
    const way_t *ways;
    int n_ways;
} state_t;

#define M(e, k) {e, k}
#define LEN(a) (int) (sizeof(a) / sizeof(a[0]))

static const pick_t LOUNGE_ACTS[] = {
    {ACT_DRIFT, 6},
    {ACT_LOOK, 3},
    {ACT_TILT, 1},
    {ACT_STRETCH, 1, {M(EMO_ENERGY, -0.6f)}},
    {ACT_SNEEZE, 0.4f},
    {ACT_HICCUP, 0.3f, {M(EMO_HAPPY, 1)}},
    {ACT_C_PEEKABOO, 0.6f, {M(EMO_HAPPY, 0.8f), M(EMO_SOCIAL, 0.5f)}},
    {ACT_C_SNEAK, 0.6f, {M(EMO_STIM, 0.5f)}},
    {ACT_C_GAZE, 0.8f, {M(EMO_SOCIAL, 0.8f)}},
};
static const pick_t WATCH_ACTS[] = {
    {ACT_C_GAZE, 5},
    {ACT_C_BLINK, 3, {M(EMO_TRUST, 1)}},
    {ACT_C_TILT, 2},
    {ACT_C_WINK, 1.5f, {M(EMO_HAPPY, 1)}},
    {ACT_C_HEARTS, 1, {M(EMO_HAPPY, 1.5f), M(EMO_TRUST, 1)}},
    {ACT_C_TRICKS, 1, {M(EMO_STIM, 1)}},
    {ACT_C_DRIFT, 2},
};
static const pick_t EXPLORE_ACTS[] = {
    {ACT_PEEK, 3},
    {ACT_SNIFF, 3},
    {ACT_FIREFLY, 2, {M(MOD_NIGHT, 0.8f)}},
    {ACT_LOOK, 2},
    {ACT_C_SCAN, 2},
    {ACT_C_SNEAK, 1.5f},
    {ACT_DRIFT, 2},
};
static const pick_t PLAY_ACTS[] = {
    {ACT_DANCE, 3},
    {ACT_LOOP, 2},
    {ACT_BOUNCE, 2},
    {ACT_FIREFLY, 1.5f, {M(MOD_NIGHT, 0.5f)}},
    {ACT_C_TRICKS, 2},
    {ACT_C_BOUNCE, 2},
    {ACT_C_STARRY, 1, {M(EMO_HAPPY, 1)}},
    {ACT_C_PEEKABOO, 1.5f},
    {ACT_HICCUP, 0.4f},
};
static const pick_t DREAM_ACTS[] = {
    {ACT_STARS, 3, {M(MOD_NIGHT, 0.8f)}},
    {ACT_HUM, 2.5f},
    {ACT_DRIFT, 3},
    {ACT_C_STARRY, 0.4f, {M(MOD_NIGHT, 1)}},
};
static const pick_t BORED_ACTS[] = {
    {ACT_SIGH, 3},
    {ACT_LOOK, 2},
    {ACT_DRIFT, 1.5f},
    {ACT_YAWN, 1, {M(EMO_ENERGY, -1)}},
    {ACT_C_SCAN, 1},
};
static const pick_t SEEK_ACTS[] = {
    {ACT_C_SEARCH, 4},
    {ACT_C_SAD, 2},
    {ACT_C_DRIFT, 1},
};
static const pick_t SULK_ACTS[] = {
    {ACT_SULK, 1},
};
static const pick_t DROWSY_ACTS[] = {
    {ACT_YAWN, 2.5f},
    {ACT_NOD, 3},
    {ACT_DRIFT, 2},
    {ACT_C_DOZE, 1.5f},
    {ACT_STRETCH, 1},
};

static const way_t LOUNGE_WAYS[] = {
    {ST_WATCH, 2, {M(EMO_SOCIAL, 1)}},
    {ST_EXPLORE, 3, {M(EMO_STIM, 0.8f), M(EMO_ENERGY, 0.8f)}},
    {ST_PLAY, 2, {M(EMO_HAPPY, 1), M(EMO_STIM, 1)}},
    {ST_DREAM, 2, {M(EMO_STIM, -0.8f), M(MOD_NIGHT, 0.6f)}},
    {ST_BORED, 1, {M(EMO_STIM, -1.2f), M(EMO_SOCIAL, -0.8f)}},
    {ST_DROWSY, 1, {M(EMO_ENERGY, -1.5f)}},
};
static const way_t WATCH_WAYS[] = {
    {ST_LOUNGE, 4},
    {ST_PLAY, 2, {M(EMO_HAPPY, 1), M(EMO_STIM, 0.8f)}},
    {ST_EXPLORE, 1},
};
static const way_t EXPLORE_WAYS[] = {
    {ST_LOUNGE, 3},
    {ST_PLAY, 2, {M(EMO_STIM, 1)}},
    {ST_DREAM, 1, {M(MOD_NIGHT, 0.6f)}},
    {ST_BORED, 1, {M(EMO_STIM, -1)}},
};
static const way_t PLAY_WAYS[] = {
    {ST_LOUNGE, 4},
    {ST_WATCH, 1, {M(EMO_SOCIAL, 1)}},
    {ST_EXPLORE, 1},
    {ST_DROWSY, 1, {M(EMO_ENERGY, -1.5f)}},
};
static const way_t DREAM_WAYS[] = {
    {ST_LOUNGE, 3},
    {ST_DROWSY, 2, {M(EMO_ENERGY, -1.5f)}},
    {ST_EXPLORE, 1, {M(EMO_ENERGY, 0.5f)}},
};
static const way_t BORED_WAYS[] = {
    {ST_LOUNGE, 2},
    {ST_SEEK, 2, {M(EMO_SOCIAL, -1.5f)}},
    {ST_EXPLORE, 2, {M(EMO_ENERGY, 0.8f)}},
    {ST_DROWSY, 2, {M(EMO_ENERGY, -1.5f)}},
    {ST_DREAM, 1},
};
static const way_t SEEK_WAYS[] = {
    {ST_SULK, 2, {M(EMO_TRUST, -1.5f), M(EMO_HAPPY, -0.5f)}},
    {ST_BORED, 2},
    {ST_LOUNGE, 2},
};
static const way_t SULK_WAYS[] = {
    {ST_LOUNGE, 3},
    {ST_BORED, 1},
};
static const way_t DROWSY_WAYS[] = {
    {ST_LOUNGE, 1, {M(EMO_ENERGY, 2)}},
    {ST_DREAM, 1},
};

#define ACTS_OF(a) a, LEN(a)

static const state_t STATES[ST_COUNT] = {
    [ST_LOUNGE] = {"lounge", {EYES_MOOD_NEUTRAL, 65, 40, 0.88f, 1.3f, 1}, false, true, 45, 120, 3000, 8000, 0,
                   ACT_COUNT, ACTS_OF(LOUNGE_ACTS), ACTS_OF(LOUNGE_WAYS)},
    [ST_WATCH] = {"watch", {EYES_MOOD_NEUTRAL, 20, 15, 1, 1, 1}, true, true, 20, 50, 800, 2500, 45, ACT_C_GAZE,
                  ACTS_OF(WATCH_ACTS), ACTS_OF(WATCH_WAYS)},
    [ST_EXPLORE] = {"explore", {EYES_MOOD_CURIOUS, 110, 70, 0.9f, 1.4f, 0.8f}, false, true, 40, 90, 1500, 5000, 60,
                    ACT_COUNT, ACTS_OF(EXPLORE_ACTS), ACTS_OF(EXPLORE_WAYS)},
    [ST_PLAY] = {"play", {EYES_MOOD_HAPPY, 80, 50, 0.85f, 1.2f, 0.6f}, false, false, 25, 55, 600, 2500, 120,
                 ACT_COUNT, ACTS_OF(PLAY_ACTS), ACTS_OF(PLAY_WAYS)},
    [ST_DREAM] = {"dream", {EYES_MOOD_DREAMY, 50, 50, 1.1f, 1.5f, 1.8f}, false, true, 45, 120, 4000, 9000, 120,
                  ACT_COUNT, ACTS_OF(DREAM_ACTS), ACTS_OF(DREAM_WAYS)},
    [ST_BORED] = {"bored", {EYES_MOOD_BORED, 45, 35, 1, 1.4f, 1.6f}, false, false, 30, 80, 4000, 9000, 60, ACT_SIGH,
                  ACTS_OF(BORED_ACTS), ACTS_OF(BORED_WAYS)},
    [ST_SEEK] = {"seek", {EYES_MOOD_SAD, 20, 15, 1, 1, 1}, true, false, 12, 25, 800, 2000, 600, ACT_C_SEARCH,
                 ACTS_OF(SEEK_ACTS), ACTS_OF(SEEK_WAYS)},
    [ST_SULK] = {"sulk", {EYES_MOOD_GRUMPY, 40, 30, 1.1f, 1.2f, 1.5f}, false, false, 30, 60, 2000, 5000, 900,
                 ACT_SULK, ACTS_OF(SULK_ACTS), ACTS_OF(SULK_WAYS)},
    [ST_DROWSY] = {"drowsy", {EYES_MOOD_SLEEPY, 35, 30, 1.1f, 1.45f, 2}, false, true, 60, 180, 4000, 9000, 60,
                   ACT_YAWN, ACTS_OF(DROWSY_ACTS), ACTS_OF(DROWSY_WAYS)},
};

/* Where the act in hand is: waiting before it, taking Yoru up to the glass
 * or back, or running. */
typedef enum {
    STAGE_GAP,
    STAGE_IN,
    STAGE_RUN,
    STAGE_OUT,
} stage_t;

/* Taking it to the glass and back mustn't hang on a swap that can't happen. */
#define SWAP_TIMEOUT_MS 2500
/* How near the ball comes before the swap. */
#define GLASS_Z 0.72f
#define RECENT 3

static struct {
    state_id_t st;
    uint32_t since;   // life clock
    uint32_t until;   // when it's over, life clock
    uint32_t left_at[ST_COUNT];
    uint32_t act_at[ACT_COUNT];
    bool ever[ACT_COUNT];
    bool st_ever[ST_COUNT];
    act_id_t recent[RECENT];
    act_id_t act;     // the act in hand
    act_id_t forced;  // debug/life's next act; ACT_COUNT none
    bool first;       // the state's entry act still to play
    stage_t stage;
    uint32_t stage_ms; // lv_tick: when the stage started
    uint32_t gap_until; // lv_tick
    int in_phase;
} D;

static float mod_value(uint8_t e)
{
    if (e == MOD_NIGHT) {
        return life_night() ? 1 : -1;
    }
    return mood((emo_t) e);
}

static float weigh(float w, const mod_t m[2])
{
    for (int i = 0; i < 2; i++) {
        if (m[i].k != 0) {
            w *= fmaxf(MOD_FLOOR, 1 + m[i].k * mod_value(m[i].e));
        }
    }
    return w;
}

static bool cooled(uint32_t at, bool ever, uint16_t cooldown_s)
{
    return !ever || LT.vnow - at >= (uint32_t) cooldown_s * 1000;
}

static bool state_ok(state_id_t to)
{
    const state_t *s = &STATES[to];
    if (LT.ctx.focus && !s->quiet) {
        return false;
    }
    return cooled(D.left_at[to], D.st_ever[to], s->cooldown_s);
}

static void enter(state_id_t to, const char *why)
{
    const state_t *s = &STATES[to];
    if (D.st != to) {
        D.left_at[D.st] = LT.vnow;
        D.st_ever[D.st] = true;
    }
    ESP_LOGI(TAG, "%s -> %s (%s)", STATES[D.st].name, s->name, why);
    D.st = to;
    D.since = LT.vnow;
    D.until = LT.vnow + (uint32_t) life_frange((float) s->dwell_lo_s, (float) s->dwell_hi_s) * 1000;
    D.first = s->enter != ACT_COUNT;
    D.stage = STAGE_GAP;
    D.gap_until = LT.now + 300;
}

/* At the end of a state: one of its ways out, by weight. */
static void move_on(void)
{
    const state_t *s = &STATES[D.st];
    float w[8] = {0}, total = 0;
    for (int i = 0; i < s->n_ways && i < 8; i++) {
        const way_t *way = &s->ways[i];
        if (way->to != D.st && state_ok(way->to)) {
            w[i] = weigh(way->w, way->m);
            total += w[i];
        }
    }
    if (total <= 0) {
        enter(ST_LOUNGE, "nowhere else");
        return;
    }
    float r = life_frange(0, total);
    for (int i = 0; i < s->n_ways && i < 8; i++) {
        if (r < w[i]) {
            enter(s->ways[i].to, "time");
            return;
        }
        r -= w[i];
    }
    enter(s->ways[s->n_ways - 1].to, "time");
}

/* The ways out that don't wait for the state to end. */
static bool urgent(void)
{
    const float energy = mood(EMO_ENERGY), social = mood(EMO_SOCIAL), stim = mood(EMO_STIM), happy = mood(EMO_HAPPY);
    if (!STATES[D.st].quiet && LT.ctx.focus) {
        enter(ST_LOUNGE, "do not disturb");
        return true;
    }
    if (energy < -0.55f && D.st != ST_DROWSY && state_ok(ST_DROWSY)) {
        enter(ST_DROWSY, "tired");
        return true;
    }
    const bool settled = D.st == ST_LOUNGE || D.st == ST_BORED || D.st == ST_DREAM || D.st == ST_EXPLORE;
    if (settled && social < -0.6f && state_ok(ST_SEEK)) {
        enter(ST_SEEK, "lonely");
        return true;
    }
    if ((D.st == ST_LOUNGE || D.st == ST_BORED) && stim > 0.65f && happy > 0.3f && energy > -0.2f
        && state_ok(ST_PLAY)) {
        enter(ST_PLAY, "excited");
        return true;
    }
    if ((D.st == ST_LOUNGE || D.st == ST_EXPLORE || D.st == ST_DREAM) && stim < -0.6f && state_ok(ST_BORED)) {
        enter(ST_BORED, "bored");
        return true;
    }
    return false;
}

static bool recent(act_id_t a)
{
    for (int i = 0; i < RECENT; i++) {
        if (D.recent[i] == a) {
            return true;
        }
    }
    return false;
}

/* The next act from the state's repertoire: by weight, not one still
 * cooling down, less likely if done just now; quiet ones under Do not
 * disturb; only the close ones in a state that lives up close. */
static act_id_t pick(void)
{
    const state_t *s = &STATES[D.st];
    if (D.forced != ACT_COUNT) {
        const act_id_t a = D.forced;
        D.forced = ACT_COUNT;
        return a;
    }
    if (D.first) {
        D.first = false;
        return s->enter;
    }
    float w[12] = {0}, total = 0;
    for (int i = 0; i < s->n_acts && i < 12; i++) {
        const pick_t *p = &s->acts[i];
        const act_info_t *info = &ACTS[p->act];
        if ((LT.ctx.focus && (info->flags & ACTF_LOUD)) || (s->close && !(info->flags & ACTF_CLOSE))
            || !cooled(D.act_at[p->act], D.ever[p->act], info->cooldown_s)) {
            continue;
        }
        w[i] = weigh(p->w, p->m) * (recent(p->act) ? 0.3f : 1);
        total += w[i];
    }
    if (total <= 0) {
        return s->close ? ACT_C_DRIFT : ACT_DRIFT;
    }
    float r = life_frange(0, total);
    for (int i = 0; i < s->n_acts && i < 12; i++) {
        if (r < w[i]) {
            return s->acts[i].act;
        }
        r -= w[i];
    }
    return s->acts[s->n_acts - 1].act;
}

static void begin(act_id_t a)
{
    D.act = a;
    D.act_at[a] = LT.vnow;
    D.ever[a] = true;
    memmove(&D.recent[1], &D.recent[0], (RECENT - 1) * sizeof(D.recent[0]));
    D.recent[0] = a;
    const bool close = ACTS[a].flags & ACTF_CLOSE;
    D.stage_ms = LT.now;
    D.in_phase = 0;
    if (close && !eyes_up_close()) {
        D.stage = STAGE_IN;
    } else if (!close && eyes_up_close()) {
        D.stage = STAGE_OUT;
    } else {
        D.stage = STAGE_RUN;
        acts_start(a, &STATES[D.st].style);
    }
    ESP_LOGD(TAG, "%s: %s", STATES[D.st].name, ACTS[a].name);
}

static void gap(void)
{
    const state_t *s = &STATES[D.st];
    D.stage = STAGE_GAP;
    D.gap_until = LT.now + (uint32_t) life_frange(s->gap_lo_ms, s->gap_hi_ms);
}

void mind_init(void)
{
    memset(&D, 0, sizeof(D));
    D.forced = ACT_COUNT;
    for (int i = 0; i < RECENT; i++) {
        D.recent[i] = ACT_COUNT;
    }
    enter(ST_LOUNGE, "start");
}

bool mind_step(void)
{
    const state_t *s = &STATES[D.st];
    switch (D.stage) {
    case STAGE_IN:
        // Up to the glass, then the views swap behind a blink.
        if (D.in_phase == 0) {
            eyes_look_at(0, -0.1f);
            // Not right up to it: a sphere that big costs half a megabyte.
            eyes_move_to(0, 10 * GLASS_Z, GLASS_Z, 600, EYES_PATH_GLIDE);
            D.in_phase = 1;
        } else if (D.in_phase == 1 && !eyes_moving()) {
            eyes_close_up(true);
            eyes_look_around();
            D.in_phase = 2;
            D.stage_ms = LT.now;
        } else if (D.in_phase == 2 && eyes_up_close()) {
            D.stage = STAGE_RUN;
            acts_start(D.act, &s->style);
        } else if (D.in_phase == 2 && LT.now - D.stage_ms > SWAP_TIMEOUT_MS) {
            gap(); // it couldn't: something else, later
        }
        return false;
    case STAGE_OUT:
        if (D.in_phase == 0) {
            eyes_close_up(false);
            D.in_phase = 1;
        } else if (!eyes_up_close() && !eyes_moving()) {
            // Back on the ball: the act it was waiting for, if any.
            if (D.act != ACT_COUNT && !(ACTS[D.act].flags & ACTF_CLOSE)) {
                D.stage = STAGE_RUN;
                acts_start(D.act, &s->style);
            } else {
                gap();
            }
        } else if (LT.now - D.stage_ms > SWAP_TIMEOUT_MS) {
            gap();
        }
        return false;
    case STAGE_RUN:
        if (!acts_step()) {
            return false;
        }
        // A one-off up close goes back to the ball at once.
        if ((ACTS[D.act].flags & ACTF_CLOSE) && !s->close) {
            D.act = ACT_COUNT;
            D.stage = STAGE_OUT;
            D.stage_ms = LT.now;
            D.in_phase = 0;
            return false;
        }
        D.act = ACT_COUNT;
        gap();
        return false;
    default:
        break;
    }

    // Between acts: time for something else?
    if ((int32_t) (LT.now - D.gap_until) < 0) {
        return false;
    }
    // So tired it wants a nap: when nobody's around and it's drowsy anyway.
    if (D.st == ST_DROWSY && LT.ctx.drowsy && mood(EMO_ENERGY) < -0.6f) {
        return true;
    }
    if (!urgent() && (int32_t) (LT.vnow - D.until) >= 0 && D.forced == ACT_COUNT) {
        move_on();
    }
    begin(pick());
    return false;
}

void mind_interrupted(resume_t resume)
{
    // Whatever it was doing is dropped; whoever took the eyes left them on
    // the ball.
    eyes_close_up(false);
    eyes_firefly(false, 0, 0);
    eyes_cross(0);
    D.act = ACT_COUNT;
    D.stage = STAGE_GAP;
    D.gap_until = LT.now + 1500;
    if (resume == RESUME_WITH && D.st != ST_WATCH) {
        enter(ST_WATCH, "you're here");
        D.gap_until = LT.now + 1500;
    } else if (resume == RESUME_WITH) {
        D.until = LT.vnow + (uint32_t) STATES[ST_WATCH].dwell_lo_s * 1000; // stays a while
    }
}

const char *mind_state(void)
{
    return STATES[D.st].name;
}

const char *mind_act(void)
{
    return D.act < ACT_COUNT ? ACTS[D.act].name : "";
}

bool mind_force_state(const char *name)
{
    for (int i = 0; i < ST_COUNT; i++) {
        if (strcmp(name, STATES[i].name) == 0) {
            D.st_ever[i] = false; // no cooldown for a test
            enter((state_id_t) i, "debug");
            return true;
        }
    }
    return false;
}

bool mind_force_act(const char *name)
{
    const act_id_t a = acts_by_name(name);
    if (a == ACT_COUNT) {
        return false;
    }
    D.forced = a;
    if (D.stage == STAGE_GAP) {
        D.gap_until = LT.now;
    }
    return true;
}

void mind_status(cJSON *out)
{
    cJSON_AddStringToObject(out, "state", STATES[D.st].name);
    cJSON_AddStringToObject(out, "act", mind_act());
    cJSON_AddNumberToObject(out, "since_s", (LT.vnow - D.since) / 1000);
    cJSON_AddNumberToObject(out, "left_s", (int32_t) (D.until - LT.vnow) > 0 ? (D.until - LT.vnow) / 1000 : 0);
    cJSON_AddBoolToObject(out, "close", eyes_up_close());
}
