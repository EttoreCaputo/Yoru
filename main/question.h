#pragma once

/* An agent's question with its choices on the board (docs/protocol.md,
 * Questions), as Claude Code's AskUserQuestion asks it: one question at a
 * time, its options as pills to tap (toggles and Send when several may be
 * picked), and a small "Terminal" to give up and answer there. A ring along
 * the rim runs the time down. Once the last one is answered the picks go to
 * the host with the `answer` notification. One at a time: a new one
 * replaces it. All calls with the LVGL lock held. */

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QUESTION_ID_MAX 47
#define QUESTION_MAX 4
#define QUESTION_OPTIONS_MAX 4
/* The protocol's limits, in characters. */
#define QUESTION_HEADER_CHARS 12
#define QUESTION_TEXT_CHARS 120
#define QUESTION_OPTION_CHARS 40

typedef struct {
    char header[QUESTION_HEADER_CHARS + 1];
    char text[QUESTION_TEXT_CHARS + 1];
    bool multi;
    int n;
    char options[QUESTION_OPTIONS_MAX][QUESTION_OPTION_CHARS + 1];
} question_t;

/** Told when a card comes up and goes: `answered` 1 when it was answered
 * here, 0 when sent to the terminal, -1 for none (taken away, timed out,
 * replaced). */
typedef void (*question_cb_t)(bool open, int answered);

void question_init(question_cb_t cb);

/** Show `n` questions for `seconds`, replacing the card up. The texts are
 * ASCII already (card_ascii()). */
void question_show(const char *id, const char *project, int seconds, const question_t *questions, int n);

/** Take the card away if it shows `id`; false if it didn't. */
bool question_hide(const char *id);

bool question_is_open(void);

#ifdef __cplusplus
}
#endif
