#pragma once

/* Your habits for today (docs/protocol.md, Habits): the host keeps them and
 * starts each day afresh; it sends the whole list with `set_habits`. A tap on
 * the board ticks one off (`habit_check` tells the host the new count), so a
 * glass of water or a walk is one touch away. habits_ui.c shows them. All
 * calls with the LVGL lock held. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HABITS_MAX 8
#define HABITS_ID_MAX 23
#define HABITS_NAME_CHARS 24
#define HABITS_GOAL_MAX 99

typedef struct {
    char id[HABITS_ID_MAX + 1];
    /* Printable ASCII. */
    char name[HABITS_NAME_CHARS + 1];
    int done, goal;
    /* Days in a row it was done, today not counted until it is. */
    int streak;
    uint32_t rgb;
} habit_t;

/** `set_habits`' arguments; false with the reason in `err`. */
bool habits_set(const cJSON *args, char *err, size_t len);

int habits_count(void);
const habit_t *habits_at(int i);
/** Changes with every list and every tick. */
uint32_t habits_version(void);
/** How far today is, 0..1 over all of them (each counts once, up to its goal). */
float habits_progress(void);
/** How many are done today. */
int habits_done(void);

/** One more (+1) or one less (-1) for the i-th, within 0..goal: tell the
 * host. Returns the new count, or -1 when nothing changed. */
int habits_tick(int i, int delta);

#ifdef __cplusplus
}
#endif
