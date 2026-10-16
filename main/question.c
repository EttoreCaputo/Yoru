#include "question.h"

#include <stdio.h>
#include <string.h>

#include "card.h"
#include "cJSON.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "agents_ui.h"
#include "menu.h"
#include "rpc.h"

static const char *TAG = "question";

#define SECONDS_MAX 3600
#define TICK_MS 250
#define PROJECT_CHARS 24
/* Top to bottom inside the circle: who asks, the question, the choices,
 * then Send and Terminal. */
#define HEAD_Y 40
#define HEAD_W 270
#define TEXT_Y 72
#define TEXT_W 340
#define TEXT_H 70
#define OPT_Y 152
#define OPT_W 320
#define OPT_H 48
#define OPT_GAP 8
#define FOOT_GAP 12
#define FOOT_W 130
#define FOOT_H 44

/* Big for internal RAM and only read by the LVGL task: PSRAM (CLAUDE.md). */
EXT_RAM_BSS_ATTR static question_t s_questions[QUESTION_MAX];

static struct {
    question_cb_t cb;
    lv_obj_t *card;
    lv_obj_t *rim;
    /* What one question shows, rebuilt for the next. */
    lv_obj_t *page;
    lv_obj_t *send;
    lv_obj_t *opts[QUESTION_OPTIONS_MAX];
    lv_timer_t *timer;
    char id[QUESTION_ID_MAX + 1];
    char project[PROJECT_CHARS + 1];
    int n;
    int at;
    /* The picks, one bit per option, for each question. */
    uint8_t picked[QUESTION_MAX];
    uint32_t start_ms;
    uint32_t total_ms;
} s;

static void build_page(void);

static void close_card(int answered)
{
    if (s.card == NULL) {
        return;
    }
    lv_obj_delete_async(s.card);
    s.card = s.rim = s.page = s.send = NULL;
    if (s.timer != NULL) {
        lv_timer_delete(s.timer);
        s.timer = NULL;
    }
    ESP_LOGI(TAG, "%s closed (%s)", s.id, answered > 0 ? "answered" : answered == 0 ? "terminal" : "no answer");
    s.id[0] = '\0';
    if (s.cb != NULL) {
        s.cb(false, answered);
    }
}

static void send_answers(void)
{
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "id", s.id);
    cJSON *answers = cJSON_AddArrayToObject(params, "answers");
    for (int q = 0; q < s.n; q++) {
        cJSON *picks = cJSON_CreateArray();
        for (int i = 0; i < s_questions[q].n; i++) {
            if (s.picked[q] & (1u << i)) {
                cJSON_AddItemToArray(picks, cJSON_CreateNumber(i));
            }
        }
        cJSON_AddItemToArray(answers, picks);
    }
    rpc_notify("answer", params);
    close_card(1);
}

/* On to the next question, or done. */
static void next(void)
{
    if (++s.at >= s.n) {
        send_answers();
        return;
    }
    build_page();
}

static void terminal_cb(lv_event_t *e)
{
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "id", s.id);
    cJSON_AddBoolToObject(params, "terminal", true);
    rpc_notify("answer", params);
    close_card(0);
}

static void send_cb(lv_event_t *e)
{
    if (s.picked[s.at] != 0) {
        next();
    }
}

static void show_picked(int i)
{
    const bool on = (s.picked[s.at] & (1u << i)) != 0;
    lv_obj_t *o = s.opts[i];
    lv_obj_set_style_bg_color(o, on ? CARD_ACCENT : CARD_ROW, 0);
    lv_obj_set_style_border_color(o, on ? CARD_ACCENT : CARD_ROW_PRESSED, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(o, 0), on ? lv_color_hex(0x000000) : CARD_TEXT, 0);
}

static void option_cb(lv_event_t *e)
{
    const int i = (int) (intptr_t) lv_event_get_user_data(e);
    if (!s_questions[s.at].multi) {
        s.picked[s.at] = (uint8_t) (1u << i);
        next();
        return;
    }
    s.picked[s.at] ^= (uint8_t) (1u << i);
    show_picked(i);
    // Send means something only with a pick.
    lv_obj_set_style_opa(s.send, s.picked[s.at] != 0 ? LV_OPA_COVER : LV_OPA_40, 0);
}

static void build_page(void)
{
    if (s.page != NULL) {
        // Async: this runs from a tap on one of its own buttons.
        lv_obj_delete_async(s.page);
    }
    const question_t *q = &s_questions[s.at];
    s.page = lv_obj_create(s.card);
    lv_obj_remove_style_all(s.page);
    lv_obj_set_size(s.page, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(s.page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s.page, LV_OBJ_FLAG_CLICKABLE);

    char head[64];
    const int len = snprintf(head, sizeof(head), "%s%s%s", s.project, s.project[0] && q->header[0] ? "  -  " : "",
                             q->header);
    if (s.n > 1 && len < (int) sizeof(head)) {
        snprintf(head + len, sizeof(head) - len, "  %d/%d", s.at + 1, s.n);
    }
    lv_obj_t *h = card_label(s.page, &lv_font_montserrat_20, CARD_ACCENT, head);
    lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(h, HEAD_W, lv_font_get_line_height(&lv_font_montserrat_20));
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, HEAD_Y);

    lv_obj_t *t = card_label(s.page, &lv_font_montserrat_20, CARD_TEXT, q->text);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(t, TEXT_W, TEXT_H);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, TEXT_Y);

    for (int i = 0; i < q->n; i++) {
        s.opts[i] = card_button(s.page, OPT_W, OPT_H, CARD_TEXT, CARD_ROW, q->options[i], &lv_font_montserrat_20,
                                option_cb, LV_EVENT_CLICKED, (void *) (intptr_t) i);
        lv_obj_set_style_border_color(s.opts[i], CARD_ROW_PRESSED, 0);
        lv_obj_set_style_bg_color(s.opts[i], CARD_ROW_PRESSED, LV_STATE_PRESSED);
        lv_obj_align(s.opts[i], LV_ALIGN_TOP_MID, 0, OPT_Y + i * (OPT_H + OPT_GAP));
        if (q->multi) {
            show_picked(i);
        }
    }

    const int32_t foot_y = OPT_Y + q->n * (OPT_H + OPT_GAP) - OPT_GAP + FOOT_GAP;
    lv_obj_t *term = card_button(s.page, FOOT_W, FOOT_H, CARD_DIM, CARD_TRACK, card_tr("Terminal", "Terminale"),
                                 &lv_font_montserrat_20, terminal_cb, LV_EVENT_CLICKED, NULL);
    s.send = NULL;
    if (q->multi) {
        s.send = card_button(s.page, FOOT_W, FOOT_H, CARD_ALLOW, CARD_ALLOW_BG,
                             card_tr(LV_SYMBOL_OK " Send", LV_SYMBOL_OK " Invia"), &lv_font_montserrat_20, send_cb,
                             LV_EVENT_CLICKED, NULL);
        lv_obj_align(s.send, LV_ALIGN_TOP_MID, FOOT_W / 2 + 6, foot_y);
        lv_obj_set_style_opa(s.send, s.picked[s.at] != 0 ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_align(term, LV_ALIGN_TOP_MID, -(FOOT_W / 2 + 6), foot_y);
    } else {
        lv_obj_align(term, LV_ALIGN_TOP_MID, 0, foot_y);
    }
}

static void tick_cb(lv_timer_t *t)
{
    const uint32_t gone = lv_tick_elaps(s.start_ms);
    if (gone >= s.total_ms) {
        close_card(-1); // the agent asks in its terminal now
        return;
    }
    card_rim_set(s.rim, s.total_ms - gone, s.total_ms);
}

void question_init(question_cb_t cb)
{
    s.cb = cb;
}

void question_show(const char *id, const char *project, int seconds, const question_t *questions, int n)
{
    const bool replacing = s.card != NULL;
    if (replacing) {
        lv_obj_delete_async(s.card);
        s.card = s.rim = s.page = s.send = NULL;
        lv_timer_delete(s.timer);
        s.timer = NULL;
        ESP_LOGI(TAG, "%s replaced", s.id);
    }
    menu_close();
    agents_ui_close();
    snprintf(s.id, sizeof(s.id), "%s", id);
    card_ascii(project, s.project, PROJECT_CHARS);
    s.n = n < 1 ? 1 : n > QUESTION_MAX ? QUESTION_MAX : n;
    memcpy(s_questions, questions, sizeof(question_t) * (size_t) s.n);
    memset(s.picked, 0, sizeof(s.picked));
    s.at = 0;
    s.total_ms = (uint32_t) (seconds < 1 ? 1 : seconds > SECONDS_MAX ? SECONDS_MAX : seconds) * 1000;
    s.start_ms = lv_tick_get();
    // Black: the choices fill the middle, where Yoru would be.
    s.card = card_root(true);
    s.rim = card_rim(s.card);
    build_page();
    lv_obj_fade_in(s.card, 180, 0);
    s.timer = lv_timer_create(tick_cb, TICK_MS, NULL);
    tick_cb(s.timer);
    ESP_LOGI(TAG, "%s: %d question(s), %d s", s.id, s.n, seconds);
    if (!replacing && s.cb != NULL) {
        s.cb(true, -1);
    }
}

bool question_hide(const char *id)
{
    if (s.card == NULL || strcmp(id, s.id) != 0) {
        return false;
    }
    close_card(-1);
    return true;
}

bool question_is_open(void)
{
    return s.card != NULL;
}
