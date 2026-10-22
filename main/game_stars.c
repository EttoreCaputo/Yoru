#include "game_stars.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "card.h"
#include "game_kit.h"
#include "playback.h"

/* Constellations: Yoru shows you the sky of where he comes from, a
 * constellation at a time, and you draw it with him: drag him from star to
 * star along the faint lines, all of them, in one stroke, never twice the
 * same line. Lift your finger and the sky forgets; after a few tries he
 * shows where to start. Once drawn, he names it (his names, not ours). */

#define CX ((float) KIT_CX)
#define CY ((float) KIT_CY)
#define FRAME_MS 16
#define MAX_STARS 10
#define MAX_LINES 12
#define STAR_R 5.0f
#define HIT_R 26.0f
#define YORU_R 15.0f
#define REST_X 0.0f
#define REST_Y 192.0f
#define INTRO_MS 1300
#define DONE_MS 2600
#define DUST 44
#define SAVE_KEY "stars"
/* Missed this many times, he shows where to start. */
#define HINT_AFTER 2

#define COL_TITLE lv_color_hex(0x5BE07A)
#define COL_LINE lv_color_hex(0x23263A)
#define COL_LIT lv_color_hex(0xBFD4FF)
#define COL_STAR lv_color_hex(0xF2F2F5)
#define COL_HINT lv_color_hex(0xFFB547)

typedef struct {
    const char *en, *it;
    int8_t n, lines;
    int16_t pts[MAX_STARS][2];
    int8_t ab[MAX_LINES][2];
} sky_t;

static const sky_t SKIES[] = {
    {"The Kite", "L'aquilone", 4, 5, {{0, -120}, {90, 0}, {0, 120}, {-90, 0}}, {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 2}}},
    {"The Little House",
     "La casetta",
     5,
     8,
     {{-80, 100}, {80, 100}, {-80, -20}, {80, -20}, {0, -115}},
     {{0, 1}, {0, 2}, {1, 3}, {2, 3}, {2, 4}, {3, 4}, {0, 3}, {1, 2}}},
    {"The Fish",
     "Il pesce",
     6,
     7,
     {{-130, 0}, {-40, -65}, {-40, 65}, {55, 0}, {130, -65}, {130, 65}},
     {{0, 1}, {0, 2}, {1, 3}, {2, 3}, {3, 4}, {3, 5}, {4, 5}}},
    {"The Boat",
     "La barca",
     7,
     8,
     {{-130, 40}, {130, 40}, {80, 110}, {-80, 110}, {0, 40}, {0, -130}, {90, -10}},
     {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 4}, {0, 4}}},
    {"The Star",
     "La stella",
     5,
     10,
     {{0, -135}, {128, -42}, {79, 109}, {-79, 109}, {-128, -42}},
     {{0, 2}, {1, 3}, {2, 4}, {3, 0}, {4, 1}, {0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 0}}},
    {"The Lantern",
     "La lanterna",
     8,
     9,
     {{0, -130}, {113, -65}, {113, 65}, {0, 130}, {-113, 65}, {-113, -65}, {0, 0}, {0, -175}},
     {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 0}, {6, 0}, {6, 3}, {7, 0}}},
    {"Yoru",
     "Yoru",
     10,
     10,
     {{40, -97}, {97, -40}, {97, 40}, {40, 97}, {-40, 97}, {-97, 40}, {-97, -40}, {-40, -97}, {-165, 0}, {165, 0}},
     {{0, 1}, {2, 3}, {3, 4}, {4, 5}, {6, 7}, {7, 0}, {8, 5}, {8, 6}, {9, 2}, {9, 1}}},
};
#define SKY_COUNT ((int) (sizeof(SKIES) / sizeof(SKIES[0])))

typedef enum { ST_INTRO, ST_DRAW, ST_FORGET, ST_DONE } state_t;

static struct {
    lv_obj_t *obj;
    lv_timer_t *timer;
    void (*quit)(void);
    int level, misses;
    const sky_t *sky;
    state_t st;
    uint32_t st_ms, last_ms;
    bool lit[MAX_LINES];
    int lit_count, at; // the star he is on, -1 for none
    bool dragging;
    /* Yoru: where he is, where he goes when let go. */
    float x, y, tx, ty, gx, gy;
    kit_face_t face;
    uint32_t happy_ms, hmm_ms, sig;
    lv_area_t ball_drawn;
    /* The stars that twinkle or pulse: what was drawn last. */
    uint8_t star_level[MAX_STARS];
    char note[40], note2[40];
    lv_area_t note_drawn;
} s;

/* ---- Where things are --------------------------------------------------------- */

static float star_x(int i)
{
    return CX + s.sky->pts[i][0];
}

static float star_y(int i)
{
    return CY + s.sky->pts[i][1];
}

static lv_area_t line_area(int k)
{
    const int a = s.sky->ab[k][0], b = s.sky->ab[k][1];
    return (lv_area_t) {(int32_t) fminf(star_x(a), star_x(b)) - 4, (int32_t) fminf(star_y(a), star_y(b)) - 4,
                        (int32_t) fmaxf(star_x(a), star_x(b)) + 4, (int32_t) fmaxf(star_y(a), star_y(b)) + 4};
}

static lv_area_t star_area(int i)
{
    return kit_disc_area(star_x(i), star_y(i), STAR_R + 8);
}

static int degree(int i)
{
    int d = 0;
    for (int k = 0; k < s.sky->lines; k++) {
        d += s.sky->ab[k][0] == i || s.sky->ab[k][1] == i;
    }
    return d;
}

/* Where the stroke can start: an odd star, if there are any. */
static bool good_start(int i)
{
    bool any_odd = false;
    for (int j = 0; j < s.sky->n; j++) {
        any_odd |= degree(j) % 2 == 1;
    }
    return !any_odd || degree(i) % 2 == 1;
}

/* A faint dust of far stars, the same every time. */
static void dust_at(int i, float *x, float *y, float *r, float *k)
{
    uint32_t h = (uint32_t) i * 2654435761u + 12345;
    h ^= h >> 13;
    const float a = (float) (h % 6283) / 1000.0f, d = 30 + (float) ((h >> 8) % 190);
    *x = CX + d * cosf(a);
    *y = CY + d * sinf(a);
    *r = (h >> 20) % 3 == 0 ? 1.5f : 1.0f;
    *k = 0.25f + (float) ((h >> 4) % 30) / 100.0f;
}

/* ---- Drawing ------------------------------------------------------------------- */

static void line(lv_layer_t *layer, int k, lv_color_t color, int32_t w)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = w;
    dsc.round_start = 1;
    dsc.round_end = 1;
    const int a = s.sky->ab[k][0], b = s.sky->ab[k][1];
    dsc.p1.x = (lv_value_precise_t) star_x(a);
    dsc.p1.y = (lv_value_precise_t) star_y(a);
    dsc.p2.x = (lv_value_precise_t) star_x(b);
    dsc.p2.y = (lv_value_precise_t) star_y(b);
    lv_draw_line(layer, &dsc);
}

/* How bright a star is now, 0..255: they pulse where to start (a hint),
 * he is on one, or all twinkle once it is drawn. */
static uint8_t star_now(int i, uint32_t now)
{
    if (s.st == ST_DONE) {
        return (uint8_t) (160 + 95 * sinf((float) now / 130.0f + 1.9f * (float) i));
    }
    if (s.st == ST_DRAW && s.lit_count == 0 && s.misses >= HINT_AFTER && good_start(i)) {
        return (uint8_t) (127 + 127 * sinf((float) now / 200.0f));
    }
    return i == s.at ? 255 : 180;
}

static void draw_star(lv_layer_t *layer, int i)
{
    const float k = (float) s.star_level[i] / 255.0f;
    const bool hint = s.st == ST_DRAW && s.lit_count == 0 && s.misses >= HINT_AFTER && good_start(i);
    const lv_color_t c = hint ? COL_HINT : COL_STAR;
    kit_disc(layer, star_x(i), star_y(i), STAR_R + 7, kit_dim(c, 0.12f * k));
    kit_disc(layer, star_x(i), star_y(i), STAR_R + 3, kit_dim(c, 0.3f * k));
    kit_disc(layer, star_x(i), star_y(i), STAR_R, kit_dim(c, 0.55f + 0.45f * k));
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_area_t *clip = &layer->_clip_area;
    char str[32];
    snprintf(str, sizeof(str), LV_SYMBOL_LEFT "  %d / %d", s.level + 1, SKY_COUNT);
    const lv_area_t ta = kit_text_area(&lv_font_montserrat_20, CX, 30, str);
    if (kit_overlap(clip, &ta)) {
        kit_text(layer, &lv_font_montserrat_20, CX, 30, COL_TITLE, str);
    }
    for (int i = 0; i < DUST; i++) {
        float x, y, r, k;
        dust_at(i, &x, &y, &r, &k);
        const lv_area_t a = kit_disc_area(x, y, r);
        if (kit_overlap(clip, &a)) {
            kit_disc(layer, x, y, r, kit_dim(COL_STAR, k));
        }
    }
    for (int k = 0; k < s.sky->lines; k++) {
        const lv_area_t a = line_area(k);
        if (kit_overlap(clip, &a)) {
            line(layer, k, s.lit[k] ? COL_LIT : COL_LINE, s.lit[k] ? 3 : 2);
        }
    }
    for (int i = 0; i < s.sky->n; i++) {
        const lv_area_t a = star_area(i);
        if (kit_overlap(clip, &a)) {
            draw_star(layer, i);
        }
    }
    if (s.st != ST_INTRO) {
        kit_yoru(layer, s.x, s.y, YORU_R, YORU_R, &s.face);
    }
    if (s.note[0] != '\0' && kit_overlap(clip, &s.note_drawn)) {
        const float y = s.st == ST_DONE ? CY + 178 : CY - 14;
        kit_text(layer, s.st == ST_DONE ? &lv_font_montserrat_28 : &lv_font_montserrat_40, CX, y, COL_STAR, s.note);
        if (s.note2[0] != '\0') {
            kit_text(layer, &lv_font_montserrat_20, CX, y + 40, lv_color_hex(0x8C90A6), s.note2);
        }
    }
}

/* ---- The game ------------------------------------------------------------------ */

static void set_note(const char *big, const char *small)
{
    if (s.note[0] != '\0') {
        kit_dirty(s.obj, s.note_drawn);
    }
    snprintf(s.note, sizeof(s.note), "%s", big != NULL ? big : "");
    snprintf(s.note2, sizeof(s.note2), "%s", small != NULL ? small : "");
    if (s.note[0] == '\0') {
        return;
    }
    const bool done = s.st == ST_DONE;
    const float y = done ? CY + 178 : CY - 14;
    lv_area_t a = kit_text_area(done ? &lv_font_montserrat_28 : &lv_font_montserrat_40, CX, y, s.note);
    if (s.note2[0] != '\0') {
        const lv_area_t b = kit_text_area(&lv_font_montserrat_20, CX, y + 40, s.note2);
        a = (lv_area_t) {LV_MIN(a.x1, b.x1), a.y1, LV_MAX(a.x2, b.x2), b.y2};
    }
    s.note_drawn = a;
    kit_dirty(s.obj, a);
}

static void set_state(state_t st, uint32_t now)
{
    s.st = st;
    s.st_ms = now;
}

static void load_sky(int level, uint32_t now)
{
    s.level = level;
    s.sky = &SKIES[level];
    s.misses = 0;
    s.lit_count = 0;
    s.at = -1;
    s.dragging = false;
    memset(s.lit, 0, sizeof(s.lit));
    memset(s.star_level, 0, sizeof(s.star_level));
    s.x = s.tx = CX + REST_X;
    s.y = s.ty = CY + REST_Y;
    s.ball_drawn = (lv_area_t) {0, 0, -1, -1};
    s.note[0] = '\0';
    lv_obj_invalidate(s.obj);
    char big[24];
    snprintf(big, sizeof(big), "%s %d", card_tr("Sky", "Cielo"), level + 1);
    set_state(ST_INTRO, now);
    set_note(big, level == 0 ? card_tr("All the lines, one stroke", "Tutte le linee, un tratto") : NULL);
}

static void forget(uint32_t now)
{
    // The lines drawn so far go dim again.
    for (int k = 0; k < s.sky->lines; k++) {
        if (s.lit[k]) {
            s.lit[k] = false;
            kit_dirty(s.obj, line_area(k));
        }
    }
    s.lit_count = 0;
    s.misses++;
    s.hmm_ms = now;
    playback_sound(s.misses >= HINT_AFTER ? SOUND_HMM : SOUND_CONFUSED);
    if (s.at >= 0) {
        kit_dirty(s.obj, star_area(s.at));
    }
    s.at = -1;
    s.tx = CX + REST_X;
    s.ty = CY + REST_Y;
    set_state(ST_DRAW, now);
}

/* The finger reached star i: along a line not drawn yet from where he is,
 * it lights up. */
static void reach(int i, uint32_t now)
{
    if (i == s.at) {
        return;
    }
    if (s.at < 0) {
        s.at = i; // the stroke starts here
        kit_dirty(s.obj, star_area(i));
        playback_sound(SOUND_BOOP);
        return;
    }
    for (int k = 0; k < s.sky->lines; k++) {
        const int a = s.sky->ab[k][0], b = s.sky->ab[k][1];
        if (!s.lit[k] && ((a == s.at && b == i) || (b == s.at && a == i))) {
            s.lit[k] = true;
            s.lit_count++;
            kit_dirty(s.obj, line_area(k));
            kit_dirty(s.obj, star_area(s.at));
            s.at = i;
            kit_dirty(s.obj, star_area(i));
            if (s.lit_count == s.sky->lines) {
                s.dragging = false;
                s.tx = star_x(i);
                s.ty = star_y(i);
                set_state(ST_DONE, now);
                set_note(card_tr(s.sky->en, s.sky->it), NULL);
                playback_sound(s.level + 1 == SKY_COUNT ? SOUND_EXCITED : SOUND_TADA);
                kit_save(SAVE_KEY, (s.level + 1) % SKY_COUNT);
            } else {
                playback_sound(SOUND_CHIRP);
            }
            return;
        }
    }
}

static void face(uint32_t now)
{
    float tgx = 0, tgy = -0.15f;
    kit_eyes_t eyes = KIT_EYES_OPEN;
    if (s.st == ST_DONE || now - s.happy_ms < 600) {
        eyes = KIT_EYES_HAPPY;
        tgy = 0;
    } else if (now - s.hmm_ms < 900) {
        eyes = KIT_EYES_SAD;
        tgy = 0.4f;
    } else if (s.dragging) {
        // Where the drawing goes: towards the nearest star he can reach.
        float best = 1e9f;
        for (int k = 0; k < s.sky->lines; k++) {
            const int a = s.sky->ab[k][0], b = s.sky->ab[k][1];
            if (s.lit[k] || (a != s.at && b != s.at)) {
                continue;
            }
            const int o = a == s.at ? b : a;
            const float dx = star_x(o) - s.x, dy = star_y(o) - s.y, d = hypotf(dx, dy);
            if (d < best && d > 1) {
                best = d;
                tgx = dx / d * 0.7f;
                tgy = dy / d * 0.7f;
            }
        }
    } else {
        // Up at the sky, now and then.
        const int i = (int) ((now / 1700) % (uint32_t) s.sky->n);
        const float dx = star_x(i) - s.x, dy = star_y(i) - s.y, d = fmaxf(1, hypotf(dx, dy));
        tgx = dx / d * 0.7f;
        tgy = dy / d * 0.7f;
    }
    s.gx += (tgx - s.gx) * 0.25f;
    s.gy += (tgy - s.gy) * 0.25f;
    s.face.gx = s.gx;
    s.face.gy = s.gy;
    s.face.eyes = eyes;
    s.face.lid = eyes == KIT_EYES_OPEN ? kit_blink(now) : 1;
}

static uint32_t ball_sig(void)
{
    uint32_t h = (uint32_t) lroundf(s.x * 2) * 31 + (uint32_t) lroundf(s.y * 2);
    h = h * 31 + (uint32_t) lroundf(s.gx * 24) + 97 * (uint32_t) lroundf(s.gy * 24);
    return h * 31 + (uint32_t) s.face.eyes * 7 + (uint32_t) lroundf(s.face.lid * 8);
}

static void tick_cb(lv_timer_t *t)
{
    const uint32_t now = lv_tick_get();
    const float dt = fminf(0.04f, (float) (now - s.last_ms) / 1000.0f);
    s.last_ms = now;
    const uint32_t in = now - s.st_ms;
    switch (s.st) {
    case ST_INTRO:
        if (in >= INTRO_MS) {
            set_note(NULL, NULL);
            set_state(ST_DRAW, now);
            playback_sound(SOUND_BOOP);
        }
        break;
    case ST_DONE:
        if (in >= DONE_MS + (s.level + 1 == SKY_COUNT ? 1500 : 0)) {
            load_sky((s.level + 1) % SKY_COUNT, now);
            return;
        }
        break;
    default:
        break;
    }
    if (!s.dragging) {
        // Not under the finger: he floats to where he belongs.
        const float k = fminf(1, dt * 8);
        s.x += (s.tx - s.x) * k;
        s.y += (s.ty - s.y) * k;
    }
    face(now);
    // Only the stars whose light changed are redrawn, a few times a second.
    if (s.st != ST_INTRO) {
        for (int i = 0; i < s.sky->n; i++) {
            const uint8_t v = star_now(i, now);
            if (abs((int) v - (int) s.star_level[i]) > 20 || (v == 255) != (s.star_level[i] == 255)) {
                s.star_level[i] = v;
                kit_dirty(s.obj, star_area(i));
            }
        }
    }
    const uint32_t sig = ball_sig();
    if (sig != s.sig || s.st == ST_INTRO) {
        s.sig = sig;
        if (s.st != ST_INTRO) {
            kit_moved(s.obj, &s.ball_drawn, kit_yoru_area(s.x, s.y, YORU_R, YORU_R));
        }
    }
    if (s.dragging || s.st != ST_DRAW) {
        lv_display_trigger_activity(NULL);
    }
}

/* ---- Touch --------------------------------------------------------------------- */

static void quit_async(void *arg)
{
    if (s.quit != NULL) {
        s.quit();
    }
}

static int star_hit(float x, float y)
{
    int best = -1;
    float bd = HIT_R;
    for (int i = 0; i < s.sky->n; i++) {
        const float d = hypotf(x - star_x(i), y - star_y(i));
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

static void press_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    const float x = (float) p.x, y = (float) p.y;
    const uint32_t now = lv_tick_get();
    if (code == LV_EVENT_PRESSED) {
        if (p.y < 70) {
            lv_async_call(quit_async, NULL); // the title: back to the games
            return;
        }
        if (s.st != ST_DRAW) {
            return;
        }
        // On a star, or on Yoru: he comes along.
        const int i = star_hit(x, y);
        if (i < 0 && hypotf(x - s.x, y - s.y) > YORU_R + 16) {
            s.happy_ms = now; // a tap on the black: he only looks happy
            return;
        }
        s.dragging = true;
        s.x = x;
        s.y = y;
        if (i >= 0) {
            reach(i, now);
        }
        return;
    }
    if (!s.dragging) {
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        s.x = x;
        s.y = y;
        const int i = star_hit(x, y);
        if (i >= 0) {
            reach(i, now);
        }
        return;
    }
    // Let go before the end: the sky forgets.
    s.dragging = false;
    if (s.st == ST_DRAW && s.lit_count > 0) {
        forget(now);
    } else if (s.st == ST_DRAW) {
        if (s.at >= 0) {
            s.tx = star_x(s.at); // stays on the star he started from
            s.ty = star_y(s.at);
        }
    }
}

static void deleted_cb(lv_event_t *e)
{
    if (s.timer != NULL) {
        lv_timer_delete(s.timer);
    }
    memset(&s, 0, sizeof(s));
}

lv_obj_t *game_stars_start(lv_obj_t *parent, void (*quit)(void))
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
    lv_obj_add_event_cb(s.obj, press_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s.obj, press_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s.obj, press_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(s.obj, deleted_cb, LV_EVENT_DELETE, NULL);
    const uint32_t now = lv_tick_get();
    s.last_ms = now;
    const int level = (int) kit_load(SAVE_KEY);
    load_sky(level >= 0 && level < SKY_COUNT ? level : 0, now);
    s.timer = lv_timer_create(tick_cb, FRAME_MS, NULL);
    return s.obj;
}

void game_stars_icon(lv_layer_t *layer, float x, float y, float r)
{
    // A little constellation, half drawn.
    static const float P[4][2] = {{-0.5f, 0.25f}, {-0.1f, -0.35f}, {0.3f, -0.05f}, {0.5f, 0.4f}};
    for (int i = 0; i < 3; i++) {
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color = i < 2 ? COL_LIT : lv_color_hex(0x4A5070);
        dsc.width = 2;
        dsc.p1.x = (lv_value_precise_t) (x + P[i][0] * r);
        dsc.p1.y = (lv_value_precise_t) (y + P[i][1] * r);
        dsc.p2.x = (lv_value_precise_t) (x + P[i + 1][0] * r);
        dsc.p2.y = (lv_value_precise_t) (y + P[i + 1][1] * r);
        lv_draw_line(layer, &dsc);
    }
    for (int i = 0; i < 4; i++) {
        kit_disc(layer, x + P[i][0] * r, y + P[i][1] * r, 0.09f * r, kit_dim(COL_STAR, 0.3f));
        kit_disc(layer, x + P[i][0] * r, y + P[i][1] * r, 0.05f * r, COL_STAR);
    }
}
