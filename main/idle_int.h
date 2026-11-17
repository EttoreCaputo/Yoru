#pragma once

/* Inside the idle module (docs/idle.md), in two parts:
 * - idle_acts.c: the animations it plays on its own, a step at a time;
 * - idle.c: the API, the picker that chooses the next one, and what
 *   interrupts it (a call, a nap, sleep, coming to you, a greeting).
 * All with the LVGL lock held. */

#include <stdbool.h>
#include <stdint.h>

#include "eyes.h"
#include "idle.h"
#include "sounds.h"

/* ---- What every part sees this tick ----------------------------------------- */

typedef struct {
    idle_ctx_t ctx;
    uint32_t now; // lv_tick
} idle_tick_t;

extern idle_tick_t IT;

float idle_frange(float lo, float hi);
/* The point at distance `z` that shows (sx, sy) pixels from the middle. */
eyes_pos_t idle_at(float sx, float sy, float z);
bool idle_night(void);
/* A sound, unless Do not disturb keeps it quiet. */
void idle_sound(sound_t sound);

/* ---- The acts (idle_acts.c) -------------------------------------------------- */

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
    ACT_YAWN,
    ACT_NOD,
    ACT_WAVE,
    ACT_SHY,
    ACT_HOP,
    ACT_GIGGLE,
    ACT_CHEER,
    ACT_LOVE,
    ACT_COUNT,
} act_id_t;

#define ACTF_LOUD 1 // too lively for Do not disturb

typedef struct {
    const char *name;
    uint8_t flags;
    uint16_t cooldown_s;
} act_info_t;

extern const act_info_t ACTS[ACT_COUNT];

/* How it moves while idle: its face and how far and fast it drifts. */
typedef struct {
    eyes_mood_t face;
    float roam_x, roam_y; // pixels from the middle
    float z_lo, z_hi;
    float pace;           // 1 normal, 2 twice as slow
} act_style_t;

void acts_start(act_id_t id, const act_style_t *style);
/* True when it's done. */
bool acts_step(void);
