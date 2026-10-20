#pragma once

/* Yoru as a remote for the agents (docs/protocol.md, Remote): what the host
 * last said about where a new session can start (`set_projects`) and about
 * the chat open on the board (`set_chat`), and the `chat` notification that
 * goes back. The texts are kept as ASCII, ready for the built-in fonts. Safe
 * from any task. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REMOTE_AGENTS 4
#define REMOTE_PROJECTS 8
#define REMOTE_ID_MAX 15
#define REMOTE_NAME_CHARS 40
#define REMOTE_AGENT_CHARS 11
/* The chat: the latest messages, each at most this long. */
#define REMOTE_MESSAGES 12
#define REMOTE_MESSAGE_CHARS 1200

typedef struct {
    char id[REMOTE_ID_MAX + 1];
    char name[REMOTE_NAME_CHARS + 1];
} remote_project_t;

typedef struct {
    /* False until the host's first `set_projects`. */
    bool valid;
    int nagents;
    char agents[REMOTE_AGENTS][REMOTE_AGENT_CHARS + 1];
    int nprojects;
    remote_project_t projects[REMOTE_PROJECTS]; // the most recent first
} remote_projects_t;

typedef struct {
    bool you; // said by you, else by the agent
    char text[REMOTE_MESSAGE_CHARS + 1];
} remote_message_t;

typedef struct {
    char session[REMOTE_ID_MAX + 1]; // "" before the host said anything
    int n;
    remote_message_t messages[REMOTE_MESSAGES]; // the oldest first
} remote_chat_t;

void remote_init(void);

/** `set_projects`' and `set_chat`'s arguments; false with why in `err`. */
bool remote_set_projects(const cJSON *args, char *err, size_t len);
bool remote_set_chat(const cJSON *args, char *err, size_t len);

void remote_get_projects(remote_projects_t *out);
void remote_get_chat(remote_chat_t *out);

/** Go up whenever what the getters return changes. */
uint32_t remote_projects_version(void);
uint32_t remote_chat_version(void);

/** Tell the host a session's chat opened or closed (`chat`). */
void remote_chat_open(const char *session, bool open);

#ifdef __cplusplus
}
#endif
