#pragma once
/* What Yoru does when nothing else has its attention (docs/idle.md): now and
 * then one of its animations, picked at random, and otherwise still, the eyes
 * wandering and blinking on their own. Over that, what interrupts it:
 * something important for you (it comes up to the glass and knocks), a
 * greeting when you're back, a nap when nobody is around, sleep.
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
    /* Nobody for the "sleepy after" setting, or the battery low: sleepy
     * eyes, slower. */
    bool drowsy;
    /* Nobody for the "asleep after" setting: time for a nap. */
    bool away;
    /* Since someone was last around: touched, talked, moved the case, used
     * the computer. */
    uint32_t alone_ms;
    /* The minute of the day and the day, -1 while the time isn't known. */
    int minute;
    int32_t day;
} idle_ctx_t;

typedef enum {
    IDLE_NOTHING,
    IDLE_SLEEP, // it has settled down to sleep
} idle_want_t;

void idle_init(void);
/** Every 100 ms. */
idle_want_t idle_step(const idle_ctx_t *ctx);
/** Someone touched it, called it or a reaction plays: whatever it was doing
 * stops, and it comes back to the middle. */
void idle_attend(void);
/** It fell asleep (any way), or woke up. */
void idle_slept(void);
void idle_woke(void);
/** Something important waits for you: it comes up to the glass, knocks and
 * points. False if it can't now (asleep, settling down, already asking). */
bool idle_call(void);
/** It is at the glass for you right now: a touch means "I'm coming". */
bool idle_calling(void);

/** What it is doing, for get_state: idle, drowsy, or what interrupted it
 * (call, nap, sleep, come, greet). */
const char *idle_activity(void);

#ifdef __cplusplus
}
#endif
