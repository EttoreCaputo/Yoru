#pragma once
/* What Yoru does when nothing else has its attention (docs/life.md): a life
 * of its own, after Anki's Vector. Five emotions (happy, energy, company,
 * excitement, trust) that what happens pushes and time brings back; a state
 * it is in (lounging, with you up at the glass, exploring, playing,
 * dreaming, bored, looking for you, sulking, drowsy), each with its acts and
 * weighted ways on; acts on the ball or up close, where only its eyes show.
 * Over that, what interrupts it: something important for you (it comes up
 * to the glass and knocks), a greeting when you're back, a nap when nobody
 * is around, sleep.
 * It only moves the eyes (eyes.c) and plays its sounds; behaviour.c says when
 * it may, tells it what happens, and puts it to sleep when it asks. All
 * calls with the LVGL lock held, but life_debug(). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

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

/* What happens to it, for the emotions (life_mood.c has what each does). */
typedef enum {
    LIFE_FEEL_POKE,
    LIFE_FEEL_STROKE,
    LIFE_FEEL_HOLD,    // your finger in its hands
    LIFE_FEEL_TALK,    // a conversation
    LIFE_FEEL_SLAP,
    LIFE_FEEL_SHAKE,
    LIFE_FEEL_DROP,
    LIFE_FEEL_LIFT,
    LIFE_FEEL_PLAYED,  // a game with you
    LIFE_FEEL_TIMER,   // a timer rang
    LIFE_FEEL_TASK,    // a task of his done
    LIFE_FEEL_WOKE,
    LIFE_FEEL_GREETED,
    LIFE_FEEL_IGNORED, // it called and nobody came
    LIFE_FEEL_FUN,     // it had fun on its own
    LIFE_FEEL_SOUND,   // a sudden noise
    LIFE_FEEL_PLUGGED,
    LIFE_FEEL_FRIEND,  // another Yoru came by
    LIFE_FEEL_FRIEND_ASLEEP, // the other Yoru fell asleep
    LIFE_FEEL_JEALOUS, // you stroke the other Yoru
    LIFE_FEEL_COUNT,
} life_feel_t;

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
/** Join in with another Yoru (behaviour.c): an act (`yawn`, `dance`...)
 * now, over what it was doing on its own, without telling the friends
 * again. False if it isn't living its own life now (a call, a nap...) or
 * the name is unknown. */
bool life_join(const char *act);
/** The other Yoru's firefly flew off towards it: one comes in from the edge
 * at (x, y), a unit vector in the picture's axes, `along` (-1..1) along
 * that edge. False as life_join(). */
bool life_firefly_from(float x, float y, float along);

/** Something happened to it: its emotions move. */
void life_feel(life_feel_t feel);
/** What it is doing: its state (lounge, watch, explore, play, dream, bored,
 * seek, sulk, drowsy) or what interrupted it (call, nap, sleep, come,
 * greet), for get_state. */
const char *life_activity(void);
/** Its cheer, 0 (glum) to 1 (delighted), for get_state. */
float life_cheer(void);
/** The state, the act, the emotions: get_state's `life`. */
cJSON *life_status(void);
/** debug/life: {"state"}, {"act"}, {"feel"}, {"speed"} (the life clock up to
 * 300 times faster). Takes the LVGL lock. False, with why in `err`, for an
 * unknown name. */
bool life_debug(const cJSON *params, char *err, size_t len);

#ifdef __cplusplus
}
#endif
