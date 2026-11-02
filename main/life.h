#pragma once
/* What Yoru does when nothing else has its attention: it floats about near
 * you, gets sleepy when nobody is around and drifts off to sleep; when
 * something important arrives for you it comes up to the glass and knocks.
 * Now and then it has a whim of its own (peeks out past the edge, stretches,
 * dances, gazes up at the sky it came from, loops, sneezes), greets you when
 * you come back after a while and in the morning, and has a cheer that your
 * attention raises and time alone wears down: cheerful it dances and loops
 * more, glum it looks bored and comes to look for you. The needs and the
 * fireflies come later (docs/ROADMAP.md).
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
    /* Do not disturb: it keeps still and quiet. */
    bool focus;
    /* Nobody for the "sleepy after" setting: sleepy eyes, slower. */
    bool drowsy;
    /* Nobody for the "asleep after" setting: time for a nap. */
    bool lonely;
    /* Since someone was last around: touched, talked, moved the case, used
     * the computer. */
    uint32_t alone_ms;
    /* The minute of the day and the day, -1 while the time isn't known. */
    int minute;
    int32_t day;
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
/** Something important waits for you: it comes up to the glass, knocks and
 * points. False if it can't now (asleep, settling down, already asking). */
bool life_call(void);
/** It is at the glass for you right now: a touch means "I'm coming". */
bool life_calling(void);
/** What it is doing (lounge, call, nap, sleep, come, whim, greet), for
 * get_state. */
const char *life_activity(void);
/** Its cheer, 0 (glum) to 1 (delighted), for get_state. */
float life_cheer(void);

#ifdef __cplusplus
}
#endif
