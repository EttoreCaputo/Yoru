#pragma once

/* What Yoru needs, the way a Tamagotchi or a Sims character does: each a
 * level from 0 (badly missing) to 1 (fully met) that runs down with time and
 * with what it does, and goes up when the need is met. They drive its life
 * (life.c picks what to do by them) and its mood (pet.c: a well cared-for
 * Yoru is happier). Kept in NVS across reboots. Nothing kills it: left
 * alone it feeds and amuses itself, only less happily. Safe from any task. */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEED_REST,      // sleep: drains awake, faster when busy or at night
    NEED_FOOD,      // fireflies: drains slowly, a snack fills it a third
    NEED_FUN,       // play: drains when nothing happens
    NEED_COMPANY,   // you: touch, talk, being at the computer
    NEED_CURIOSITY, // something new: exploring, sounds, what the computer does
    NEED_COUNT,
} need_t;

/* What it is doing, for how fast the needs run down. */
typedef enum {
    NEEDS_CALM,   // floating about, daydreaming, sulking
    NEEDS_BUSY,   // exploring, hunting, asking for you
    NEEDS_PLAY,   // playing
    NEEDS_ASLEEP,
} needs_doing_t;

typedef struct {
    float level[NEED_COUNT];
} needs_t;

/** Load the levels saved in NVS. */
void needs_init(void);

/** `dt` seconds on (pet.c, once a second): someone is around, it is night
 * by the host's clock, music is playing. Saves now and then. */
void needs_tick(float dt, bool company, bool night, bool music);

void needs_doing(needs_doing_t doing);
needs_doing_t needs_doing_now(void);

/** The need is met by `amount` (negative: it gets worse). */
void needs_meet(need_t need, float amount);

float needs_get(need_t need);
void needs_get_all(needs_t *out);
/** How well it is cared for overall, 0..1. */
float needs_wellbeing(void);
const char *needs_name(need_t need);
/** The most pressing need, and how pressing (1 - its level). */
need_t needs_worst(float *urgency);

#ifdef __cplusplus
}
#endif
