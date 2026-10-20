#pragma once

/* The inbox (docs/protocol.md, Inbox): what the host thinks is worth
 * knowing, from your mail, calendar, issues... The host is the source of
 * truth; the board keeps the latest INBOX_MAX, newest first, in PSRAM, says
 * which are unread, and tells the host what you did with one
 * (`notification_action`). How loudly one arrives depends on its urgency;
 * inbox_ui.c shows them and behaviour.c makes Yoru come to the glass.
 * All calls with the LVGL lock held. */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INBOX_MAX 30
#define INBOX_ID_MAX 47
#define INBOX_SOURCE_CHARS 24
#define INBOX_TITLE_CHARS 60
#define INBOX_TEXT_CHARS 200
#define INBOX_ACTIONS_MAX 2
#define INBOX_ACTION_CHARS 16

/* In order: a later one is louder. */
typedef enum { INBOX_LOW, INBOX_NORMAL, INBOX_HIGH, INBOX_URGENT, INBOX_NONE = -1 } inbox_urgency_t;

typedef struct {
    char id[INBOX_ID_MAX + 1];
    /* Printable ASCII (card_ascii), cut to fit. */
    char source[INBOX_SOURCE_CHARS + 1];
    char title[INBOX_TITLE_CHARS + 1];
    char text[INBOX_TEXT_CHARS + 1];
    char actions[INBOX_ACTIONS_MAX][INBOX_ACTION_CHARS + 1];
    int nactions;
    inbox_urgency_t urgency;
    bool unread;
    /* Seconds of uptime when it reached the board, for its age. */
    uint32_t arrived_s;
} inbox_item_t;

/** `low`, `normal`, `high`, `urgent`; false for anything else. */
bool inbox_urgency_from_name(const char *name, inbox_urgency_t *out);

/** Add `item` (UTF-8 fields, made ASCII here) on top, replacing one with the
 * same id. Returns how loudly it arrives: its urgency, `low` under do not
 * disturb or the agents' quiet unless it is urgent, and INBOX_NONE when it
 * was already there as it is (a host sending its inbox again). */
inbox_urgency_t inbox_add(const char *id, const char *source, const char *title, const char *text,
                          const char *const *actions, int nactions, inbox_urgency_t urgency);

/** Take it away; false if there was none. */
bool inbox_remove(const char *id);
void inbox_clear(void);

int inbox_count(void);
/** How many are unread, and the most urgent of them (INBOX_NONE for none). */
int inbox_unread(inbox_urgency_t *top);
/** The i-th, newest first; NULL past the end. Valid until the next change. */
const inbox_item_t *inbox_at(int i);
const inbox_item_t *inbox_find(const char *id);
/** Changes with every add, removal and read. */
uint32_t inbox_version(void);

/** The loudest arrival since the last call (INBOX_NONE: none), for
 * behaviour.c. */
inbox_urgency_t inbox_take_alert(void);

/** The user did something with it: tell the host. `read` (its detail was
 * opened) marks it read and is sent once; `dismiss` takes it away; `open`
 * and an action's index ("0", "1") leave it as it is, read. */
void inbox_act(const char *id, const char *action);

/** Uptime in seconds, the clock of `arrived_s`. */
uint32_t inbox_now_s(void);

#ifdef __cplusplus
}
#endif
