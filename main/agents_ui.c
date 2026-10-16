#include "agents_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "agents.h"
#include "approval.h"
#include "board.h"
#include "board_settings.h"
#include "card.h"
#include "cJSON.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "menu.h"
#include "metrics_model.h"
#include "question.h"
#include "rpc.h"

static const char *TAG = "agents_ui";

#define CX (BOARD_LCD_H_RES / 2)
#define CY (BOARD_LCD_V_RES / 2)
#define POLL_MS 200
/* The dots: along the top of the rim, this far apart, from the middle. */
#define DOT_R 222
#define DOT_STEP_DEG 9.0f
#define DOT_D 12
#define DOT_WAIT_D 16
/* The line over the bottom rim. */
#define LINE_W 290
#define LINE_H 34
#define LINE_Y (BOARD_LCD_V_RES - 92)
#define TOAST_CHARS 72
#define TOAST_MAX_S 30
/* The view. */
#define VIEW_IDLE_MS 30000
#define VIEW_REFRESH_MS 5000
#define ROW_W 360
#define ROW_H 100
/* A hold arms the interrupt; a tap within this long sends it. */
#define ARM_MS 3000

#define COL_WORK CARD_ACCENT
#define COL_DONE CARD_ALLOW
#define COL_ERROR CARD_HURRY
#define COL_IDLE CARD_DIM

/* A copy for the LVGL task, and the metrics for the plan's limits: big for
 * the stack, so PSRAM. */
EXT_RAM_BSS_ATTR static agents_t s_view;
EXT_RAM_BSS_ATTR static metrics_snapshot_t s_metrics;

static struct {
    lv_obj_t *face;
    agents_ui_cb_t cb;
    lv_obj_t *dots[AGENTS_MAX];
    agent_state_t dot_state[AGENTS_MAX];
    lv_obj_t *line;
    lv_obj_t *line_text;
    uint32_t version;
    bool hidden;
    /* A toast up until then (0: none); the ticker's text otherwise. Kept,
     * so one that comes while the menu is up shows once it goes. */
    uint32_t toast_until_ms;
    char toast[TOAST_CHARS + 1];
    lv_color_t toast_color;
    /* The view. */
    lv_obj_t *view;
    lv_obj_t *list;
    uint32_t view_built_ms;
    char armed[AGENTS_ID_MAX + 1];
    uint32_t armed_ms;
} s;

static void build_list(void);

static uint32_t now_ms(void)
{
    return lv_tick_get();
}

static lv_color_t state_color(agent_state_t st)
{
    switch (st) {
    case AGENT_WORK:
        return COL_WORK;
    case AGENT_WAIT:
        return CARD_WAIT;
    case AGENT_DONE:
        return COL_DONE;
    case AGENT_ERROR:
        return COL_ERROR;
    default:
        return COL_IDLE;
    }
}

static lv_color_t tone_color(toast_tone_t tone)
{
    return tone == TOAST_DONE ? COL_DONE : tone == TOAST_WAIT ? CARD_WAIT : tone == TOAST_ERROR ? COL_ERROR : CARD_TEXT;
}

/* "edit agents.c +42 -3", with a symbol for the kind of step. */
static void step_text(const agent_step_t *st, char *out, size_t len)
{
    static const struct {
        const char *kind, *symbol;
    } KINDS[] = {
        {"read", LV_SYMBOL_FILE},    {"edit", LV_SYMBOL_EDIT},   {"write", LV_SYMBOL_EDIT},
        {"run", LV_SYMBOL_PLAY},     {"search", LV_SYMBOL_EYE_OPEN}, {"web", LV_SYMBOL_WIFI},
        {"agent", LV_SYMBOL_LOOP},   {"ask", LV_SYMBOL_BELL},
    };
    const char *symbol = LV_SYMBOL_RIGHT;
    for (size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); i++) {
        if (strcmp(st->kind, KINDS[i].kind) == 0) {
            symbol = KINDS[i].symbol;
        }
    }
    int n = snprintf(out, len, "%s %s", symbol, st->text);
    if (st->add >= 0 && n > 0 && n < (int) len) {
        n += snprintf(out + n, len - n, "  +%d", st->add);
    }
    if (st->del >= 0 && n > 0 && n < (int) len) {
        snprintf(out + n, len - n, " -%d", st->del);
    }
}

static void age_text(int secs, char *out, size_t len)
{
    if (secs < 60) {
        snprintf(out, len, "%ds", secs);
    } else if (secs < 3600) {
        snprintf(out, len, "%dm", secs / 60);
    } else {
        snprintf(out, len, "%dh", secs / 3600);
    }
}

static int age_now(const agent_session_t *a)
{
    return a->since_s + (int) ((uint32_t) (esp_timer_get_time() / 1000) - s_view.at_ms) / 1000;
}

/* ---- The dots ---------------------------------------------------------------- */

static void opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa(obj, (lv_opa_t) v, 0);
}

/* Waiting pulses, working breathes, the rest hold still. */
static void animate_dot(lv_obj_t *dot, agent_state_t st)
{
    lv_anim_delete(dot, opa_cb);
    lv_obj_set_style_opa(dot, st == AGENT_DONE || st == AGENT_IDLE ? LV_OPA_70 : LV_OPA_COVER, 0);
    if (st != AGENT_WAIT && st != AGENT_WORK) {
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, dot);
    lv_anim_set_exec_cb(&a, opa_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, st == AGENT_WAIT ? LV_OPA_20 : LV_OPA_40);
    lv_anim_set_duration(&a, st == AGENT_WAIT ? 600 : 1400);
    lv_anim_set_reverse_duration(&a, st == AGENT_WAIT ? 600 : 1400);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void place_dots(void)
{
    const bool show = !s.hidden && board_settings_on(SETTING_AGENTS_RING);
    const int n = show ? s_view.n : 0;
    for (int i = 0; i < AGENTS_MAX; i++) {
        lv_obj_t *dot = s.dots[i];
        if (i >= n) {
            if (!lv_obj_has_flag(dot, LV_OBJ_FLAG_HIDDEN)) {
                lv_anim_delete(dot, opa_cb);
                lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
            }
            continue;
        }
        const agent_state_t st = s_view.sessions[i].state;
        const int32_t d = st == AGENT_WAIT ? DOT_WAIT_D : DOT_D;
        const float deg = -90.0f + ((float) i - (float) (n - 1) / 2.0f) * DOT_STEP_DEG;
        const float a = deg * 3.14159265f / 180.0f;
        lv_obj_set_size(dot, d, d);
        lv_obj_set_pos(dot, CX + (int32_t) (DOT_R * cosf(a)) - d / 2, CY + (int32_t) (DOT_R * sinf(a)) - d / 2);
        lv_obj_set_style_bg_color(dot, state_color(st), 0);
        if (lv_obj_has_flag(dot, LV_OBJ_FLAG_HIDDEN) || s.dot_state[i] != st) {
            animate_dot(dot, st);
        }
        s.dot_state[i] = st;
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- The line over the bottom rim ------------------------------------------------- */

static void show_line(const char *text, lv_color_t color)
{
    if (text == NULL || s.hidden) {
        lv_obj_add_flag(s.line, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (strcmp(lv_label_get_text(s.line_text), text) != 0) {
        lv_label_set_text(s.line_text, text);
    }
    lv_obj_set_style_text_color(s.line_text, color, 0);
    lv_obj_remove_flag(s.line, LV_OBJ_FLAG_HIDDEN);
}

/* The step the first working session is on, or none. */
static void update_ticker(void)
{
    if (s.toast_until_ms != 0 && (int32_t) (now_ms() - s.toast_until_ms) < 0) {
        show_line(s.toast, s.toast_color); // the toast has the line
        return;
    }
    s.toast_until_ms = 0;
    if (!board_settings_on(SETTING_AGENTS_TICKER)) {
        show_line(NULL, CARD_TEXT);
        return;
    }
    for (int i = 0; i < s_view.n; i++) {
        const agent_session_t *a = &s_view.sessions[i];
        if (a->state == AGENT_WORK && a->nsteps > 0) {
            const agent_step_t *st = &a->steps[a->nsteps - 1];
            char text[80];
            step_text(st, text, sizeof(text));
            show_line(text, st->ok == 0 ? COL_ERROR : CARD_DIM);
            return;
        }
    }
    show_line(NULL, CARD_TEXT);
}

void agents_ui_toast(const char *text, int seconds, toast_tone_t tone)
{
    card_ascii(text, s.toast, TOAST_CHARS);
    s.toast_color = tone_color(tone);
    seconds = seconds < 1 ? 1 : seconds > TOAST_MAX_S ? TOAST_MAX_S : seconds;
    s.toast_until_ms = now_ms() + (uint32_t) seconds * 1000;
    if (s.toast_until_ms == 0) {
        s.toast_until_ms = 1; // 0 means none
    }
    show_line(s.toast, s.toast_color);
    if (!s.hidden) {
        lv_obj_fade_in(s.line, 150, 0);
    }
}

/* ---- The view --------------------------------------------------------------------- */

bool agents_ui_shortcut(const char *name, const char *session)
{
    agents_summary_t sum;
    agents_summary(&sum);
    if (sum.n == 0 && strcmp(name, "quiet") != 0) {
        agents_ui_toast(card_tr("No agents to reach", "Nessun agente"), 3, TOAST_INFO);
        return false;
    }
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", name);
    if (session != NULL && session[0] != '\0') {
        cJSON_AddStringToObject(params, "session", session);
    }
    rpc_notify("shortcut", params);
    ESP_LOGI(TAG, "shortcut %s %s", name, session != NULL ? session : "");
    return true;
}

static void view_gesture_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_RIGHT || dir == LV_DIR_BOTTOM) {
        lv_indev_wait_release(indev);
        agents_ui_close();
    }
}

static void close_cb(lv_event_t *e)
{
    agents_ui_close();
}

static const agent_session_t *session_of(lv_event_t *e)
{
    const int i = (int) (intptr_t) lv_event_get_user_data(e);
    return i < s_view.n ? &s_view.sessions[i] : NULL;
}

static void row_cb(lv_event_t *e)
{
    const agent_session_t *a = session_of(e);
    if (a == NULL || !s_view.focus) {
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        // Armed: the next tap on it stops the agent.
        snprintf(s.armed, sizeof(s.armed), "%s", a->id);
        s.armed_ms = now_ms();
        lv_obj_t *row = lv_event_get_current_target(e);
        lv_obj_set_style_border_color(row, COL_ERROR, 0);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_label_set_text(lv_obj_get_child(row, 3), card_tr(LV_SYMBOL_STOP " Tap to interrupt the agent",
                                                            LV_SYMBOL_STOP " Tocca per interrompere"));
        lv_obj_set_style_text_color(lv_obj_get_child(row, 3), COL_ERROR, 0);
        return;
    }
    const bool armed = strcmp(s.armed, a->id) == 0 && now_ms() - s.armed_ms < ARM_MS;
    s.armed[0] = '\0';
    char id[AGENTS_ID_MAX + 1];
    snprintf(id, sizeof(id), "%s", a->id);
    agents_ui_close(); // off to the computer
    agents_ui_shortcut(armed ? "interrupt" : "focus", id);
}

static void quiet_cb(lv_event_t *e)
{
    agents_ui_shortcut("quiet", NULL);
}

static lv_obj_t *row_box(lv_obj_t *list, int32_t h)
{
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, ROW_W, h);
    lv_obj_set_style_radius(row, 26, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, CARD_ROW, 0);
    lv_obj_set_style_bg_color(row, CARD_ROW_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(row, 26, 0);
    lv_obj_set_style_pad_ver(row, 10, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static lv_obj_t *line_in(lv_obj_t *row, const lv_font_t *font, lv_color_t color, const char *text, int32_t w)
{
    lv_obj_t *l = card_label(row, font, color, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(l, w, lv_font_get_line_height(font));
    return l;
}

static void add_session(lv_obj_t *list, int i)
{
    const agent_session_t *a = &s_view.sessions[i];
    lv_obj_t *row = row_box(list, ROW_H);
    if (s_view.focus) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_SHORT_CLICKED, (void *) (intptr_t) i);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_LONG_PRESSED, (void *) (intptr_t) i);
    }
    const int32_t w = ROW_W - 52;
    char text[96], age[16];
    age_text(age_now(a), age, sizeof(age));
    snprintf(text, sizeof(text), LV_SYMBOL_BULLET " %s  %s", a->project[0] ? a->project : "?",
             a->agent[0] ? a->agent : "");
    lv_obj_t *head = line_in(row, &lv_font_montserrat_20, state_color(a->state), text, w - 50);
    lv_obj_align(head, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *since = card_label(row, &lv_font_montserrat_14, CARD_DIM, age);
    lv_obj_align(since, LV_ALIGN_TOP_RIGHT, 0, 4);

    // The task, or what it waits for.
    const char *task = a->task[0] ? a->task
                       : a->state == AGENT_WAIT ? card_tr("Waiting for you", "Ti aspetta")
                                                : "";
    lv_obj_t *t = line_in(row, &lv_font_montserrat_14, CARD_TEXT, task, w);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 30);

    text[0] = '\0';
    if (a->nsteps > 0) {
        step_text(&a->steps[a->nsteps - 1], text, sizeof(text));
    }
    lv_obj_t *st = line_in(row, &lv_font_montserrat_14, a->nsteps > 0 && a->steps[a->nsteps - 1].ok == 0 ? COL_ERROR
                                                                                                        : CARD_DIM,
                           text, w);
    lv_obj_align(st, LV_ALIGN_TOP_LEFT, 0, 54);
}

static void add_limit(lv_obj_t *list, const char *name, float pct)
{
    lv_obj_t *row = row_box(list, 64);
    char text[40];
    snprintf(text, sizeof(text), "%s  %.0f%%", name, pct);
    lv_obj_t *l = line_in(row, &lv_font_montserrat_20, pct >= 95 ? COL_ERROR : pct >= 80 ? CARD_WAIT : CARD_TEXT,
                          text, ROW_W - 52);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *bar = lv_bar_create(row);
    lv_obj_set_size(bar, ROW_W - 52, 8);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, (int32_t) (pct > 100 ? 100 : pct < 0 ? 0 : pct), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, CARD_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, pct >= 95 ? COL_ERROR : pct >= 80 ? CARD_WAIT : CARD_ACCENT, LV_PART_INDICATOR);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, -2);
}

static void build_list(void)
{
    const int32_t scroll = s.list != NULL ? lv_obj_get_scroll_y(s.list) : 0;
    if (s.list != NULL) {
        lv_obj_delete(s.list);
    }
    s.view_built_ms = now_ms();
    s.list = lv_obj_create(s.view);
    lv_obj_t *list = s.list;
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, BOARD_LCD_H_RES, BOARD_LCD_V_RES - 80);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_style_pad_top(list, 20, 0);
    lv_obj_set_style_pad_bottom(list, 140, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    if (s_view.n == 0) {
        lv_obj_t *none = card_label(list, &lv_font_montserrat_20, CARD_DIM,
                                    !s_view.valid ? card_tr("No word from the computer", "Nessuna notizia dal PC")
                                                  : card_tr("No agents running", "Nessun agente al lavoro"));
        lv_obj_set_style_pad_top(none, 60, 0);
        lv_obj_t *hint = card_label(list, &lv_font_montserrat_14, CARD_DIM,
                                    card_tr("Connect agents in Companion Hub", "Collega gli agenti in Companion Hub"));
        (void) hint;
    }
    for (int i = 0; i < s_view.n; i++) {
        add_session(list, i);
    }
    if (s_view.n > 0 && s_view.focus) {
        lv_obj_t *hint = card_label(list, &lv_font_montserrat_14, CARD_DIM,
                                    card_tr("Tap: terminal   Hold: interrupt", "Tocca: terminale   Tieni: interrompi"));
        (void) hint;
    }

    // Do not disturb, as the host keeps it.
    lv_obj_t *quiet = row_box(list, 56);
    lv_obj_add_flag(quiet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(quiet, quiet_cb, LV_EVENT_CLICKED, NULL);
    char text[48];
    if (s_view.quiet_min > 0) {
        snprintf(text, sizeof(text), card_tr(LV_SYMBOL_MUTE " Quiet, %d min left", LV_SYMBOL_MUTE " Silenzio, %d min"),
                 s_view.quiet_min);
    } else {
        snprintf(text, sizeof(text), "%s", card_tr(LV_SYMBOL_MUTE " Quiet for 1 hour", LV_SYMBOL_MUTE " Silenzio per 1 ora"));
    }
    lv_obj_t *q = line_in(quiet, &lv_font_montserrat_20, s_view.quiet_min > 0 ? CARD_WAIT : CARD_TEXT, text, ROW_W - 52);
    lv_obj_align(q, LV_ALIGN_LEFT_MID, 0, 0);

    // The plan's limits, from the metrics.
    metrics_model_get(&s_metrics);
    if (s_metrics.claude.valid && s_metrics.claude.has_session) {
        add_limit(list, card_tr("5 h limit", "Limite 5 ore"), s_metrics.claude.session_pct);
    }
    if (s_metrics.claude.valid && s_metrics.claude.has_week) {
        add_limit(list, card_tr("Week", "Settimana"), s_metrics.claude.week_pct);
    }
    lv_obj_update_layout(list);
    lv_obj_scroll_to_y(list, scroll, LV_ANIM_OFF);
}

void agents_ui_open(void)
{
    if (s.view != NULL) {
        return;
    }
    menu_close();
    agents_get(&s_view);
    s.version = agents_version();
    s.armed[0] = '\0';
    s.view = card_root(true);
    lv_obj_add_event_cb(s.view, view_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_t *title = card_label(s.view, &lv_font_montserrat_28, CARD_ACCENT,
                                 card_tr(LV_SYMBOL_DOWN "  Agents", LV_SYMBOL_DOWN "  Agenti"));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_add_flag(title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(title, 24);
    lv_obj_add_event_cb(title, close_cb, LV_EVENT_CLICKED, NULL);
    s.list = NULL;
    build_list();
    lv_obj_fade_in(s.view, 180, 0);
    if (s.face != NULL) {
        lv_obj_add_flag(s.face, LV_OBJ_FLAG_HIDDEN);
    }
    lv_display_trigger_activity(NULL);
    ESP_LOGI(TAG, "view open, %d session(s)", s_view.n);
    if (s.cb != NULL) {
        s.cb(true);
    }
}

void agents_ui_close(void)
{
    if (s.view == NULL) {
        return;
    }
    lv_obj_delete_async(s.view);
    s.view = s.list = NULL;
    if (s.face != NULL) {
        lv_obj_remove_flag(s.face, LV_OBJ_FLAG_HIDDEN);
    }
    ESP_LOGI(TAG, "view closed");
    if (s.cb != NULL) {
        s.cb(false);
    }
}

bool agents_ui_is_open(void)
{
    return s.view != NULL;
}

/* ---- Keeping up --------------------------------------------------------------------- */

static void poll_cb(lv_timer_t *t)
{
    // Out of the way of whatever else has the screen.
    const bool hidden = s.view != NULL || menu_is_open() || approval_is_open() || question_is_open();
    const uint32_t v = agents_version();
    const bool changed = v != s.version;
    if (changed) {
        s.version = v;
        agents_get(&s_view);
    }
    if (changed || hidden != s.hidden) {
        s.hidden = hidden;
        place_dots();
    }
    update_ticker();
    if (s.view != NULL) {
        if (lv_display_get_inactive_time(NULL) >= VIEW_IDLE_MS) {
            agents_ui_close();
        } else if (changed || now_ms() - s.view_built_ms >= VIEW_REFRESH_MS) {
            if (s.armed[0] == '\0' || now_ms() - s.armed_ms >= ARM_MS) {
                s.armed[0] = '\0';
                build_list(); // not under a finger that is about to interrupt
            }
        }
    }
}

void agents_ui_init(lv_obj_t *face, agents_ui_cb_t cb)
{
    s.face = face;
    s.cb = cb;
    // On the top layer, not clickable: touches go through to Yoru.
    for (int i = 0; i < AGENTS_MAX; i++) {
        lv_obj_t *dot = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, DOT_D, DOT_D);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        s.dots[i] = dot;
    }
    s.line = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s.line);
    lv_obj_set_size(s.line, LINE_W, LINE_H);
    lv_obj_set_style_radius(s.line, LINE_H / 2, 0);
    lv_obj_set_style_bg_color(s.line, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s.line, LV_OPA_70, 0);
    lv_obj_align(s.line, LV_ALIGN_TOP_MID, 0, LINE_Y);
    lv_obj_remove_flag(s.line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s.line, LV_OBJ_FLAG_HIDDEN);
    s.line_text = card_label(s.line, &lv_font_montserrat_20, CARD_TEXT, "");
    lv_label_set_long_mode(s.line_text, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(s.line_text, LINE_W - 28, lv_font_get_line_height(&lv_font_montserrat_20));
    lv_obj_set_style_text_align(s.line_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s.line_text);
    s.version = agents_version() - 1; // the first poll draws
    lv_timer_create(poll_cb, POLL_MS, NULL);
}
