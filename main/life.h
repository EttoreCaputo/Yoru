#pragma once

/* Yoru's own life, when nothing else has its attention: what it does next
 * comes from what it needs (needs.c) and how it feels (pet.c), not from a
 * dice roll. Each thing it can do scores by how well it meets the pressing
 * needs, the way a Sims character picks; it keeps at it for a while, then
 * picks again. Hungry, it hunts a firefly in the dark, or comes up to the
 * glass to ask you for one; bored, it plays (hops along the bottom of the
 * screen, hides past the edge and peeks back, loops through the space);
 * curious, it goes exploring into the distance; lonely, it knocks on the
 * glass; tired, it yawns, finds a corner and drifts off to sleep, and wakes
 * once it is rested. Unhappy, it sulks by the edge. A tap on the black
 * drops a firefly there.
 * It only moves the eyes (eyes.c) and plays the pet's sounds; behaviour.c
 * says when it may, and puts it to sleep or wakes it when it asks. All
 * calls with the LVGL lock held. */

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
    /* Do not disturb: it keeps to itself, quietly. */
    bool focus;
    /* Someone is around (pet_alone_s() short). */
    bool company;
    /* Nobody for the "sleepy after" setting: only calm things. */
    bool drowsy;
    /* Nobody for the "asleep after" setting: time for a nap. */
    bool lonely;
    /* Night by the host's clock. */
    bool night;
    bool music;
    /* Its sounds for the coding agents are on (and not quiet). */
    bool agent_sounds;
} life_ctx_t;

typedef enum {
    LIFE_NOTHING,
    LIFE_SLEEP, // it has settled down to sleep
    LIFE_WAKE,  // rested, it wakes up on its own
} life_want_t;

void life_init(void);

/** Every 100 ms. */
life_want_t life_step(const life_ctx_t *ctx);

/** Someone touched it, called it or a reaction plays: whatever it was doing
 * stops, and if it was far away it comes back. */
void life_attend(void);

/** It fell asleep (any way), or woke up: `natural` when it woke by itself. */
void life_slept(void);
void life_woke(bool natural);

/** A tap on the black at (x, y), not on Yoru: a firefly drops there. False
 * if it can't now. */
bool life_tap(int32_t x, int32_t y);

/** A snack from the menu or the host: a firefly comes in from the edge.
 * False if it can't now (asleep, busy). */
bool life_feed(void);

/** A coding agent waits for you: it comes up to the glass, knocks and
 * points, as when it asks for itself. False if it can't now (asleep,
 * eating, already asking). */
bool life_call(void);

/** It is at the glass for an agent right now: a touch means "I'm coming". */
bool life_calling(void);

/** What it is doing, for get_pet. */
const char *life_activity(void);

#ifdef __cplusplus
}
#endif
