#pragma once
/* What Yoru does when nothing else has its attention: it floats about near
 * you, gets sleepy when nobody is around and drifts off to sleep; when a
 * coding agent waits for you it comes up to the glass, knocks and points at
 * the computer. No needs and no moods of its own for now: the pet comes back
 * later (docs/ROADMAP.md).
 * It only moves the eyes (eyes.c) and plays its sounds; behaviour.c says when
 * it may, and puts it to sleep when it asks. All calls with the LVGL lock
 * held. */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Nothing else has the eyes: no conversation, menu, request, reaction,
     * finger on the screen. */
    bool free;
    bool asleep;
    /* Do not disturb, or an agent waits: it keeps still and quiet. */
    bool focus;
    /* Nobody for the "sleepy after" setting: sleepy eyes, slower. */
    bool drowsy;
    /* Nobody for the "asleep after" setting: time for a nap. */
    bool lonely;
    /* Its sounds for the coding agents are on (and not quiet). */
    bool agent_sounds;
} life_ctx_t;

typedef enum {
    LIFE_NOTHING,
    LIFE_SLEEP, // it has settled down to sleep
} life_want_t;

void life_init(void);
/** Every 100 ms. */
life_want_t life_step(const life_ctx_t *ctx);
/** Someone touched it, called it or a reaction plays: whatever it was doing
 * stops, and it comes back to the middle. */
void life_attend(void);
/** It fell asleep (any way), or woke up. */
void life_slept(void);
void life_woke(void);
/** A coding agent waits for you: it comes up to the glass, knocks and
 * points. False if it can't now (asleep, settling down, already asking). */
bool life_call(void);
/** It is at the glass for an agent right now: a touch means "I'm coming". */
bool life_calling(void);
/** What it is doing (lounge, call, nap, sleep, come), for get_state. */
const char *life_activity(void);

#ifdef __cplusplus
}
#endif
