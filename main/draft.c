#include "draft.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "card.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "menu.h"
#include "rpc.h"

static const char *TAG = "draft";

/* The longest a draft waits; the host sends a minute or so. */
#define SECONDS_MAX 600
#define TICK_MS 250
/* Where it goes on top, the words in a box that scrolls, the buttons and
 * the hint under them, inside the circle. */
#define HEAD_Y 40
#define HEAD_W 270
#define TEXT_Y 76
#define TEXT_W 330
#define TEXT_H 216
#define BTN_W 150
#define BTN_H 64
#define BTN_Y 304
#define HINT_Y 382
#define HINT_W 250

typedef enum { ACTION_CANCEL, ACTION_SEND, ACTION_REDO } action_t;

static struct {
    draft_cb_t cb;
    lv_obj_t *card;
    lv_obj_t *rim;
    lv_timer_t *timer;
    char id[DRAFT_ID_MAX + 1];
    uint32_t start_ms;
    uint32_t total_ms;
} s;

static void close_card(int sent)
{
    if (s.card == NULL) {
        return;
    }
    lv_obj_delete_async(s.card);
    s.card = s.rim = NULL;
    lv_timer_delete(s.timer);
    s.timer = NULL;
    ESP_LOGI(TAG, "%s closed (%s)", s.id, sent > 0 ? "sent" : sent == 0 ? "cancelled" : "no answer");
    s.id[0] = '\0';
    if (s.cb != NULL) {
        s.cb(false, sent);
    }
}

static void answer(action_t action)
{
    static const char *const ACTIONS[] = {"cancel", "send", "redo"};
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "id", s.id);
    cJSON_AddStringToObject(params, "action", ACTIONS[action]);
    rpc_notify("draft", params);
    close_card(action == ACTION_SEND ? 1 : 0);
}

static void button_cb(lv_event_t *e)
{
    answer((action_t) (intptr_t) lv_event_get_user_data(e));
}

static void tick_cb(lv_timer_t *t)
{
    const uint32_t gone = lv_tick_elaps(s.start_ms);
    if (gone >= s.total_ms) {
        // Nobody sent it: the host drops it, as for a cancel.
        answer(ACTION_CANCEL);
        return;
    }
    card_rim_set(s.rim, s.total_ms - gone, s.total_ms);
}

static void build(const char *text, const char *to)
{
    // Black: the words need the whole screen.
    s.card = card_root(true);
    s.rim = card_rim(s.card);

    char head[DRAFT_TO_CHARS + 8];
    char where[DRAFT_TO_CHARS + 1];
    card_ascii(to, where, DRAFT_TO_CHARS);
    snprintf(head, sizeof(head), "%s %s", card_tr("To", "A"), where);
    lv_obj_t *h = card_label(s.card, &lv_font_montserrat_20, CARD_ACCENT, head);
    lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(h, HEAD_W, lv_font_get_line_height(&lv_font_montserrat_20));
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, HEAD_Y);

    // The words, scrolling when they're long.
    lv_obj_t *box = lv_obj_create(s.card);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, TEXT_W, TEXT_H);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, TEXT_Y);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_AUTO);
    char *words = heap_caps_malloc(DRAFT_TEXT_CHARS + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (words != NULL) {
        card_ascii(text, words, DRAFT_TEXT_CHARS);
        lv_obj_t *l = card_label(box, &lv_font_montserrat_24, CARD_TEXT, words);
        free(words);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(l, TEXT_W);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        // Short words sit in the middle of the box, long ones from its top.
        lv_obj_update_layout(l);
        if (lv_obj_get_height(l) < TEXT_H) {
            lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
        }
    }

    lv_obj_t *cancel = card_button(s.card, BTN_W, BTN_H, CARD_DENY, CARD_DENY_BG,
                                   card_tr(LV_SYMBOL_CLOSE " Cancel", LV_SYMBOL_CLOSE " Annulla"),
                                   &lv_font_montserrat_20, button_cb, LV_EVENT_CLICKED, (void *) ACTION_CANCEL);
    lv_obj_align(cancel, LV_ALIGN_TOP_MID, -(BTN_W / 2 + 8), BTN_Y);
    lv_obj_t *send = card_button(s.card, BTN_W, BTN_H, CARD_ALLOW, CARD_ALLOW_BG,
                                 card_tr(LV_SYMBOL_OK " Send", LV_SYMBOL_OK " Invia"), &lv_font_montserrat_24,
                                 button_cb, LV_EVENT_CLICKED, (void *) ACTION_SEND);
    lv_obj_align(send, LV_ALIGN_TOP_MID, BTN_W / 2 + 8, BTN_Y);

    lv_obj_t *hint = card_label(s.card, &lv_font_montserrat_14, CARD_DIM,
                                card_tr("Hold PWR to say it again", "Tieni PWR per ridirlo"));
    lv_obj_set_width(hint, HINT_W);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, HINT_Y);
}

void draft_init(draft_cb_t cb)
{
    s.cb = cb;
}

void draft_show(const char *id, const char *text, const char *to, int seconds)
{
    const bool replacing = s.card != NULL;
    if (replacing) {
        lv_obj_delete_async(s.card);
        s.card = s.rim = NULL;
        lv_timer_delete(s.timer);
        s.timer = NULL;
        ESP_LOGI(TAG, "%s replaced", s.id);
    }
    menu_close();
    snprintf(s.id, sizeof(s.id), "%s", id);
    s.total_ms = (uint32_t) (seconds < 1 ? 1 : seconds > SECONDS_MAX ? SECONDS_MAX : seconds) * 1000;
    s.start_ms = lv_tick_get();
    build(text, to);
    lv_obj_fade_in(s.card, 150, 0);
    s.timer = lv_timer_create(tick_cb, TICK_MS, NULL);
    tick_cb(s.timer);
    ESP_LOGI(TAG, "%s: %d s", s.id, seconds);
    if (!replacing && s.cb != NULL) {
        s.cb(true, -1);
    }
}

bool draft_hide(const char *id)
{
    if (s.card == NULL || strcmp(id, s.id) != 0) {
        return false;
    }
    close_card(-1);
    return true;
}

bool draft_redo(void)
{
    if (s.card == NULL) {
        return false;
    }
    answer(ACTION_REDO);
    return true;
}

bool draft_is_open(void)
{
    return s.card != NULL;
}
