#pragma once

/* What the coding agents on the computer are doing, as the host last said
 * with `set_agents` (docs/protocol.md, Agents): up to six sessions, the most
 * urgent first, each with its state, task and last steps, plus the host's
 * "do not disturb agents" and whether it can bring a terminal to the front.
 * Without news for 30 s the sessions are forgotten. The board shows them
 * (agents_ui.c) and Yoru reacts to the changes (behaviour.c). Safe from any
 * task. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGENTS_MAX 6
#define AGENTS_STEPS 3
#define AGENTS_ID_MAX 15

typedef enum {
    AGENT_IDLE,  // started, nothing asked yet
    AGENT_WORK,  // a turn is running
    AGENT_WAIT,  // it needs you: a permission or a question
    AGENT_DONE,  // the turn ended
    AGENT_ERROR, // the last turn failed
} agent_state_t;

typedef struct {
    char kind[8];  // read, edit, write, run, search, web, agent, ask, other
    char text[49]; // ASCII already
    int16_t add, del; // lines added and removed by an edit, -1 for none
    int8_t ok;        // 1 fine, 0 failed, -1 not said
} agent_step_t;

typedef struct {
    char id[AGENTS_ID_MAX + 1];
    char agent[12];
    char project[41];
    char task[73];
    agent_state_t state;
    /* Seconds since the state began, when the host sent it. */
    int since_s;
    int nsteps;
    agent_step_t steps[AGENTS_STEPS]; // oldest first
} agent_session_t;

typedef struct {
    /* False before the host's first word, or 30 s after its last. */
    bool valid;
    int n;
    agent_session_t sessions[AGENTS_MAX];
    int quiet_min;
    bool focus;
    /* When it came, in ms since boot: add to since_s for the age now. */
    uint32_t at_ms;
} agents_t;

/* What changed with the last `set_agents`, for Yoru to react to. */
typedef enum {
    AGENTS_EV_WAIT = 1 << 0,  // a session started waiting for you
    AGENTS_EV_DONE = 1 << 1,  // a turn ended well
    AGENTS_EV_ERROR = 1 << 2, // a turn failed
    AGENTS_EV_QUIET_ON = 1 << 3,
    AGENTS_EV_QUIET_OFF = 1 << 4,
} agents_event_t;

/* The few things behaviour.c and the BOOT button need, without the copy. */
typedef struct {
    int n, working, waiting, errors;
    int quiet_min;
    bool focus;
    /* The session waiting the longest (the first in the list), or "". */
    char wait_id[AGENTS_ID_MAX + 1];
} agents_summary_t;

void agents_init(void);

/** `set_agents`' arguments; false with why in `err` when they're wrong. */
bool agents_set(const cJSON *args, char *err, size_t len);

/** A copy of the last sessions. */
void agents_get(agents_t *out);
void agents_summary(agents_summary_t *out);

/** Goes up whenever what agents_get() returns changes (going stale too). */
uint32_t agents_version(void);

/** The agents_event_t bits since the last call. */
uint32_t agents_take_events(void);

const char *agents_state_name(agent_state_t state);

#ifdef __cplusplus
}
#endif
