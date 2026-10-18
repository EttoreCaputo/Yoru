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
#include "look.h"
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
#define VIEW_FRAME_MS 40
/* The little Yorus sit around this point, clear of the title and the
 * quiet button; each box has room above for a hop and what floats over its
 * head, beside for a badge, below for a squash. */
#define VIEW_CY (CY - 14)
#define MINI_TOP(r) ((r) * 9 / 10)
#define MINI_SIDE(r) ((r) / 2)
#define MINI_BOTTOM(r) ((r) / 4)
#define QUIET_W 150
#define QUIET_H 40
#define QUIET_BOTTOM 30
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

/* A little Yoru in the agents view, one per session. */
typedef struct {
    lv_obj_t *obj, *label;
    int r;
    uint32_t rgb;
    float phase; // seconds, so they don't move in step
} mini_t;

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
    lv_timer_t *anim;
    mini_t minis[AGENTS_MAX];
    int nminis;
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

/* ---- The little Yorus ------------------------------------------------------------------ */

/* Each agent's colour, by the name the host gives it; others are pearl, as
 * Yoru itself. */
static uint32_t agent_rgb(const char *agent)
{
    static const struct {
        const char *name;
        uint32_t rgb;
    } COLOURS[] = {
        {"claude", 0xE07B4F},  {"gemini", 0xF2C94C}, {"codex", 0x4C8DF6},   {"gpt", 0x4C8DF6},
        {"openai", 0x4C8DF6},  {"cursor", 0x3FC37E}, {"opencode", 0xA98BF5}, {"copilot", 0xE5E7EE},
        {"windsurf", 0x2EC4C6}, {"aider", 0xF06EA9}, {"qwen", 0x8F7CF7},    {"grok", 0x9AA3B2},
    };
    char low[sizeof(((agent_session_t *) 0)->agent)];
    size_t n = 0;
    for (; agent[n] != '\0' && n < sizeof(low) - 1; n++) {
        low[n] = (char) (agent[n] >= 'A' && agent[n] <= 'Z' ? agent[n] + 32 : agent[n]);
    }
    low[n] = '\0';
    for (size_t i = 0; i < sizeof(COLOURS) / sizeof(COLOURS[0]); i++) {
        if (strstr(low, COLOURS[i].name) != NULL) {
            return COLOURS[i].rgb;
        }
    }
    return 0xAEAAB5;
}

/* Where the k-th of n sits: one in the middle, more on a ring, never one
 * straight up under the title. */
static void mini_place(int k, int n, int32_t *x, int32_t *y, int *r)
{
    static const int RADIUS[AGENTS_MAX + 1] = {0, 72, 60, 50, 46, 42, 40};
    static const int RING[AGENTS_MAX + 1] = {0, 0, 108, 104, 108, 112, 114};
    *r = RADIUS[n];
    const float deg = -90.0f + 180.0f / (float) n + 360.0f * (float) k / (float) n;
    const float a = deg * 3.14159265f / 180.0f;
    *x = CX + (int32_t) lroundf((float) RING[n] * cosf(a));
    *y = VIEW_CY + (int32_t) lroundf((float) RING[n] * sinf(a));
}

/* What a little Yoru does this frame. */
typedef struct {
    float dx, dy;       // the body, from its place
    float sx, sy;       // squash, one of SQUASH
    float gx, gy;       // the gaze, as a share of the radius
    float eye_h;        // the eyes' height (1 normal, small when blinking)
} pose_t;

/* The only squashes there are: each is a sphere rendered once per agent
 * (look.c), so a hop costs nothing after its first. Continuous ones made a
 * new size nearly every frame. Round, stretched up in the air, three steps
 * of landing, sagging. */
static const float SQUASH[][2] = {{1, 1}, {0.96f, 1.05f}, {1.04f, 0.95f}, {1.08f, 0.90f}, {1.11f, 0.86f},
                                  {1.05f, 0.93f}};

static void squash(pose_t *p, int step)
{
    p->sx = SQUASH[step][0];
    p->sy = SQUASH[step][1];
}

static float wave(float t, float period)
{
    return sinf(2 * 3.14159265f * t / period);
}

static pose_t mini_pose(agent_state_t st, float t)
{
    pose_t p = {0, 0, 1, 1, 0, 0, 1};
    // A blink every few seconds while awake.
    if ((st == AGENT_WORK || st == AGENT_WAIT) && fmodf(t, 3.7f) < 0.12f) {
        p.eye_h = 0.15f;
    }
    switch (st) {
    case AGENT_WORK: {
        // Bobbing at its work, the eyes reading along a line.
        p.dy = -0.08f * wave(t, 1.2f);
        p.gx = 0.17f * wave(t, 2.4f);
        p.gy = 0.06f;
        break;
    }
    case AGENT_WAIT: {
        // Hopping on the spot to be seen, eyes on you.
        const float ph = fmodf(t, 0.9f) / 0.9f;
        if (ph < 0.7f) {
            p.dy = -0.34f * sinf(3.14159265f * ph / 0.7f);
            squash(&p, 1);
        } else {
            const float q = sinf(3.14159265f * (ph - 0.7f) / 0.3f);
            squash(&p, q < 0.35f ? 2 : q < 0.7f ? 3 : 4);
        }
        p.gy = -0.04f;
        break;
    }
    case AGENT_DONE:
        // Floating, pleased.
        p.dy = -0.06f * wave(t, 3.0f);
        break;
    case AGENT_ERROR: {
        // Sagging, with a shiver now and then.
        const float ph = fmodf(t, 2.6f);
        if (ph < 0.5f) {
            p.dx = 0.08f * sinf(2 * 3.14159265f * 8 * ph) * (1 - ph / 0.5f);
        }
        squash(&p, 5);
        p.gy = 0.10f;
        break;
    }
    default:
        // Idle: dozing, breathing slowly.
        p.dy = -0.04f * wave(t, 4.0f);
        p.gy = 0.06f;
        break;
    }
    return p;
}

static void disc(lv_layer_t *layer, float x, float y, float r, lv_color_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = LV_RADIUS_CIRCLE;
    const lv_area_t a = {(int32_t) (x - r), (int32_t) (y - r), (int32_t) (x + r), (int32_t) (y + r)};
    lv_draw_rect(layer, &dsc, &a);
}

static void bar(lv_layer_t *layer, float x1, float y1, float x2, float y2, float w, lv_color_t color, lv_opa_t opa)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.p1.x = x1;
    dsc.p1.y = y1;
    dsc.p2.x = x2;
    dsc.p2.y = y2;
    dsc.width = (int32_t) w;
    dsc.round_start = dsc.round_end = 1;
    dsc.color = color;
    dsc.opa = opa;
    lv_draw_line(layer, &dsc);
}

static void ring(lv_layer_t *layer, float x, float y, float r, float w, lv_color_t color, lv_opa_t opa)
{
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center.x = (int32_t) x;
    dsc.center.y = (int32_t) y;
    dsc.radius = (uint16_t) r;
    dsc.start_angle = 0;
    dsc.end_angle = 360;
    dsc.width = (int32_t) w;
    dsc.color = color;
    dsc.opa = opa;
    lv_draw_arc(layer, &dsc);
}

/* The eyes, as on Yoru: dark pills, a little below the middle. Their shape
 * says the state: closed dozing, wide waiting, ^ ^ when done, x x on an
 * error. */
static void mini_eyes(lv_layer_t *layer, agent_state_t st, float x, float y, float r, const pose_t *p)
{
    const lv_color_t ink = lv_color_hex(0x1D1B24);
    const float gap = 0.33f * r * p->sx;
    const float ey = y + (0.12f + p->gy) * r * p->sy;
    const float stroke = fmaxf(2.5f, 0.09f * r);
    for (int side = -1; side <= 1; side += 2) {
        const float ex = x + side * gap + p->gx * r;
        switch (st) {
        case AGENT_IDLE:
            bar(layer, ex - 0.11f * r, ey + 0.04f * r, ex + 0.11f * r, ey + 0.04f * r, stroke, ink, LV_OPA_COVER);
            break;
        case AGENT_DONE: {
            lv_draw_arc_dsc_t dsc;
            lv_draw_arc_dsc_init(&dsc);
            dsc.center.x = (int32_t) ex;
            dsc.center.y = (int32_t) (ey + 0.08f * r);
            dsc.radius = (uint16_t) (0.13f * r);
            dsc.start_angle = 200;
            dsc.end_angle = 340;
            dsc.width = (int32_t) stroke;
            dsc.rounded = 1;
            dsc.color = ink;
            lv_draw_arc(layer, &dsc);
            break;
        }
        case AGENT_ERROR: {
            const float k = 0.09f * r;
            bar(layer, ex - k, ey - k, ex + k, ey + k, stroke, ink, LV_OPA_COVER);
            bar(layer, ex - k, ey + k, ex + k, ey - k, stroke, ink, LV_OPA_COVER);
            break;
        }
        default: {
            const bool wide = st == AGENT_WAIT;
            const float hw = (wide ? 0.11f : 0.085f) * r;
            const float hh = fmaxf((wide ? 0.21f : 0.18f) * r * p->eye_h * p->sy, 1.5f);
            lv_draw_rect_dsc_t dsc;
            lv_draw_rect_dsc_init(&dsc);
            dsc.bg_color = ink;
            dsc.radius = LV_RADIUS_CIRCLE;
            const lv_area_t a = {(int32_t) (ex - hw), (int32_t) (ey - hh), (int32_t) (ex + hw), (int32_t) (ey + hh)};
            lv_draw_rect(layer, &dsc, &a);
            break;
        }
        }
    }
}

/* What floats around it: thinking dots, a call, a sparkle, an error badge,
 * a z. */
static void mini_extras(lv_layer_t *layer, agent_state_t st, float x, float y, float r, float t)
{
    const float top = y - r;
    switch (st) {
    case AGENT_WORK:
        for (int k = 0; k < 3; k++) {
            const float on = fmaxf(0, wave(t - (float) k * 0.3f, 0.9f));
            disc(layer, x + (float) (k - 1) * 0.3f * r, top - 0.32f * r, fmaxf(2.5f, 0.07f * r), CARD_TEXT,
                 (lv_opa_t) (70 + 185 * on));
        }
        break;
    case AGENT_WAIT: {
        // A "!" over its head, pulsing.
        const float s = 1 + 0.15f * fmaxf(0, wave(t, 0.9f));
        const float w = fmaxf(4, 0.12f * r) * s;
        bar(layer, x, top - 0.78f * r * s, x, top - 0.42f * r, w, CARD_WAIT, LV_OPA_COVER);
        disc(layer, x, top - 0.24f * r, w / 2, CARD_WAIT, LV_OPA_COVER);
        break;
    }
    case AGENT_DONE: {
        // A twinkle by its head.
        const float on = fmaxf(0, wave(t, 1.6f));
        const float k = (0.06f + 0.08f * on) * r, sx = x + 0.78f * r, sy = top + 0.05f * r;
        const lv_opa_t opa = (lv_opa_t) (60 + 195 * on);
        bar(layer, sx - k, sy, sx + k, sy, 2, CARD_ALLOW, opa);
        bar(layer, sx, sy - k, sx, sy + k, 2, CARD_ALLOW, opa);
        break;
    }
    case AGENT_ERROR: {
        const float bx = x + 0.72f * r, by = y - 0.72f * r, br = fmaxf(7, 0.2f * r);
        disc(layer, bx, by, br, COL_ERROR, LV_OPA_COVER);
        bar(layer, bx, by - 0.5f * br, bx, by + 0.1f * br, 2.5f, CARD_TEXT, LV_OPA_COVER);
        disc(layer, bx, by + 0.48f * br, 1.5f, CARD_TEXT, LV_OPA_COVER);
        break;
    }
    default: {
        // A z rising from it every few seconds.
        const float ph = fmodf(t, 3.0f) / 3.0f;
        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        dsc.text = "z";
        dsc.font = &lv_font_montserrat_14;
        dsc.color = CARD_DIM;
        dsc.opa = (lv_opa_t) (255 * sinf(3.14159265f * ph));
        const int32_t zx = (int32_t) (x + 0.6f * r + 0.2f * r * ph), zy = (int32_t) (top - 0.5f * r * ph);
        const lv_area_t a = {zx, zy - 16, zx + 14, zy};
        lv_draw_label(layer, &dsc, &a);
        break;
    }
    }
}

static void mini_draw_cb(lv_event_t *e)
{
    const int i = (int) (intptr_t) lv_event_get_user_data(e);
    if (i >= s_view.n) {
        return;
    }
    const mini_t *m = &s.minis[i];
    const agent_session_t *a = &s_view.sessions[i];
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t box;
    lv_obj_get_coords(m->obj, &box);
    const float t = (float) now_ms() / 1000.0f + m->phase;
    const pose_t p = mini_pose(a->state, t);
    const float r = (float) m->r;
    // Squashed by rendering, not by LVGL scaling the sprite every frame.
    const int rx = (int) lroundf(r * p.sx), ry = (int) lroundf(r * p.sy);
    const float x = (float) (box.x1 + box.x2 + 1) / 2 + p.dx * r;
    // Squashed, it stays on its feet.
    const float y = (float) box.y1 + MINI_TOP(m->r) + r + p.dy * r + (r - (float) ry);
    const look_sprite_t *sp = look_tinted_ball(rx, ry, m->rgb);
    if (sp != NULL) {
        look_draw(layer, sp, (int32_t) x, (int32_t) y, 1, 1, 0, LV_OPA_COVER, CARD_TEXT);
    }
    mini_eyes(layer, a->state, x, y, r, &p);
    mini_extras(layer, a->state, x, y - ((float) ry - r), r, t);
    if (strcmp(s.armed, a->id) == 0 && now_ms() - s.armed_ms < ARM_MS) {
        ring(layer, x, y, r + 7, 3, COL_ERROR, LV_OPA_COVER);
    }
}

static void mini_cb(lv_event_t *e)
{
    const agent_session_t *a = session_of(e);
    if (a == NULL || !s_view.focus) {
        return;
    }
    const int i = (int) (intptr_t) lv_event_get_user_data(e);
    if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        // Armed: the next tap on it stops the agent.
        snprintf(s.armed, sizeof(s.armed), "%s", a->id);
        s.armed_ms = now_ms();
        lv_label_set_text(s.minis[i].label, card_tr("Tap to stop", "Tocca: ferma"));
        lv_obj_set_style_text_color(s.minis[i].label, COL_ERROR, 0);
        return;
    }
    const bool armed = strcmp(s.armed, a->id) == 0 && now_ms() - s.armed_ms < ARM_MS;
    s.armed[0] = '\0';
    char id[AGENTS_ID_MAX + 1];
    snprintf(id, sizeof(id), "%s", a->id);
    agents_ui_close(); // off to the computer
    agents_ui_shortcut(armed ? "interrupt" : "focus", id);
}

static void add_mini(lv_obj_t *parent, int i)
{
    const agent_session_t *a = &s_view.sessions[i];
    mini_t *m = &s.minis[i];
    int32_t x, y;
    mini_place(i, s_view.n, &x, &y, &m->r);
    m->rgb = agent_rgb(a->agent);
    // Out of step with each other, by the session's id.
    uint32_t h = 2166136261u;
    for (const char *c = a->id; *c; c++) {
        h = (h ^ (uint8_t) *c) * 16777619u;
    }
    m->phase = (float) (h % 1000) / 100.0f;

    const int32_t w = 2 * m->r + 2 * MINI_SIDE(m->r), hgt = MINI_TOP(m->r) + 2 * m->r + MINI_BOTTOM(m->r);
    m->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(m->obj);
    lv_obj_set_size(m->obj, w, hgt);
    lv_obj_set_pos(m->obj, x - w / 2, y - m->r - MINI_TOP(m->r));
    lv_obj_remove_flag(m->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(m->obj, mini_draw_cb, LV_EVENT_DRAW_MAIN, (void *) (intptr_t) i);
    if (s_view.focus) {
        lv_obj_add_flag(m->obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(m->obj, mini_cb, LV_EVENT_SHORT_CLICKED, (void *) (intptr_t) i);
        lv_obj_add_event_cb(m->obj, mini_cb, LV_EVENT_LONG_PRESSED, (void *) (intptr_t) i);
    }

    // Its project underneath, amber while it waits for you.
    char name[20];
    card_ascii(a->project[0] ? a->project : a->agent, name, sizeof(name) - 1);
    const bool armed = strcmp(s.armed, a->id) == 0;
    m->label = card_label(parent, &lv_font_montserrat_14,
                          armed ? COL_ERROR : a->state == AGENT_WAIT ? CARD_WAIT : CARD_DIM,
                          armed ? card_tr("Tap to stop", "Tocca: ferma") : name);
    lv_label_set_long_mode(m->label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(m->label, 2 * m->r + 36 > 120 ? 120 : 2 * m->r + 36);
    lv_obj_set_style_text_align(m->label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(m->label, x - lv_obj_get_style_width(m->label, 0) / 2, y + m->r + 6);
}

static void anim_cb(lv_timer_t *t)
{
    for (int i = 0; i < s.nminis; i++) {
        lv_obj_invalidate(s.minis[i].obj);
    }
}

static void quiet_cb(lv_event_t *e)
{
    agents_ui_shortcut("quiet", NULL);
}

/* The plan's limits, one short line: "5 h 42%   Week 18%". */
static void add_limits(lv_obj_t *parent)
{
    metrics_model_get(&s_metrics);
    if (!s_metrics.claude.valid || (!s_metrics.claude.has_session && !s_metrics.claude.has_week)) {
        return;
    }
    char text[48] = "";
    float worst = 0;
    int n = 0;
    if (s_metrics.claude.has_session) {
        n += snprintf(text + n, sizeof(text) - n, "%s %.0f%%", card_tr("5 h", "5 ore"), s_metrics.claude.session_pct);
        worst = s_metrics.claude.session_pct;
    }
    if (s_metrics.claude.has_week && n < (int) sizeof(text)) {
        snprintf(text + n, sizeof(text) - n, "%s%s %.0f%%", n > 0 ? "   " : "", card_tr("Week", "Sett."),
                 s_metrics.claude.week_pct);
        worst = fmaxf(worst, s_metrics.claude.week_pct);
    }
    lv_obj_t *l = card_label(parent, &lv_font_montserrat_14,
                             worst >= 95 ? COL_ERROR : worst >= 80 ? CARD_WAIT : CARD_DIM, text);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -QUIET_BOTTOM - QUIET_H - 6);
}

static void build_list(void)
{
    if (s.list != NULL) {
        lv_obj_delete(s.list);
    }
    s.view_built_ms = now_ms();
    s.list = lv_obj_create(s.view);
    s.nminis = 0;
    lv_obj_t *list = s.list;
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_CLICKABLE); // the view takes the swipes

    if (s_view.n == 0) {
        lv_obj_t *none = card_label(list, &lv_font_montserrat_20, CARD_DIM,
                                    !s_view.valid ? card_tr("No word from the computer", "Nessuna notizia dal PC")
                                                  : card_tr("No agents running", "Nessun agente al lavoro"));
        lv_obj_align(none, LV_ALIGN_CENTER, 0, -16);
        lv_obj_t *hint = card_label(list, &lv_font_montserrat_14, CARD_DIM,
                                    card_tr("Connect agents in Companion Hub", "Collega gli agenti in Companion Hub"));
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, 16);
    }
    for (int i = 0; i < s_view.n; i++) {
        add_mini(list, i);
    }
    s.nminis = s_view.n;

    // Do not disturb, as the host keeps it.
    lv_obj_t *quiet = lv_obj_create(list);
    lv_obj_remove_style_all(quiet);
    lv_obj_set_size(quiet, QUIET_W, QUIET_H);
    lv_obj_set_style_radius(quiet, QUIET_H / 2, 0);
    lv_obj_set_style_bg_opa(quiet, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(quiet, CARD_ROW, 0);
    lv_obj_set_style_bg_color(quiet, CARD_ROW_PRESSED, LV_STATE_PRESSED);
    lv_obj_align(quiet, LV_ALIGN_BOTTOM_MID, 0, -QUIET_BOTTOM);
    lv_obj_add_flag(quiet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(quiet, quiet_cb, LV_EVENT_CLICKED, NULL);
    char text[48];
    if (s_view.quiet_min > 0) {
        snprintf(text, sizeof(text), card_tr(LV_SYMBOL_MUTE " %d min", LV_SYMBOL_MUTE " %d min"), s_view.quiet_min);
    } else {
        snprintf(text, sizeof(text), "%s", card_tr(LV_SYMBOL_MUTE " Quiet 1 h", LV_SYMBOL_MUTE " Silenzio 1 h"));
    }
    lv_obj_t *q = card_label(quiet, &lv_font_montserrat_14, s_view.quiet_min > 0 ? CARD_WAIT : CARD_TEXT, text);
    lv_obj_center(q);
    add_limits(list);
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
    s.list = NULL;
    build_list();
    lv_obj_t *title = card_label(s.view, &lv_font_montserrat_20, CARD_ACCENT,
                                 card_tr(LV_SYMBOL_DOWN "  Agents", LV_SYMBOL_DOWN "  Agenti"));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_add_flag(title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(title, 24);
    lv_obj_add_event_cb(title, close_cb, LV_EVENT_CLICKED, NULL);
    s.anim = lv_timer_create(anim_cb, VIEW_FRAME_MS, NULL);
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
    lv_timer_delete(s.anim);
    s.anim = NULL;
    lv_obj_delete_async(s.view);
    s.view = s.list = NULL;
    s.nminis = 0;
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
        } else if (s.armed[0] != '\0' && now_ms() - s.armed_ms >= ARM_MS) {
            s.armed[0] = '\0'; // not stopped in time: back to its name
            build_list();
        } else if (changed || (s.armed[0] == '\0' && now_ms() - s.view_built_ms >= VIEW_REFRESH_MS)) {
            build_list(); // the armed one stays armed: it goes by id
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
