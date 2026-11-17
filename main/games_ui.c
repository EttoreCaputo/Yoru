#include "games_ui.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "card.h"
#include "game_kit.h"
#include "game_sling.h"
#include "game_stars.h"
#include "game_tilt.h"
#include "playback.h"

/* The launcher: Yoru in the middle, the games in a ring of round bubbles
 * around him, as the menu's (menu.c). He looks at the one under your
 * finger; its name is written below. Like the games, one object that draws
 * everything itself and redraws only what changed. */

#define CX ((float) KIT_CX)
#define CY ((float) KIT_CY)
#define COL_GAMES lv_color_hex(0x5BE07A)
#define RING_R 128.0f
#define BUBBLE_R 52.0f
#define YORU_R 40.0f
#define TITLE_Y 34
#define NAME_Y (KIT_CY + 196)
#define TICK_MS 33

typedef struct {
    const char *en, *it;
    uint32_t top, bottom;
    lv_obj_t *(*start)(lv_obj_t *parent, void (*quit)(void));
    void (*icon)(lv_layer_t *layer, float x, float y, float r);
} game_def_t;

static const game_def_t GAMES[] = {
    {"Tilt ball", "Rotola a casa", 0x3C4A78, 0x161C36, game_tilt_start, game_tilt_icon},
    {"Slingshot", "Fionda", 0x2A2440, 0x0E0B1C, game_sling_start, game_sling_icon},
    {"Constellations", "Costellazioni", 0x1C2A44, 0x080E1E, game_stars_start, game_stars_icon},
};
#define GAME_COUNT ((int) (sizeof(GAMES) / sizeof(GAMES[0])))

static struct {
    lv_obj_t *page, *launcher, *game;
    lv_timer_t *tick;
    void (*back)(void);
    int pressed, shown;
    kit_face_t face;
    float gx, gy;
    lv_area_t name_drawn;
} s;

static void go_back(void)
{
    if (s.back != NULL) {
        s.back();
    }
}

/* ---- Where things are -------------------------------------------------------- */

static void bubble_at(int i, float *x, float *y)
{
    // Round the ring from the top, clockwise; with an even count the top
    // is left free for the title.
    const float step = 2 * (float) M_PI / (float) GAME_COUNT;
    const float a = -(float) M_PI / 2 + step * ((float) i + (GAME_COUNT % 2 == 0 ? 0.5f : 0));
    const float r = GAME_COUNT == 1 ? 0 : RING_R;
    *x = CX + r * cosf(a);
    *y = CY + r * sinf(a) + (GAME_COUNT == 1 ? -20 : 0);
}

static float bubble_r(int i)
{
    return i == s.pressed ? BUBBLE_R * 0.9f : BUBBLE_R;
}

static void yoru_at(float *x, float *y)
{
    *x = CX;
    *y = CY + (GAME_COUNT == 1 ? 120 : 0);
}

static int bubble_hit(lv_point_t p)
{
    for (int i = 0; i < GAME_COUNT; i++) {
        float x, y;
        bubble_at(i, &x, &y);
        if (hypotf((float) p.x - x, (float) p.y - y) < BUBBLE_R + 10) {
            return i;
        }
    }
    return -1;
}

static const char *name_text(void)
{
    const int i = s.pressed >= 0 ? s.pressed : s.shown;
    if (i < 0) {
        return card_tr("Pick a game", "Scegli un gioco");
    }
    return card_tr(GAMES[i].en, GAMES[i].it);
}

/* ---- Drawing ------------------------------------------------------------------ */

static void draw_bubble(lv_layer_t *layer, int i)
{
    float x, y;
    bubble_at(i, &x, &y);
    const float r = bubble_r(i);
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = LV_RADIUS_CIRCLE;
    const lv_color_t colors[2] = {lv_color_hex(GAMES[i].top), lv_color_hex(GAMES[i].bottom)};
    dsc.bg_color = colors[1];
    lv_grad_init_stops(&dsc.bg_grad, colors, NULL, NULL, 2);
    dsc.bg_grad.dir = LV_GRAD_DIR_VER;
    const lv_area_t a = {(int32_t) lroundf(x - r), (int32_t) lroundf(y - r), (int32_t) lroundf(x + r) - 1,
                         (int32_t) lroundf(y + r) - 1};
    lv_draw_rect(layer, &dsc, &a);
    GAMES[i].icon(layer, x, y, r);
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_area_t *clip = &layer->_clip_area;
    char title[32];
    snprintf(title, sizeof(title), LV_SYMBOL_LEFT "  %s", card_tr("Games", "Giochi"));
    const lv_area_t ta = kit_text_area(&lv_font_montserrat_28, CX, TITLE_Y, title);
    if (kit_overlap(clip, &ta)) {
        kit_text(layer, &lv_font_montserrat_28, CX, TITLE_Y, COL_GAMES, title);
    }
    for (int i = 0; i < GAME_COUNT; i++) {
        float x, y;
        bubble_at(i, &x, &y);
        const lv_area_t a = kit_disc_area(x, y, BUBBLE_R + 2);
        if (kit_overlap(clip, &a)) {
            draw_bubble(layer, i);
        }
    }
    float yx, yy;
    yoru_at(&yx, &yy);
    const lv_area_t ya = kit_yoru_area(yx, yy, YORU_R, YORU_R);
    if (kit_overlap(clip, &ya)) {
        kit_yoru(layer, yx, yy, YORU_R, YORU_R, &s.face);
    }
    if (kit_overlap(clip, &s.name_drawn)) {
        kit_text(layer, &lv_font_montserrat_20, CX, NAME_Y, CARD_DIM, name_text());
    }
}

static void redraw_name(void)
{
    kit_dirty(s.launcher, s.name_drawn);
    s.name_drawn = kit_text_area(&lv_font_montserrat_20, CX, NAME_Y, name_text());
    kit_dirty(s.launcher, s.name_drawn);
}

static void redraw_bubble(int i)
{
    if (i >= 0) {
        float x, y;
        bubble_at(i, &x, &y);
        kit_dirty(s.launcher, kit_disc_area(x, y, BUBBLE_R + 2));
    }
}

/* Yoru blinks, and looks at the game under your finger, or around. */
static void tick_cb(lv_timer_t *t)
{
    const uint32_t now = lv_tick_get();
    float tx = 0, ty = -0.1f;
    float yx, yy;
    yoru_at(&yx, &yy);
    const int at = s.pressed >= 0 ? s.pressed : (int) ((now / 2200) % (GAME_COUNT + 1)) - 1;
    if (at >= 0) {
        float x, y;
        bubble_at(at, &x, &y);
        const float d = fmaxf(1, hypotf(x - yx, y - yy));
        tx = (x - yx) / d * 0.75f;
        ty = (y - yy) / d * 0.75f;
    }
    s.gx += (tx - s.gx) * 0.3f;
    s.gy += (ty - s.gy) * 0.3f;
    const kit_face_t f = {.gx = s.gx, .gy = s.gy, .lid = kit_blink(now), .eyes = KIT_EYES_OPEN};
    if (fabsf(f.gx - s.face.gx) > 0.01f || fabsf(f.gy - s.face.gy) > 0.01f || fabsf(f.lid - s.face.lid) > 0.05f) {
        s.face = f;
        kit_dirty(s.launcher, kit_yoru_area(yx, yy, YORU_R, YORU_R));
    }
}

/* ---- Playing ------------------------------------------------------------------ */

static void build_launcher(void);

static void quit_game(void)
{
    if (s.game != NULL) {
        lv_obj_delete(s.game);
        s.game = NULL;
    }
    build_launcher();
}

static void start_game(int i)
{
    lv_timer_delete(s.tick);
    s.tick = NULL;
    lv_obj_delete(s.launcher);
    s.launcher = NULL;
    s.shown = i;
    s.game = GAMES[i].start(s.page, quit_game);
}

static void start_async(void *arg)
{
    if (s.launcher != NULL) {
        start_game((int) (intptr_t) arg);
    }
}

/* ---- Touch ----------------------------------------------------------------- */

static void press_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    const int hit = bubble_hit(p);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        if (hit != s.pressed) {
            redraw_bubble(s.pressed);
            s.pressed = hit;
            redraw_bubble(hit);
            redraw_name();
        }
        return;
    }
    const int was = s.pressed;
    s.pressed = -1;
    redraw_bubble(was);
    if (code == LV_EVENT_RELEASED && was >= 0 && hit == was) {
        playback_sound(SOUND_BOOP);
        lv_async_call(start_async, (void *) (intptr_t) was); // not inside its own event
    } else if (code == LV_EVENT_CLICKED && p.y < TITLE_Y + 30) {
        go_back();
        return;
    }
    redraw_name();
}

static void gesture_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_RIGHT || dir == LV_DIR_BOTTOM) {
        lv_indev_wait_release(indev);
        go_back();
    }
}

static void build_launcher(void)
{
    s.pressed = -1;
    s.launcher = lv_obj_create(s.page);
    lv_obj_remove_style_all(s.launcher);
    lv_obj_set_size(s.launcher, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_center(s.launcher);
    lv_obj_add_flag(s.launcher, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s.launcher, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s.launcher, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(s.launcher, press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s.launcher, press_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s.launcher, press_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s.launcher, press_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s.launcher, press_cb, LV_EVENT_PRESS_LOST, NULL);
    s.face = (kit_face_t) {.lid = 1, .eyes = KIT_EYES_OPEN};
    s.name_drawn = kit_text_area(&lv_font_montserrat_20, CX, NAME_Y, name_text());
    s.tick = lv_timer_create(tick_cb, TICK_MS, NULL);
}

static void deleted_cb(lv_event_t *e)
{
    // Only this page's: the menu deletes it later, maybe after a new one.
    if (lv_event_get_target(e) == s.page) {
        if (s.tick != NULL) {
            lv_timer_delete(s.tick);
        }
        memset(&s, 0, sizeof(s));
    }
}

lv_obj_t *games_ui_build(void (*back)(void))
{
    memset(&s, 0, sizeof(s));
    s.back = back;
    s.shown = -1;
    s.page = card_root();
    lv_obj_add_event_cb(s.page, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s.page, deleted_cb, LV_EVENT_DELETE, NULL);
    build_launcher();
    return s.page;
}

void games_ui_refresh(void)
{
}
