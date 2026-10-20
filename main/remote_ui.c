#include "remote_ui.h"

#include <stdio.h>
#include <string.h>

#include "agents_ui.h"
#include "card.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "remote.h"
#include "voice.h"

static const char *TAG = "remote_ui";

/* The page inside the circle: the title and a line under it on top, the
 * messages in a box that scrolls and fades out at its edges, the hint at the
 * bottom. */
#define TITLE_Y 22
#define TITLE_W 260
#define SUB_Y 50
#define SUB_W 280
#define BOX_Y 76
#define BOX_W 316
#define BOX_H 334
#define FADE_H 22
/* A bubble is at most this wide; yours sit right, blue, the agent's left,
 * edged in its colour. Closer together while the same side speaks. */
#define BUBBLE_W 250
#define BUBBLE_RADIUS 16
#define BUBBLE_YOU lv_color_hex(0x2A4B9B)
#define BUBBLE_AGENT lv_color_hex(0x1D1E26)
#define GAP_SAME 4
#define GAP_TURN 12
/* The "..." while the agent works steps this often. */
#define TYPING_MS 400
#define HINT_Y 420
#define HINT_W 220
/* "New": the agents in a row, the projects in a list under them. */
#define CHIPS_Y 58
#define CHIPS_W 300
#define CHIP_H 34
#define LIST_Y 104
#define LIST_W 290
#define LIST_H 290
#define ROW_H 46

typedef enum { PAGE_NONE, PAGE_CHAT, PAGE_NEW } page_t;

/* Copies for the LVGL task: the messages are KBs, so PSRAM. */
EXT_RAM_BSS_ATTR static remote_chat_t s_chat;
EXT_RAM_BSS_ATTR static remote_projects_t s_projects;

static struct {
    page_t page;
    lv_obj_t *root;
    lv_obj_t *title, *sub, *hint;
    /* The chat. */
    char session[AGENTS_ID_MAX + 1];
    lv_obj_t *box;
    lv_obj_t *typing; // the "..." bubble's label while the agent works
    uint32_t rgb;     // the agent's colour
    uint32_t chat_version;
    bool working;
    /* "New": the agent picked, and the project (-1: still the list). */
    lv_obj_t *body;
    uint32_t projects_version;
    int agent;
    int project;
} s;

static void build_new(void);

static void set_text(lv_obj_t *l, const char *text)
{
    if (strcmp(lv_label_get_text(l), text) != 0) {
        lv_label_set_text(l, text);
    }
}

static void gesture_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_RIGHT || dir == LV_DIR_BOTTOM) {
        lv_indev_wait_release(indev);
        remote_ui_close();
    }
}

static void back_cb(lv_event_t *e)
{
    remote_ui_close();
}

/* A black page over the view, with its title. */
static void page(const char *title, lv_color_t color)
{
    s.root = card_root(true);
    lv_obj_add_event_cb(s.root, gesture_cb, LV_EVENT_GESTURE, NULL);
    s.title = card_label(s.root, &lv_font_montserrat_20, color, title);
    lv_label_set_long_mode(s.title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s.title, TITLE_W);
    lv_obj_set_style_text_align(s.title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s.title, LV_ALIGN_TOP_MID, 0, TITLE_Y);
    lv_obj_add_flag(s.title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s.title, 20);
    lv_obj_add_event_cb(s.title, back_cb, LV_EVENT_CLICKED, NULL);
    s.hint = card_label(s.root, &lv_font_montserrat_14, CARD_DIM, "");
    lv_label_set_long_mode(s.hint, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s.hint, HINT_W);
    lv_obj_set_style_text_align(s.hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s.hint, LV_ALIGN_TOP_MID, 0, HINT_Y);
    lv_obj_fade_in(s.root, 150, 0);
}

/* The hint, or the host's toast while there is one: the line over the rim
 * is under the page. */
static void update_hint(const char *hint)
{
    char toast[80];
    lv_color_t color;
    if (agents_ui_toast_now(toast, sizeof(toast), &color)) {
        set_text(s.hint, toast);
        lv_obj_set_style_text_color(s.hint, color, 0);
    } else {
        set_text(s.hint, hint);
        lv_obj_set_style_text_color(s.hint, CARD_DIM, 0);
    }
}

/* ---- The chat --------------------------------------------------------------------- */

static const char *state_text(agent_state_t st)
{
    switch (st) {
    case AGENT_WORK:
        return card_tr("Working", "Al lavoro");
    case AGENT_WAIT:
        return card_tr("Waiting for you", "Ti aspetta");
    case AGENT_DONE:
        return card_tr("Done", "Fatto");
    case AGENT_ERROR:
        return card_tr("Failed", "Errore");
    default:
        return card_tr("Idle", "Inattivo");
    }
}

static const agent_session_t *find(const agents_t *agents)
{
    for (int i = 0; i < agents->n; i++) {
        if (strcmp(agents->sessions[i].id, s.session) == 0) {
            return &agents->sessions[i];
        }
    }
    return NULL;
}

/* The title and the state under it. */
static void fill_head(const agents_t *agents)
{
    const agent_session_t *a = find(agents);
    if (a == NULL) {
        set_text(s.sub, card_tr("Session ended", "Sessione finita"));
        lv_obj_set_style_text_color(s.sub, CARD_DIM, 0);
        return;
    }
    set_text(s.title, a->project[0] ? a->project : a->agent);
    char text[64];
    const int age_s = a->since_s + (int) ((lv_tick_get() - agents->at_ms) / 1000);
    if (age_s < 60) {
        snprintf(text, sizeof(text), "%s", state_text(a->state));
    } else if (age_s < 3600) {
        snprintf(text, sizeof(text), "%s  " LV_SYMBOL_BULLET "  %d min", state_text(a->state), age_s / 60);
    } else {
        snprintf(text, sizeof(text), "%s  " LV_SYMBOL_BULLET "  %d h", state_text(a->state), age_s / 3600);
    }
    set_text(s.sub, text);
    lv_obj_set_style_text_color(s.sub, agents_ui_state_color(a->state), 0);
}

/* One message: a rounded bubble, on its side of a full-width row. `gap`
 * above it. Returns its label. */
static lv_obj_t *bubble(const char *text, bool you, int32_t gap)
{
    lv_obj_t *row = lv_obj_create(s.box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(row, gap, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE); // the box scrolls under a finger on it
    lv_obj_t *b = lv_obj_create(row);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(b, BUBBLE_W, 0);
    lv_obj_set_style_radius(b, BUBBLE_RADIUS, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, you ? BUBBLE_YOU : BUBBLE_AGENT, 0);
    lv_obj_set_style_pad_hor(b, 12, 0);
    lv_obj_set_style_pad_ver(b, 8, 0);
    if (!you) {
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(b, 3, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(s.rgb), 0);
    }
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_align(b, you ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *l = card_label(b, &lv_font_montserrat_20, you ? lv_color_hex(0xFFFFFF) : CARD_TEXT, text);
    lv_obj_set_width(l, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(l, BUBBLE_W - 27, 0);
    lv_obj_set_style_text_line_space(l, 2, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    return l;
}

/* The messages, the newest at the bottom and scrolled to; "..." while the
 * agent works on an answer. */
static void fill_messages(const agents_t *agents)
{
    const agent_session_t *a = find(agents);
    lv_obj_clean(s.box);
    s.typing = NULL;
    const bool mine = strcmp(s_chat.session, s.session) == 0;
    const int n = mine ? s_chat.n : 0;
    for (int i = 0; i < n; i++) {
        const bool you = s_chat.messages[i].you;
        const int32_t gap = i == 0 ? FADE_H : you == s_chat.messages[i - 1].you ? GAP_SAME : GAP_TURN;
        bubble(s_chat.messages[i].text, you, gap);
    }
    s.working = a != NULL && a->state == AGENT_WORK;
    if (n == 0 && !s.working) {
        lv_obj_t *none = card_label(s.box, &lv_font_montserrat_20, CARD_DIM,
                                    card_tr("No messages yet", "Ancora nessun messaggio"));
        lv_obj_set_width(none, lv_pct(100));
        lv_obj_set_style_pad_top(none, BOX_H / 2 - 20, 0);
        lv_obj_set_style_text_align(none, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (s.working) {
        const bool after_agent = n > 0 && !s_chat.messages[n - 1].you;
        s.typing = bubble("...", false, n == 0 ? FADE_H : after_agent ? GAP_SAME : GAP_TURN);
        lv_obj_set_style_text_color(s.typing, CARD_DIM, 0);
    }
    // Room under the last one, past the fade.
    lv_obj_t *end = lv_obj_create(s.box);
    lv_obj_remove_style_all(end);
    lv_obj_set_size(end, 1, FADE_H);
    lv_obj_update_layout(s.box);
    lv_obj_scroll_to_y(s.box, LV_COORD_MAX, LV_ANIM_OFF);
}

/* What PWR is doing, where the hint is: the voice overlay is under the page. */
static void update_chat_hint(void)
{
    switch (voice_state()) {
    case VOICE_LISTENING:
        set_text(s.hint, card_tr(LV_SYMBOL_AUDIO " Listening...", LV_SYMBOL_AUDIO " Ti ascolto..."));
        lv_obj_set_style_text_color(s.hint, CARD_ACCENT, 0);
        return;
    case VOICE_THINKING:
        set_text(s.hint, card_tr("Writing it down...", "Lo trascrivo..."));
        lv_obj_set_style_text_color(s.hint, CARD_ACCENT, 0);
        return;
    default:
        update_hint(card_tr(LV_SYMBOL_AUDIO "  Hold PWR to talk", LV_SYMBOL_AUDIO "  Tieni PWR e parla"));
    }
}

/* The black over the box's top and bottom edges, so the messages fade out
 * there instead of being cut. Not clickable: the box under it scrolls. */
static void fade(int32_t y, bool down)
{
    lv_obj_t *f = lv_obj_create(s.root);
    lv_obj_remove_style_all(f);
    lv_obj_set_size(f, BOX_W, FADE_H);
    lv_obj_align(f, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(f, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_color(f, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_main_opa(f, down ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(f, down ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(f, LV_GRAD_DIR_VER, 0);
    lv_obj_remove_flag(f, LV_OBJ_FLAG_CLICKABLE);
}

void remote_ui_chat_open(const char *session)
{
    remote_ui_close();
    s.page = PAGE_CHAT;
    snprintf(s.session, sizeof(s.session), "%s", session);
    agents_t *agents = heap_caps_malloc(sizeof(agents_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (agents == NULL) {
        s.page = PAGE_NONE;
        return;
    }
    agents_get(agents);
    const agent_session_t *a = find(agents);
    s.rgb = a != NULL ? agents_ui_agent_rgb(a->agent) : 0xAEAAB5;
    page("", lv_color_hex(s.rgb));

    s.sub = card_label(s.root, &lv_font_montserrat_14, CARD_DIM, "");
    lv_label_set_long_mode(s.sub, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s.sub, SUB_W);
    lv_obj_set_style_text_align(s.sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s.sub, LV_ALIGN_TOP_MID, 0, SUB_Y);

    // The conversation, one bubble under the other, scrolling.
    s.box = lv_obj_create(s.root);
    lv_obj_remove_style_all(s.box);
    lv_obj_set_size(s.box, BOX_W, BOX_H);
    lv_obj_align(s.box, LV_ALIGN_TOP_MID, 0, BOX_Y);
    lv_obj_set_flex_flow(s.box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s.box, LV_DIR_VER);

    lv_obj_set_scrollbar_mode(s.box, LV_SCROLLBAR_MODE_OFF);
    fade(BOX_Y, true);
    fade(BOX_Y + BOX_H - FADE_H, false);

    remote_get_chat(&s_chat);
    s.chat_version = remote_chat_version();
    fill_head(agents);
    fill_messages(agents);
    free(agents);
    update_chat_hint();
    // The host fills it with set_chat from now on.
    remote_chat_open(s.session, true);
    ESP_LOGI(TAG, "chat %s", s.session);
}

/* ---- New ---------------------------------------------------------------------------- */

static void agent_cb(lv_event_t *e)
{
    s.agent = (int) (intptr_t) lv_event_get_user_data(e);
    build_new();
}

static void project_cb(lv_event_t *e)
{
    s.project = (int) (intptr_t) lv_event_get_user_data(e);
    build_new();
}

static void change_cb(lv_event_t *e)
{
    s.project = -1;
    build_new();
}

/* "claude" as "Claude". */
static void capitalised(const char *in, char *out, size_t len)
{
    snprintf(out, len, "%s", in);
    if (out[0] >= 'a' && out[0] <= 'z') {
        out[0] = (char) (out[0] - 32);
    }
}

static void build_new(void)
{
    if (s.body != NULL) {
        lv_obj_delete(s.body);
    }
    s.body = lv_obj_create(s.root);
    lv_obj_t *body = s.body;
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_CLICKABLE); // the page takes the swipes
    lv_obj_move_to_index(body, 0);                  // under the title and the hint

    if (!s_projects.valid || s_projects.nagents == 0 || s_projects.nprojects == 0) {
        lv_obj_t *none = card_label(body, &lv_font_montserrat_20, CARD_DIM,
                                    !s_projects.valid ? card_tr("No word from the computer", "Nessuna notizia dal PC")
                                                      : card_tr("No projects to start in", "Nessun progetto"));
        lv_obj_align(none, LV_ALIGN_CENTER, 0, 0);
        update_hint("");
        return;
    }
    if (s.agent >= s_projects.nagents) {
        s.agent = 0;
    }
    if (s.project >= s_projects.nprojects) {
        s.project = -1;
    }

    // The agents, one chip each; the one picked in its colour.
    const int n = s_projects.nagents;
    const int32_t gap = 6;
    int32_t w = (CHIPS_W - (n - 1) * gap) / n;
    w = w > 96 ? 96 : w;
    for (int i = 0; i < n; i++) {
        const bool on = i == s.agent;
        const lv_color_t c = on ? lv_color_hex(agents_ui_agent_rgb(s_projects.agents[i])) : CARD_DIM;
        char name[REMOTE_AGENT_CHARS + 1];
        capitalised(s_projects.agents[i], name, sizeof(name));
        lv_obj_t *chip = card_button(body, w, CHIP_H, c, on ? CARD_ROW_PRESSED : CARD_TRACK, name,
                                     &lv_font_montserrat_14, agent_cb, LV_EVENT_CLICKED, (void *) (intptr_t) i);
        lv_obj_align(chip, LV_ALIGN_TOP_MID, (int32_t) ((2 * i - (n - 1)) * (w + gap) / 2), CHIPS_Y);
    }

    char agent[REMOTE_AGENT_CHARS + 1];
    capitalised(s_projects.agents[s.agent], agent, sizeof(agent));
    if (s.project < 0) {
        // The projects, the most recent first.
        lv_obj_t *list = lv_obj_create(body);
        lv_obj_remove_style_all(list);
        lv_obj_set_size(list, LIST_W, LIST_H);
        lv_obj_align(list, LV_ALIGN_TOP_MID, 0, LIST_Y);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(list, 6, 0);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
        for (int i = 0; i < s_projects.nprojects; i++) {
            card_button(list, LIST_W, ROW_H, CARD_TEXT, CARD_ROW, s_projects.projects[i].name, &lv_font_montserrat_20,
                        project_cb, LV_EVENT_CLICKED, (void *) (intptr_t) i);
        }
        update_hint(card_tr("Pick a project", "Scegli un progetto"));
        return;
    }

    // Ready: say what to do.
    const lv_color_t c = lv_color_hex(agents_ui_agent_rgb(s_projects.agents[s.agent]));
    lv_obj_t *mic = card_label(body, &lv_font_montserrat_40, c, LV_SYMBOL_AUDIO);
    lv_obj_align(mic, LV_ALIGN_CENTER, 0, -50);
    char text[REMOTE_NAME_CHARS + 32];
    snprintf(text, sizeof(text), "%s %s %s", agent, card_tr("in", "in"), s_projects.projects[s.project].name);
    lv_obj_t *what = card_label(body, &lv_font_montserrat_20, CARD_TEXT, text);
    lv_label_set_long_mode(what, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(what, LIST_W);
    lv_obj_set_style_text_align(what, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(what, LV_ALIGN_CENTER, 0, 4);
    lv_obj_t *say = card_label(body, &lv_font_montserrat_14, CARD_DIM,
                               card_tr("Hold PWR and say what to do", "Tieni PWR e di' cosa fare"));
    lv_obj_align(say, LV_ALIGN_CENTER, 0, 36);
    lv_obj_t *change = card_button(body, 140, 40, CARD_DIM, CARD_TRACK, card_tr("Change", "Cambia"),
                                   &lv_font_montserrat_14, change_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(change, LV_ALIGN_CENTER, 0, 96);
    update_hint("");
}

void remote_ui_new_open(void)
{
    remote_ui_close();
    s.page = PAGE_NEW;
    s.project = -1;
    remote_get_projects(&s_projects);
    s.projects_version = remote_projects_version();
    page(card_tr("New session", "Nuova sessione"), CARD_ACCENT);
    build_new();
    ESP_LOGI(TAG, "new, %d project(s)", s_projects.nprojects);
}

/* ---- Both ----------------------------------------------------------------------------- */

void remote_ui_close(void)
{
    if (s.page == PAGE_NONE) {
        return;
    }
    if (s.page == PAGE_CHAT) {
        remote_chat_open(s.session, false);
    }
    lv_obj_delete_async(s.root);
    s.root = s.body = s.box = s.typing = NULL;
    s.page = PAGE_NONE;
}

bool remote_ui_is_open(void)
{
    return s.page != PAGE_NONE;
}

void remote_ui_target(char *out, size_t len)
{
    out[0] = '\0';
    if (s.page == PAGE_CHAT) {
        snprintf(out, len, "{\"session\":\"%s\"}", s.session);
    } else if (s.page == PAGE_NEW && s.project >= 0 && s.project < s_projects.nprojects) {
        // Ids and names came from the host as plain short strings.
        snprintf(out, len, "{\"project\":\"%s\",\"agent\":\"%s\"}", s_projects.projects[s.project].id,
                 s_projects.agents[s.agent]);
    }
}

void remote_ui_poll(const agents_t *agents)
{
    if (s.page == PAGE_CHAT) {
        const uint32_t v = remote_chat_version();
        const agent_session_t *a = find(agents);
        const bool working = a != NULL && a->state == AGENT_WORK;
        // Rebuilt only when it changed, so a finger scrolling back isn't
        // pulled down every poll.
        if (v != s.chat_version || working != s.working) {
            s.chat_version = v;
            remote_get_chat(&s_chat);
            fill_messages(agents);
        }
        fill_head(agents);
        update_chat_hint();
        if (s.typing != NULL) {
            static const char *const DOTS[] = {".", "..", "..."};
            set_text(s.typing, DOTS[(lv_tick_get() / TYPING_MS) % 3]);
        }
    } else if (s.page == PAGE_NEW) {
        const uint32_t v = remote_projects_version();
        if (v != s.projects_version && s.project < 0) {
            s.projects_version = v;
            remote_get_projects(&s_projects);
            build_new();
        } else if (s.project < 0 && s_projects.valid && s_projects.nprojects > 0) {
            update_hint(card_tr("Pick a project", "Scegli un progetto"));
        }
    }
}
