#include "approval.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "card.h"
#include "inbox_ui.h"
#include "cJSON.h"
#include "esp_log.h"
#include "menu.h"
#include "rpc.h"

static const char *TAG = "approval";

/* The protocol's limit for `text`, in characters. */
#define TEXT_CHARS 72
#define TOOL_CHARS 40
#define PROJECT_CHARS 40
/* The longest wait a card runs down; the host uses 120 s. */
#define SECONDS_MAX 3600
#define TICK_MS 250
/* The ring turns red for the last few seconds. */
#define HURRY_S 10
#define BTN_W 150
#define BTN_H 64
/* The card fits around Yoru, shrunk in the middle: the words above it,
 * the buttons below. */
#define HEAD_Y 48
#define HEAD_W 290
#define TEXT_Y 86
#define TEXT_W 330
#define TEXT_H 66
#define BTN_Y 306
#define LEFT_Y 384
#define LEFT_W 290
/* With Always under Deny and Allow: everything a little higher, and the
 * last line narrower, where the circle is. */
#define BTN_Y_ALWAYS 292
#define ALWAYS_W 200
#define ALWAYS_H 48
#define LEFT_Y_ALWAYS 418
#define LEFT_W_ALWAYS 220

static struct {
    approval_cb_t cb;
    lv_obj_t *card;
    lv_obj_t *rim;
    lv_obj_t *left;
    lv_timer_t *timer;
    char id[APPROVAL_ID_MAX + 1];
    char project[PROJECT_CHARS + 1];
    uint32_t start_ms;
    uint32_t total_ms;
    int shown_s;
} s;

/* ---- The card ------------------------------------------------------------------ */

typedef enum { ANSWER_DENY, ANSWER_ALLOW, ANSWER_ALWAYS } answer_t;

static void close_card(int allow)
{
    if (s.card == NULL) {
        return;
    }
    lv_obj_delete_async(s.card);
    s.card = s.rim = s.left = NULL;
    if (s.timer != NULL) {
        lv_timer_delete(s.timer);
        s.timer = NULL;
    }
    ESP_LOGI(TAG, "%s closed (%s)", s.id, allow > 0 ? "allowed" : allow == 0 ? "denied" : "no answer");
    s.id[0] = '\0';
    if (s.cb != NULL) {
        s.cb(false, allow);
    }
}

static void answer_cb(lv_event_t *e)
{
    static const char *const DECISIONS[] = {"deny", "allow", "always"};
    const answer_t answer = (answer_t) (intptr_t) lv_event_get_user_data(e);
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "id", s.id);
    cJSON_AddStringToObject(params, "decision", DECISIONS[answer]);
    // For hosts from before `decision`.
    cJSON_AddBoolToObject(params, "allow", answer != ANSWER_DENY);
    rpc_notify("approval", params);
    close_card(answer != ANSWER_DENY ? 1 : 0);
}

static void tick_cb(lv_timer_t *t)
{
    const uint32_t gone = lv_tick_elaps(s.start_ms);
    if (gone >= s.total_ms) {
        close_card(-1); // the host asks in the terminal now
        return;
    }
    const int left_s = card_rim_set(s.rim, s.total_ms - gone, s.total_ms);
    if (left_s != s.shown_s) {
        s.shown_s = left_s;
        if (s.project[0] != '\0') {
            lv_label_set_text_fmt(s.left, "%s  " LV_SYMBOL_BULLET "  %d s", s.project, left_s);
        } else {
            lv_label_set_text_fmt(s.left, "%d s", left_s);
        }
        if (left_s <= HURRY_S) {
            lv_obj_set_style_text_color(s.left, CARD_HURRY, 0);
        }
    }
}

static lv_obj_t *button(lv_obj_t *parent, answer_t answer)
{
    if (answer == ANSWER_ALWAYS) {
        return card_button(parent, ALWAYS_W, ALWAYS_H, CARD_ACCENT, CARD_TRACK,
                           card_tr(LV_SYMBOL_REFRESH " Always", LV_SYMBOL_REFRESH " Sempre"), &lv_font_montserrat_20,
                           answer_cb, LV_EVENT_CLICKED, (void *) (intptr_t) answer);
    }
    const bool allow = answer == ANSWER_ALLOW;
    const char *text = allow ? card_tr(LV_SYMBOL_OK " Allow", LV_SYMBOL_OK " Consenti")
                             : card_tr(LV_SYMBOL_CLOSE " Deny", LV_SYMBOL_CLOSE " Nega");
    return card_button(parent, BTN_W, BTN_H, allow ? CARD_ALLOW : CARD_DENY, allow ? CARD_ALLOW_BG : CARD_DENY_BG,
                       text, &lv_font_montserrat_24, answer_cb, LV_EVENT_CLICKED, (void *) (intptr_t) answer);
}

static void build(const char *tool, const char *text, const char *project, bool always, bool full)
{
    // See-through, so Yoru shows in the middle, but it takes every touch:
    // no pokes or menu while a request waits.
    s.card = card_root(false);
    s.rim = card_rim(s.card);

    // Which tool, on one line.
    char buf[TEXT_CHARS + 1];
    card_ascii(tool, buf, TOOL_CHARS);
    lv_obj_t *head = card_label(s.card, &lv_font_montserrat_24, CARD_ACCENT, buf);
    lv_label_set_long_mode(head, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(head, HEAD_W, lv_font_get_line_height(&lv_font_montserrat_24));
    lv_obj_set_style_text_align(head, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, HEAD_Y);
    card_ascii(project != NULL ? project : "", s.project, PROJECT_CHARS);

    // What it would do.
    card_ascii(text, buf, TEXT_CHARS);
    lv_obj_t *what = card_label(s.card, &lv_font_montserrat_20, CARD_TEXT, buf);
    lv_label_set_long_mode(what, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(what, TEXT_W, TEXT_H);
    lv_obj_set_style_text_align(what, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(what, LV_ALIGN_TOP_MID, 0, TEXT_Y);

    // Always takes a row under the other two, so they move up a little.
    const bool three = always && full;
    const int32_t btn_y = three ? BTN_Y_ALWAYS : BTN_Y;
    lv_obj_t *deny = button(s.card, ANSWER_DENY);
    lv_obj_align(deny, LV_ALIGN_TOP_MID, -(BTN_W / 2 + 8), btn_y);
    if (full) {
        lv_obj_t *allow = button(s.card, ANSWER_ALLOW);
        lv_obj_align(allow, LV_ALIGN_TOP_MID, BTN_W / 2 + 8, btn_y);
    } else {
        // Cut to fit: it isn't allowed blind from here.
        lv_obj_t *there = card_button(s.card, BTN_W, BTN_H, CARD_DIM, CARD_TRACK,
                                      card_tr("On computer", "Sul computer"), &lv_font_montserrat_20, NULL, 0, NULL);
        lv_obj_align(there, LV_ALIGN_TOP_MID, BTN_W / 2 + 8, btn_y);
    }
    if (three) {
        lv_obj_t *forever = button(s.card, ANSWER_ALWAYS);
        lv_obj_align(forever, LV_ALIGN_TOP_MID, 0, btn_y + BTN_H + 10);
    }

    // The project and the seconds left, under the buttons.
    s.left = card_label(s.card, &lv_font_montserrat_20, CARD_DIM, "");
    lv_label_set_long_mode(s.left, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(s.left, three ? LEFT_W_ALWAYS : LEFT_W, lv_font_get_line_height(&lv_font_montserrat_20));
    lv_obj_set_style_text_align(s.left, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s.left, LV_ALIGN_TOP_MID, 0, three ? LEFT_Y_ALWAYS : LEFT_Y);
}

/* ---- Calls ------------------------------------------------------------------------ */

void approval_init(approval_cb_t cb)
{
    s.cb = cb;
}

void approval_show(const char *id, const char *tool, const char *text, const char *project, int seconds, bool always,
                   bool full)
{
    const bool replacing = s.card != NULL;
    if (replacing) {
        // Straight to the new one: Yoru keeps waiting.
        lv_obj_delete_async(s.card);
        s.card = s.rim = s.left = NULL;
        lv_timer_delete(s.timer);
        s.timer = NULL;
        ESP_LOGI(TAG, "%s replaced", s.id);
    }
    // The card takes the screen: the menu and the inbox go.
    menu_close();
    inbox_ui_close();
    snprintf(s.id, sizeof(s.id), "%s", id);
    s.total_ms = (uint32_t) (seconds < 1 ? 1 : seconds > SECONDS_MAX ? SECONDS_MAX : seconds) * 1000;
    s.start_ms = lv_tick_get();
    s.shown_s = -1;
    build(tool, text, project, always, full);
    lv_obj_fade_in(s.card, 180, 0);
    s.timer = lv_timer_create(tick_cb, TICK_MS, NULL);
    tick_cb(s.timer);
    ESP_LOGI(TAG, "%s: %s, %d s%s%s", s.id, tool, seconds, always ? ", always" : "", full ? "" : ", cut");
    if (!replacing && s.cb != NULL) {
        s.cb(true, -1);
    }
}

bool approval_hide(const char *id)
{
    if (s.card == NULL || strcmp(id, s.id) != 0) {
        return false;
    }
    close_card(-1);
    return true;
}

bool approval_is_open(void)
{
    return s.card != NULL;
}
