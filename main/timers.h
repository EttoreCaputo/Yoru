#pragma once

/* The host's timers and reminders on the board (docs/protocol.md, Timers):
 * the host keeps them and sends the whole list with `set_timers` whenever it
 * changes; the board counts each one down from when it came, and tells the
 * host what you did with one (`timer_action`). A host that never sends the
 * list still has its snapshot's timer (metrics_model.h), the one that ends
 * first, shown here as the only one. timers_ui.c shows them; behaviour.c
 * makes Yoru jump when one rings. All calls with the LVGL lock held. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TIMERS_MAX 8
#define TIMERS_ID_MAX 23
#define TIMERS_LABEL_CHARS 27

typedef enum { TIMER_KIND_TIMER, TIMER_KIND_WORK, TIMER_KIND_BREAK, TIMER_KIND_REMINDER } timer_kind_t;
typedef enum { TIMER_RUN, TIMER_PAUSE, TIMER_RING } timer_state_t;

typedef struct {
    /* Empty for the snapshot's timer: there is nothing to send back. */
    char id[TIMERS_ID_MAX + 1];
    timer_kind_t kind;
    timer_state_t state;
    /* Now, counted down on the board. */
    float left_s;
    float total_s;
    /* Printable ASCII; "17:30" for a reminder's time, when the host says. */
    char label[TIMERS_LABEL_CHARS + 1];
    char at[6];
} timer_view_t;

/** `set_timers`' arguments; false with the reason in `err`. */
bool timers_set(const cJSON *args, char *err, size_t len);

/** How many there are now, the host's list or the snapshot's one. */
int timers_count(void);
/** The i-th, the one that ends first first, as it stands now. */
bool timers_at(int i, timer_view_t *out);
/** Changes when the list does (not with the countdown). */
uint32_t timers_version(void);

/** One started ringing since the last call: its label in `label` (may be
 * empty). For behaviour.c. */
bool timers_take_ring(char *label, size_t len);

/** `pause`, `resume`, `cancel` or `dismiss` for that id: tell the host, and
 * show it at once (the host's next list has the last word). */
void timers_act(const char *id, const char *action);

#ifdef __cplusplus
}
#endif
