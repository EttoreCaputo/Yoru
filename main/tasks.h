#pragma once

/* The tasks the user gave Yoru (docs/protocol.md, Tasks): jobs the host's
 * assistant does on its own, once or on a schedule. The host keeps and runs
 * them; it sends the whole list with `set_tasks` whenever one changes, and
 * the board only shows them (tasks_ui.c). "Do it now" on the board asks the
 * host with `task_run`. The last list is kept in NVS, so it still shows
 * without the host. All calls with the LVGL lock held. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* set_tasks fits in one frame (4 KB) with this many. */
#define TASKS_MAX 8
#define TASKS_ID_MAX 39
#define TASKS_NAME_CHARS 32
#define TASKS_TEXT_CHARS 100

typedef enum { TASK_ONCE, TASK_DAILY, TASK_MONTHLY, TASK_HOURLY } task_repeat_t;
typedef enum { TASK_READS, TASK_ASKS, TASK_ACTS } task_acting_t;

typedef struct {
    char id[TASKS_ID_MAX + 1];
    /* Printable ASCII, as the fonts are. */
    char name[TASKS_NAME_CHARS + 1];
    char what[TASKS_TEXT_CHARS + 1];
    /* What Yoru said he did, last time. */
    char result[TASKS_TEXT_CHARS + 1];
    uint8_t repeat, acting;
    /* "HH:MM"; for once, `date` is "YYYY-MM-DD". */
    char at[6], date[11];
    /* Daily: bit 0 Monday .. bit 6 Sunday; 0 every day. */
    uint8_t days;
    /* Monthly: the day; hourly: the hours. */
    uint8_t day, every;
    /* Unix seconds; 0 for none. */
    int64_t next, last;
    bool enabled, failed, running;
} task_t;

/** Load the list kept in NVS (NVS must be up). */
void tasks_init(void);

/** `set_tasks`' arguments; false with the reason in `err`. */
bool tasks_set(const cJSON *args, char *err, size_t len);

int tasks_count(void);
const task_t *tasks_at(int i);
/** Changes with every list. */
uint32_t tasks_version(void);
/** How many failed last time. */
int tasks_failed(void);

/** When it runs, in words ("Mon, Wed at 09:00"). */
void tasks_when(const task_t *t, char *out, size_t len);

/** Ask the host to do the i-th now (`task_run`); false without a host. */
bool tasks_run(int i);

#ifdef __cplusplus
}
#endif
