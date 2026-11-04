#pragma once

/* Other Yorus nearby (docs/FRIENDS.md): every few seconds each board says
 * hello with ESP-NOW on its WiFi's channel, and keeps who it heard. Over the
 * radio the WiFi already has on, so it costs no RAM worth counting and needs
 * neither the host nor Bluetooth. A board met for the first time, or back
 * after a long while, is a friend to greet (behaviour.c).
 *
 * Besides the hello they tell each other what they do (a yawn, falling
 * asleep, being stroked...), so the other can join in; bringing the two
 * together (the signal jumps) is a meeting, and a tap shows which side the
 * other is on; a ball crosses between their
 * screens in a game; and when both hear the wake word, only one answers. */

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The WiFi's radio came up with an address, or stopped (net.c, from the
 * event loop): ESP-NOW starts and stops with it. */
void friends_radio(bool on);

/** Say hello and listen, while the WiFi has an address: on at boot,
 * `debug/friends` switches it until the next restart (docs/FRIENDS.md). */
void friends_enable(bool on);
bool friends_enabled(void);

/** Every tick (behaviour.c, LVGL lock held): `awake` goes into our hello.
 * `moving`: the case is being moved (hellos come faster).
 * Returns true once for a friend just met, or back after a long while,
 * that Yoru should greet now; only while `free` (nothing else has the
 * eyes): until then the friend waits. */
bool friends_tick(bool awake, bool free, bool moving);

/** How many friends are in range now; how many of them are awake. */
int friends_count(void);
int friends_awake(void);

/** get_state's `friends`: `[{id, rssi, seen_s, awake, side?}]`. */
cJSON *friends_status(void);

/* ---- What they tell each other ------------------------------------------ */

typedef enum {
    FRIEND_YAWN,    // it yawned (on its own)
    FRIEND_SNEEZE,
    FRIEND_HICCUP,
    FRIEND_DANCE,
    FRIEND_ASLEEP,  // it fell asleep
    FRIEND_WOKE,    // someone woke it
    FRIEND_STROKED, // someone is stroking it, or holds its hand
    FRIEND_TALK,    // a conversation started
    FRIEND_CHAT,    // a turn of their chatter: a = turn, b = turns in all
    FRIEND_FIREFLY, // its firefly flew off towards us: a = height, -1000..1000
    // Only from friends.c, never sent as events:
    FRIEND_BUMP,    // we were brought together, up close
    FRIEND_SIDED,   // the friend found where we are: so we know where it is
    FRIEND_EV_COUNT,
} friend_ev_kind_t;

typedef struct {
    friend_ev_kind_t kind;
    int16_t a, b;
    /* Where that friend is, in the picture's axes (x right, y down), when
     * a bump told: a unit vector. */
    bool sided;
    float side_x, side_y;
} friend_ev_t;

/** Tell the friends around (nothing without the radio). */
void friends_send(friend_ev_kind_t kind, int16_t a, int16_t b);
/** The next thing a friend told, oldest first; false when none. */
bool friends_next(friend_ev_t *ev);

/** Where the nearest friend in range is, as friend_ev_t's side: false until
 * someone showed it (friends_found_at, here or on the friend). */
bool friends_side(float *x, float *y);

/** You showed where the nearest friend is: that way, (x, y) in the
 * picture's axes (a tap on the edge facing it). The friend is told, and
 * takes the opposite way. Kept across restarts. */
void friends_found_at(float x, float y);

/** For tests (`debug/friends`): an event by its name (`yawn`, `chat`...),
 * as if this board had done it; false for an unknown one. */
bool friends_tell(const char *name);
/** For tuning (`debug/friends`): how many times they were brought together. */
void friends_bump_stats(cJSON *out);
/** For tests: say hello this often (ms) until the next restart. */
void friends_hello_every(int ms);
/** For tests: the nearest friend is that way, in degrees in the panel's
 * axes (0 right, 90 down), as a bump would have said. */
bool friends_set_side(int deg);

/* ---- The ball (game_pass.c) -------------------------------------------------- */

/** The ball game is open here: friends see it in our hello. */
void friends_playing(bool on);
/** The nearest friend has the ball game open too. */
bool friends_partner_playing(void);
/** Throw the ball to the friend: `along` where it leaves along the edge
 * facing the friend (-1..1, clockwise), `speed` in pixels a second. */
void friends_throw(float along, float speed);
/** A ball the friend threw us since the last call. */
bool friends_catch(float *along, float *speed);

/* ---- The wake word (voice.c, behaviour.c) --------------------------------- */

/** We heard the wake word this loud: the friends are told. */
void friends_heard(float volume_db);
/** A friend heard the same wake word louder: this one should leave the
 * answer to it. True once per wake word. */
bool friends_outheard(void);

#ifdef __cplusplus
}
#endif
