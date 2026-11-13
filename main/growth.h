#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Growing up (docs/CONCEPT.md): Yoru Hub counts a few real things from its
 * memory (set_growth) and each milestone unlocks one thing here, for good: a
 * colour for the sphere (look.c) or a gesture Yoru makes on its own (a rare
 * act, life_acts.c). Nothing that was there before is locked. Kept in NVS,
 * so what is unlocked stays without the computer.
 */

typedef enum {
    GROWTH_DAYS,   // days talked together
    GROWTH_FACTS,  // things learnt about the user
    GROWTH_PEOPLE, // people known by name
    GROWTH_USEFUL, // notifications called useful
    GROWTH_MEASURES,
} growth_measure_t;

void growth_init(void);

/** The host's counts. Returns the milestones reached for the first time
 * (bits, as growth_milestone_*), and celebrates them once Yoru is free. */
uint32_t growth_update(const int32_t counts[GROWTH_MEASURES]);

/** Each milestone: its measure, how many it needs, and what it unlocks. */
int growth_milestones(void);
growth_measure_t growth_milestone_measure(int i);
int32_t growth_milestone_need(int i);
const char *growth_milestone_reward(int i, bool italian);
/** As sent with set_growth's result: "sphere:dawn", "act:wave". */
const char *growth_milestone_id(int i);
bool growth_reached(int i);

/** The last counts, and the next milestone of a measure (-1: none left). */
int32_t growth_count(growth_measure_t m);
int32_t growth_next(growth_measure_t m);
int growth_reached_count(void);

/** Whether palette `i` (look.c) or a rare act (life_int.h) is unlocked. */
bool growth_palette_ok(int palette);
bool growth_act_ok(int act);

/** What was just unlocked, as a line for the toast, once; NULL: nothing
 * new. On LVGL's task (behaviour.c, when Yoru is awake and free). */
const char *growth_take_news(void);

#ifdef __cplusplus
}
#endif
