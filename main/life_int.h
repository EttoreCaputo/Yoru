#pragma once

/* Yoru's own life, inside (docs/life.md). Four parts, from the slowest to
 * the quickest, after Anki's Vector:
 * - life_mood.c: five emotions moved by what happens and drifting back to
 *   where the hour, the company and its sleep leave them;
 * - life_mind.c: the state it is in (lounging, exploring, playing...), each
 *   with its look, its repertoire and its weighted ways out;
 * - life_acts.c: what it does, a step at a time;
 * - life.c: the API, and what interrupts all that (a call, a nap, sleep,
 *   coming to you, a greeting).
 * All with the LVGL lock held. */

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"
#include "eyes.h"
#include "life.h"
#include "sounds.h"

/* ---- What every part sees this tick ----------------------------------------- */

typedef struct {
    life_ctx_t ctx;
    uint32_t now;  // lv_tick: the animations
    uint32_t vnow; // the life clock: dwell, cooldowns, emotions; faster in tests
    float speed;   // how much faster (debug/life)
} life_tick_t;

extern life_tick_t LT;

float life_frange(float lo, float hi);
/* The point at distance `z` that shows (sx, sy) pixels from the middle. */
eyes_pos_t life_at(float sx, float sy, float z);
bool life_night(void);
/* A sound, unless Do not disturb keeps it quiet. */
void life_sound(sound_t sound);

/* ---- Emotions (life_mood.c) ------------------------------------------------- */

typedef enum {
    EMO_HAPPY,  // happy .. sad
    EMO_ENERGY, // lively .. tired
    EMO_SOCIAL, // in company .. lonely
    EMO_STIM,   // excited .. bored
    EMO_TRUST,  // at ease .. wary; slow, and kept across restarts
    EMO_COUNT,
} emo_t;

void mood_init(void);
void mood_step(void);
void mood_feel(life_feel_t feel);
/* -1 .. 1. */
float mood(emo_t e);
const char *mood_name(emo_t e);
const char *mood_feel_name(life_feel_t feel);
void mood_status(cJSON *out);

/* ---- What it does (life_acts.c) --------------------------------------------- */

typedef enum {
    ACT_REST, // still: only the eyes wander and blink
    ACT_DRIFT,
    ACT_LOOK,
    ACT_GAZE,
    ACT_WINK,
    ACT_TILT,
    ACT_STRETCH,
    ACT_SNEEZE,
    ACT_HICCUP,
    ACT_PEEK,
    ACT_SNIFF,
    ACT_FIREFLY,
    ACT_DANCE,
    ACT_LOOP,
    ACT_BOUNCE,
    ACT_STARS,
    ACT_HUM,
    ACT_SIGH,
    ACT_YAWN,
    ACT_NOD,
    ACT_SEARCH,
    ACT_SAD,
    ACT_SULK,
    /* The rare ones: a gesture of its own, once a milestone unlocks it
     * (growth.c). */
    ACT_WAVE,
    ACT_SHY,
    ACT_HOP,
    ACT_GIGGLE,
    ACT_CHEER,
    ACT_LOVE,
    ACT_COUNT,
} act_id_t;

#define ACTF_LOUD 1 // too lively for Do not disturb
#define ACTF_RARE 2 // only once growth.c has unlocked it

typedef struct {
    const char *name;
    uint8_t flags;
    uint16_t cooldown_s; // on the life clock
} act_info_t;

extern const act_info_t ACTS[ACT_COUNT];

/* How the state it runs in moves: its face and how far and fast it drifts. */
typedef struct {
    eyes_mood_t face;
    float roam_x, roam_y; // pixels from the middle
    float z_lo, z_hi;
    float pace;           // 1 normal, 2 twice as slow
} act_style_t;

void acts_start(act_id_t id, const act_style_t *style);
/* True when it's done. */
bool acts_step(void);
act_id_t acts_by_name(const char *name);

/* ---- The states (life_mind.c) ------------------------------------------------ */

/* Where to pick up after an interrupt (Vector's resume suggestions). */
typedef enum {
    RESUME_ANY,  // as the weights say
    RESUME_WITH, // someone is here: with you
    RESUME_HOLD, // where it was
} resume_t;

void mind_init(void);
/* True: so tired it wants a nap. */
bool mind_step(void);
/* Something else had the eyes: the act in hand is dropped. */
void mind_interrupted(resume_t resume);
const char *mind_state(void);
const char *mind_act(void);
bool mind_force_state(const char *name);
bool mind_force_act(const char *name);
void mind_status(cJSON *out);
