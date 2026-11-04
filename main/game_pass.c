#include "game_pass.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "card.h"
#include "friends.h"
#include "game_kit.h"
#include "playback.h"

/* Pass the ball: a glowing ball rolls about the round screen as you tilt
 * the case, and bounces off the rim; but the stretch of rim facing the
 * other Yoru (docs/FRIENDS.md) is open when it has the game open too, and
 * the ball rolls out there and into its screen, where it comes in from the
 * side facing this one. Which side that is, a bump of the two cases told.
 * The passes are counted, together. */

#define CX ((float) KIT_CX)
#define CY ((float) KIT_CY)
#define FRAME_MS 16
#define BALL_R 16.0f
/* The rim the ball bounces off, its centre's distance from the middle. */
#define RIM_R ((float) KIT_SCREEN_R - 22.0f - BALL_R)
/* The opening facing the friend: this many degrees either way. */
#define GATE_DEG 28.0f
#define GATE_W 6.0f
/* Tilt to pixels a second squared, rolling friction, bounce. */
#define ROLL 1400.0f
#define FRICTION 0.6f
#define BOUNCE 0.7f
#define MAX_SPEED 900.0f
/* Thrown in by the friend at least this fast, so it gets in. */
#define CATCH_MIN 220.0f
/* Gone to the friend and not back this long: a new ball on a tap. */
#define LOST_MS 8000
#define TITLE_Y 30

#define COL_TITLE lv_color_hex(0x5BE07A)
#define COL_BALL lv_color_hex(0xFFD45C)
#define COL_RIM lv_color_hex(0x2A2E44)
#define COL_GATE lv_color_hex(0x5BE07A)
#define COL_TEXT lv_color_hex(0xF2F2F5)
#define COL_HINT lv_color_hex(0x8C90A6)

static struct {
    lv_obj_t *obj;
    lv_timer_t *timer;
    void (*quit)(void);
    uint32_t last_ms, gone_ms, boing_ms;
    bool here; // the ball is on this screen
    float x, y, vx, vy;
    /* The opening: open, and which way (picture axes). */
    bool gate;
    float gx, gy;
    int passes;
    lv_area_t ball_drawn;
    char note[48];
} s;

/* ---- Drawing ------------------------------------------------------------------- */

static float gate_angle(void)
{
    return atan2f(s.gy, s.gx) * 180.0f / (float) M_PI;
}

/* An angle in 0..359, as LVGL's arcs take them (they wrap past 360). */
static int32_t deg360(float a)
{
    const int32_t d = (int32_t) lroundf(a) % 360;
    return d < 0 ? d + 360 : d;
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_area_t *clip = &layer->_clip_area;
    char str[40];
    snprintf(str, sizeof(str), LV_SYMBOL_LEFT "  %s", card_tr("Pass the ball", "Passa la palla"));
    const lv_area_t ta = kit_text_area(&lv_font_montserrat_20, CX, TITLE_Y, str);
    if (kit_overlap(clip, &ta)) {
        kit_text(layer, &lv_font_montserrat_20, CX, TITLE_Y, COL_TITLE, str);
    }
    // The rim, and the opening towards the friend.
    const float r = RIM_R + BALL_R + 4;
    if (s.gate) {
        const float a = gate_angle();
        kit_arc(layer, CX, CY, r, 3, deg360(a + GATE_DEG), deg360(a - GATE_DEG), COL_RIM);
        kit_arc(layer, CX, CY, r + 2, GATE_W, deg360(a - GATE_DEG), deg360(a + GATE_DEG), COL_GATE);
    } else {
        kit_arc(layer, CX, CY, r, 3, 0, 360, COL_RIM);
    }
    snprintf(str, sizeof(str), "%d", s.passes);
    kit_text(layer, &lv_font_montserrat_40, CX, CY - 8, kit_dim(COL_TEXT, 0.35f), str);
    if (s.note[0] != '\0') {
        kit_text(layer, &lv_font_montserrat_20, CX, CY + 60, COL_HINT, s.note);
    }
    if (s.here) {
        kit_disc(layer, s.x, s.y, BALL_R + 8, kit_dim(COL_BALL, 0.15f));
        kit_disc(layer, s.x, s.y, BALL_R + 3, kit_dim(COL_BALL, 0.4f));
        kit_disc(layer, s.x, s.y, BALL_R, COL_BALL);
    }
}

/* ---- The game ------------------------------------------------------------------ */

static void set_note(const char *en, const char *it)
{
    const char *text = en != NULL ? card_tr(en, it) : "";
    if (strcmp(text, s.note) != 0) {
        snprintf(s.note, sizeof(s.note), "%s", text);
        lv_obj_invalidate(s.obj);
    }
}

/* The opening is there while the friend's side is known and its game is
 * open; the hint says what's missing. */
static void check_gate(void)
{
    float gx = 0, gy = 0;
    const bool sided = friends_side(&gx, &gy);
    const bool open = sided && friends_partner_playing();
    if (open != s.gate || (open && (fabsf(gx - s.gx) > 0.01f || fabsf(gy - s.gy) > 0.01f))) {
        s.gate = open;
        s.gx = gx;
        s.gy = gy;
        lv_obj_invalidate(s.obj);
    }
    if (friends_count() == 0) {
        set_note("No other Yoru nearby", "Nessun'altra Yoru vicina");
    } else if (!sided) {
        set_note("Bump the two Yorus together", "Fai toccare le due Yoru");
    } else if (!open) {
        set_note("Open the game on the other one", "Apri il gioco sull'altra");
    } else if (!s.here) {
        set_note("To your friend!", "All'amica!");
    } else {
        set_note(NULL, NULL);
    }
}

static void new_ball(void)
{
    s.here = true;
    s.x = CX;
    s.y = CY;
    s.vx = s.vy = 0;
    s.ball_drawn = (lv_area_t) {0, 0, -1, -1};
    lv_obj_invalidate(s.obj);
}

/* The ball reached the rim at angle `deg`: out through the opening, or back. */
static void at_rim(float deg, uint32_t now)
{
    const float dx = s.x - CX, dy = s.y - CY, d = hypotf(dx, dy);
    const float nx = dx / d, ny = dy / d;
    float off = deg - gate_angle();
    while (off > 180) {
        off -= 360;
    }
    while (off < -180) {
        off += 360;
    }
    const float out = s.vx * nx + s.vy * ny;
    if (s.gate && fabsf(off) < GATE_DEG && out > 0) {
        // Through: to the friend, where along the opening it left.
        friends_throw(off / GATE_DEG, hypotf(s.vx, s.vy));
        playback_sound(SOUND_WHISTLE);
        s.here = false;
        s.gone_ms = now;
        s.passes++;
        kit_dirty(s.obj, s.ball_drawn);
        lv_obj_invalidate(s.obj);
        return;
    }
    // Back off the rim.
    s.x = CX + nx * RIM_R;
    s.y = CY + ny * RIM_R;
    if (out > 0) {
        s.vx -= (1 + BOUNCE) * out * nx;
        s.vy -= (1 + BOUNCE) * out * ny;
        if (out > 160 && now - s.boing_ms > 250) {
            s.boing_ms = now;
            playback_sound(SOUND_BOOP);
        }
    }
}

/* A ball from the friend comes in through our opening. */
static void caught(float along, float speed, uint32_t now)
{
    const float a = (gate_angle() + along * GATE_DEG) * (float) M_PI / 180.0f;
    const float nx = cosf(a), ny = sinf(a);
    s.here = true;
    s.x = CX + nx * (RIM_R - 2);
    s.y = CY + ny * (RIM_R - 2);
    const float v = fminf(MAX_SPEED, fmaxf(CATCH_MIN, speed));
    s.vx = -nx * v;
    s.vy = -ny * v;
    s.passes++;
    s.ball_drawn = (lv_area_t) {0, 0, -1, -1};
    playback_sound(SOUND_CHIRP);
    lv_obj_invalidate(s.obj);
}

static void tick_cb(lv_timer_t *t)
{
    const uint32_t now = lv_tick_get();
    const float dt = fminf(0.04f, (float) (now - s.last_ms) / 1000.0f);
    s.last_ms = now;
    check_gate();
    float along, speed;
    if (friends_catch(&along, &speed) && s.gate) {
        // Its opening faces ours the other way round: along is mirrored.
        caught(-along, speed, now);
    }
    if (!s.here) {
        return;
    }
    float tx, ty;
    kit_tilt(&tx, &ty);
    s.vx += (tx * ROLL - s.vx * FRICTION) * dt;
    s.vy += (ty * ROLL - s.vy * FRICTION) * dt;
    const float v = hypotf(s.vx, s.vy);
    if (v > MAX_SPEED) {
        s.vx *= MAX_SPEED / v;
        s.vy *= MAX_SPEED / v;
    }
    s.x += s.vx * dt;
    s.y += s.vy * dt;
    const float d = hypotf(s.x - CX, s.y - CY);
    if (d >= RIM_R) {
        at_rim(atan2f(s.y - CY, s.x - CX) * 180.0f / (float) M_PI, now);
    }
    if (s.here) {
        kit_moved(s.obj, &s.ball_drawn, kit_disc_area(s.x, s.y, BALL_R + 8));
    }
    lv_display_trigger_activity(NULL);
}

/* ---- Touch --------------------------------------------------------------------- */

static void quit_async(void *arg)
{
    if (s.quit != NULL) {
        s.quit();
    }
}

static void press_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (p.y < 70) {
        lv_async_call(quit_async, NULL); // the title: back to the games
        return;
    }
    // Lost on the way: a new one.
    if (!s.here && lv_tick_get() - s.gone_ms >= LOST_MS) {
        new_ball();
    }
    kit_tilt_zero(); // level from how it lies now
}

static void deleted_cb(lv_event_t *e)
{
    if (s.timer != NULL) {
        lv_timer_delete(s.timer);
    }
    friends_playing(false);
    memset(&s, 0, sizeof(s));
}

lv_obj_t *game_pass_start(lv_obj_t *parent, void (*quit)(void))
{
    memset(&s, 0, sizeof(s));
    s.quit = quit;
    s.obj = lv_obj_create(parent);
    lv_obj_remove_style_all(s.obj);
    lv_obj_set_size(s.obj, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_center(s.obj);
    lv_obj_add_flag(s.obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s.obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s.obj, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(s.obj, press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s.obj, deleted_cb, LV_EVENT_DELETE, NULL);
    s.last_ms = lv_tick_get();
    kit_tilt_zero();
    friends_playing(true);
    new_ball();
    check_gate();
    s.timer = lv_timer_create(tick_cb, FRAME_MS, NULL);
    return s.obj;
}

void game_pass_icon(lv_layer_t *layer, float x, float y, float r)
{
    // Two rims facing each other, the ball between them.
    kit_arc(layer, x - 0.55f * r, y, 0.4f * r, 2, 300, 60, COL_GATE);
    kit_arc(layer, x + 0.55f * r, y, 0.4f * r, 2, 120, 240, COL_GATE);
    kit_disc(layer, x + 0.05f * r, y - 0.05f * r, 0.2f * r, kit_dim(COL_BALL, 0.35f));
    kit_disc(layer, x + 0.05f * r, y - 0.05f * r, 0.13f * r, COL_BALL);
}
