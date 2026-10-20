#include "errand.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "esp_log.h"
#include "esp_random.h"
#include "eyes.h"
#include "playback.h"

static const char *TAG = "errand";

#define CX (BOARD_LCD_H_RES / 2)
#define CY (BOARD_LCD_V_RES / 2)
#define FRAME_MS 33
/* The portal: how far out it opens and how big it is. */
#define PORTAL_DIST 150
#define PORTAL_R 42
/* Behind the portal, Yoru is this far away: smaller than its hole. */
#define INSIDE_Z 9.0f
#define OPEN_MS 450
#define HANDS_UP_MS 420
#define JUMP_MS 800
/* Inside at least this long, so a quick call still shows the trip; at most
 * this long, if the end never comes. */
#define INSIDE_MIN_MS 900
#define INSIDE_MAX_MS 20000
#define SPARK_EVERY_MS 380
#define BACK_MS 1200
#define CLOSE_AT_MS 350
#define CLOSE_MS 350
/* The parcel opens a little before the hands let go of it (eyes.c). */
#define UNWRAP_AT_MS 2150
/* Where the parcel is with Yoru home: eyes.c's PARCEL_Y at LOOK_SCALE. */
#define PARCEL_DY 78
#define MAX_OUT 6
#define MAX_PARTS 18

typedef enum { E_IDLE, E_OPEN, E_DIVE, E_INSIDE, E_BACK, E_UNWRAP } phase_t;

typedef struct {
    float x, y, vx, vy, g;
    int16_t age, life; // frames
    uint32_t rgb;
    uint8_t r;
    bool star;
} part_t;

/* What a connector looks like: the colour of its portal and what is drawn in
 * it (NULL: its initial). */
typedef struct {
    const char *word;
    uint32_t rgb;
    const char *glyph;
} look_t;

static const look_t LOOKS[] = {
    {"mail", 0xEA4335, LV_SYMBOL_ENVELOPE}, {"outlook", 0x0A84FF, LV_SYMBOL_ENVELOPE},
    {"calendar", 0x4285F4, "31"},           {"drive", 0x34A853, LV_SYMBOL_DIRECTORY},
    {"dropbox", 0x0061FE, LV_SYMBOL_DIRECTORY}, {"file", 0x34A853, LV_SYMBOL_DIRECTORY},
    {"notion", 0xE8E8EC, "N"},              {"github", 0xE8E8EC, LV_SYMBOL_EDIT},
    {"gitlab", 0xFC6D26, LV_SYMBOL_EDIT},   {"linear", 0x5E6AD2, "L"},
    {"jira", 0x2684FF, "J"},                {"slack", 0xE01E5A, "#"},
    {"discord", 0x5865F2, LV_SYMBOL_CALL},  {"spotify", 0x1DB954, LV_SYMBOL_AUDIO},
    {"music", 0xFA2D48, LV_SYMBOL_AUDIO},   {"web", 0x40C8E0, LV_SYMBOL_GPS},
    {"search", 0x40C8E0, LV_SYMBOL_GPS},    {"memory", 0xBF5AF2, LV_SYMBOL_SAVE},
};
#define LOOKS_N ((int) (sizeof(LOOKS) / sizeof(LOOKS[0])))
#define DEFAULT_RGB 0xBF5AF2

/* Where portals open: up and around the top of the rim, clear of the toast
 * at the bottom. A connector always gets the same one. */
static const float SLOTS_DEG[] = {-90, -128, -52, -152, -28};
#define SLOTS_N ((int) (sizeof(SLOTS_DEG) / sizeof(SLOTS_DEG[0])))

static struct {
    lv_obj_t *obj;
    lv_timer_t *timer;
    errand_cb_t cb;
    phase_t phase;
    uint32_t phase_ms;
    /* The portal: its centre from the middle of the screen, its colour, what
     * is drawn in it, how open it is (0..1) and its gulp. */
    float px, py;
    uint32_t rgb;
    char glyph[8];
    float open, gulp;
    /* The calls still out, and how the ended ones went. */
    char out[MAX_OUT][24];
    int nout;
    int ok, failed;
    bool jumped;
    uint32_t next_spark_ms;
    part_t parts[MAX_PARTS];
    lv_area_t portal_drawn, parts_drawn;
    bool portal_shown, parts_shown;
} e;

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * (float) (esp_random() & 0xFFFF) / 65535.0f;
}

static uint32_t elapsed(void)
{
    return lv_tick_elaps(e.phase_ms);
}

static void go(phase_t phase)
{
    e.phase = phase;
    e.phase_ms = lv_tick_get();
}

/* A point in Yoru's space at distance `z` that shows at (sx, sy) from the
 * middle (as life.c's at()). */
static eyes_pos_t at(float sx, float sy, float z)
{
    return (eyes_pos_t) {sx * z, sy * z, z};
}

static bool contains(const char *hay, const char *word)
{
    char low[32];
    size_t i = 0;
    for (; hay[i] != '\0' && i < sizeof(low) - 1; i++) {
        low[i] = (char) tolower((unsigned char) hay[i]);
    }
    low[i] = '\0';
    return strstr(low, word) != NULL;
}

static void pick_look(const char *source)
{
    e.rgb = DEFAULT_RGB;
    e.glyph[0] = '\0';
    for (int i = 0; i < LOOKS_N; i++) {
        if (contains(source, LOOKS[i].word)) {
            e.rgb = LOOKS[i].rgb;
            snprintf(e.glyph, sizeof(e.glyph), "%s", LOOKS[i].glyph);
            break;
        }
    }
    if (e.glyph[0] == '\0') {
        const char c = source[0] != '\0' ? source[0] : '?';
        snprintf(e.glyph, sizeof(e.glyph), "%c", isalnum((unsigned char) c) ? toupper((unsigned char) c) : '?');
    }
    uint32_t hash = 5381;
    for (const char *p = source; *p != '\0'; p++) {
        hash = hash * 33 + (uint32_t) tolower((unsigned char) *p);
    }
    const float a = SLOTS_DEG[hash % SLOTS_N] * (float) M_PI / 180.0f;
    e.px = PORTAL_DIST * cosf(a);
    e.py = PORTAL_DIST * sinf(a);
}

/* ---- Sparkles -------------------------------------------------------------- */

static void spawn(float x, float y, float vx, float vy, float g, int life, uint32_t rgb, int r, bool star)
{
    for (int i = 0; i < MAX_PARTS; i++) {
        part_t *p = &e.parts[i];
        if (p->age < p->life) {
            continue;
        }
        *p = (part_t) {x, y, vx, vy, g, 0, (int16_t) life, rgb, (uint8_t) r, star};
        return;
    }
}

/* A few sparkles shoot out of the portal: it is rummaging in there. */
static void rummage_spark(void)
{
    const float a = frand(0, 2 * (float) M_PI), v = frand(2.0f, 3.6f);
    const uint32_t rgb = (esp_random() & 1) ? e.rgb : 0xFFF4C2;
    spawn(e.px + cosf(a) * 20, e.py + sinf(a) * 20, cosf(a) * v, sinf(a) * v, 0.06f, 18, rgb, 4, true);
}

/* The parcel opens: sparkles and confetti everywhere from its hands. */
static void burst(float x, float y)
{
    static const uint32_t COLORS[] = {0xFFD45C, 0xFFF4C2, 0xFF7AB8, 0x7FE3FF};
    for (int i = 0; i < MAX_PARTS; i++) {
        const float a = frand((float) -M_PI, 0) + frand(-0.3f, 0.3f), v = frand(3.0f, 7.0f);
        const uint32_t rgb = i % 3 == 0 ? e.rgb : COLORS[i % 4];
        spawn(x, y, cosf(a) * v, sinf(a) * v, 0.28f, (int) frand(22, 34), rgb, (int) frand(3, 6), i % 2 == 0);
    }
}

/* Nothing came back: a grey puff from the portal. */
static void puff(void)
{
    for (int i = 0; i < 8; i++) {
        const float a = (float) i / 8 * 2 * (float) M_PI;
        spawn(e.px, e.py, cosf(a) * 1.6f, sinf(a) * 1.6f - 0.4f, -0.02f, 24, 0x6B6F80, 7, false);
    }
}

static bool parts_step(void)
{
    bool any = false;
    for (int i = 0; i < MAX_PARTS; i++) {
        part_t *p = &e.parts[i];
        if (p->age >= p->life) {
            continue;
        }
        p->x += p->vx;
        p->y += p->vy;
        p->vy += p->g;
        p->vx *= 0.97f;
        p->age++;
        any = true;
    }
    return any;
}

/* ---- Drawing ----------------------------------------------------------------- */

static void disc(lv_layer_t *layer, float x, float y, float r, uint32_t rgb, lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(rgb);
    dsc.bg_opa = opa;
    dsc.radius = LV_RADIUS_CIRCLE;
    const lv_area_t a = {(int32_t) (x - r), (int32_t) (y - r), (int32_t) (x + r), (int32_t) (y + r)};
    lv_draw_rect(layer, &dsc, &a);
}

static void triangle(lv_layer_t *layer, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgb,
                     lv_opa_t opa)
{
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = lv_color_hex(rgb);
    dsc.opa = opa;
    dsc.p[0].x = (int32_t) x0;
    dsc.p[0].y = (int32_t) y0;
    dsc.p[1].x = (int32_t) x1;
    dsc.p[1].y = (int32_t) y1;
    dsc.p[2].x = (int32_t) x2;
    dsc.p[2].y = (int32_t) y2;
    lv_draw_triangle(layer, &dsc);
}

/* A four-pointed star, as Yoru's dizzy ones. */
static void star(lv_layer_t *layer, float x, float y, float r, uint32_t rgb, lv_opa_t opa)
{
    triangle(layer, x - r, y, x + r, y, x, y - 2.2f * r, rgb, opa);
    triangle(layer, x - r, y, x + r, y, x, y + 2.2f * r, rgb, opa);
    triangle(layer, x, y - r, x, y + r, x - 2.2f * r, y, rgb, opa);
    triangle(layer, x, y - r, x, y + r, x + 2.2f * r, y, rgb, opa);
}

static float portal_r(void)
{
    const float t = (float) lv_tick_get() / 1000.0f;
    const float wobble = e.phase == E_INSIDE ? 0.06f * sinf(t * 9) : 0;
    return PORTAL_R * e.open * (1 + wobble + e.gulp);
}

static void draw_portal(lv_layer_t *layer)
{
    if (e.open <= 0.01f) {
        return;
    }
    const float x = CX + e.px, y = CY + e.py, r = portal_r();
    const float t = (float) lv_tick_get() / 1000.0f;
    disc(layer, x, y, r + 10, e.rgb, 50);
    disc(layer, x, y, r, e.rgb, LV_OPA_COVER);
    disc(layer, x, y, r - 7 * e.open, 0x07080D, LV_OPA_COVER);
    // A slow swirl in the hole, faster while Yoru is in there.
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center.x = (int32_t) x;
    arc.center.y = (int32_t) y;
    arc.radius = (uint16_t) LV_MAX(4, r - 16);
    arc.width = 3;
    arc.rounded = 1;
    arc.color = lv_color_mix(lv_color_hex(e.rgb), lv_color_white(), 150);
    arc.opa = LV_OPA_70;
    const float spin = t * (e.phase == E_INSIDE ? 420 : 160);
    for (int i = 0; i < 3; i++) {
        const int32_t a0 = ((int32_t) spin + i * 120) % 360;
        arc.start_angle = a0;
        arc.end_angle = a0 + 55;
        lv_draw_arc(layer, &arc);
    }
    // What it leads to, until Yoru has gone in.
    if (e.phase == E_OPEN || e.phase == E_DIVE) {
        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        dsc.font = &lv_font_montserrat_24;
        dsc.color = lv_color_hex(e.rgb);
        dsc.opa = (lv_opa_t) (255 * e.open);
        dsc.align = LV_TEXT_ALIGN_CENTER;
        dsc.text = e.glyph;
        const lv_area_t a = {(int32_t) x - 24, (int32_t) y - 14, (int32_t) x + 24, (int32_t) y + 16};
        lv_draw_label(layer, &dsc, &a);
    }
}

static void draw_parts(lv_layer_t *layer)
{
    for (int i = 0; i < MAX_PARTS; i++) {
        const part_t *p = &e.parts[i];
        if (p->age >= p->life) {
            continue;
        }
        const lv_opa_t opa = (lv_opa_t) (255 * (1 - (float) p->age / p->life));
        if (p->star) {
            star(layer, CX + p->x, CY + p->y, p->r, p->rgb, opa);
        } else {
            disc(layer, CX + p->x, CY + p->y, p->r * (1 + (float) p->age / p->life), p->rgb, opa);
        }
    }
}

static void draw_cb(lv_event_t *ev)
{
    lv_layer_t *layer = lv_event_get_layer(ev);
    draw_portal(layer);
    draw_parts(layer);
}

/* ---- What to redraw ---------------------------------------------------------- */

static lv_area_t portal_area(void)
{
    const int32_t r = (int32_t) (PORTAL_R * 1.4f) + 12;
    const int32_t x = CX + (int32_t) e.px, y = CY + (int32_t) e.py;
    return (lv_area_t) {x - r, y - r, x + r, y + r};
}

static bool parts_area(lv_area_t *out)
{
    bool any = false;
    for (int i = 0; i < MAX_PARTS; i++) {
        const part_t *p = &e.parts[i];
        if (p->age >= p->life) {
            continue;
        }
        const int32_t m = (int32_t) (p->r * 2.4f) + 3;
        const lv_area_t a = {CX + (int32_t) p->x - m, CY + (int32_t) p->y - m, CX + (int32_t) p->x + m,
                             CY + (int32_t) p->y + m};
        if (!any) {
            *out = a;
        } else {
            out->x1 = LV_MIN(out->x1, a.x1);
            out->y1 = LV_MIN(out->y1, a.y1);
            out->x2 = LV_MAX(out->x2, a.x2);
            out->y2 = LV_MAX(out->y2, a.y2);
        }
        any = true;
    }
    return any;
}

/* The portal stays put, so its box only needs redrawing while it is up. The
 * sparkles: where they all were and where they are, as one box each. */
static void invalidate(void)
{
    const bool portal = e.open > 0.01f;
    if (portal || e.portal_shown) {
        e.portal_drawn = portal_area();
        lv_obj_invalidate_area(e.obj, &e.portal_drawn);
    }
    e.portal_shown = portal;
    lv_area_t now;
    const bool parts = parts_area(&now);
    if (e.parts_shown) {
        lv_obj_invalidate_area(e.obj, &e.parts_drawn);
    }
    if (parts) {
        lv_obj_invalidate_area(e.obj, &now);
        e.parts_drawn = now;
    }
    e.parts_shown = parts;
}

/* ---- The trip ------------------------------------------------------------------- */

static void finish(void)
{
    ESP_LOGI(TAG, "back");
    e.phase = E_IDLE;
    e.open = 0;
    e.nout = 0;
    if (e.timer != NULL) {
        lv_timer_delete(e.timer);
        e.timer = NULL;
    }
    lv_obj_invalidate(e.obj);
    lv_obj_add_flag(e.obj, LV_OBJ_FLAG_HIDDEN);
    if (e.cb != NULL) {
        e.cb(false);
    }
}

/* 0..1 with a little bounce past 1 at the end: the portal pops open. */
static float back_out(float k)
{
    const float c1 = 1.70158f, c3 = c1 + 1, m = k - 1;
    return 1 + c3 * m * m * m + c1 * m * m;
}

static void open_step(void)
{
    e.open = back_out(fminf(1, (float) elapsed() / OPEN_MS));
    if (elapsed() < OPEN_MS) {
        return;
    }
    e.open = 1;
    eyes_gesture(EYES_GESTURE_DIVE);
    go(E_DIVE);
}

static void dive_step(void)
{
    const uint32_t t = elapsed();
    if (t < HANDS_UP_MS) {
        return;
    }
    if (!e.jumped) {
        e.jumped = true;
        // A little wind-up away from the portal, then a dash into it.
        const eyes_pos_t pts[] = {
            at(-e.px * 0.12f, -e.py * 0.12f + 6, 1.05f),
            at(e.px * 0.7f, e.py * 0.7f, 1.8f),
            at(e.px, e.py, 5.0f),
        };
        eyes_move(pts, 3, JUMP_MS, EYES_PATH_DART);
        playback_sound(SOUND_FLUTTER);
        return;
    }
    if (eyes_moving() && eyes_where().z < 4.5f) {
        return;
    }
    // Swallowed: far behind the portal's hole, out of sight.
    eyes_move_to(e.px * INSIDE_Z, e.py * INSIDE_Z, INSIDE_Z, 0, EYES_PATH_GLIDE);
    e.gulp = 0.3f;
    e.next_spark_ms = lv_tick_get() + 200;
    playback_sound(SOUND_BOOP);
    go(E_INSIDE);
}

static void inside_step(void)
{
    const uint32_t now = lv_tick_get();
    if ((int32_t) (now - e.next_spark_ms) >= 0) {
        e.next_spark_ms = now + SPARK_EVERY_MS + esp_random() % 200;
        rummage_spark();
        if (esp_random() % 3 == 0) {
            e.gulp = 0.12f; // something bumps the walls
        }
    }
    const uint32_t t = elapsed();
    if ((e.nout > 0 || t < INSIDE_MIN_MS) && t < INSIDE_MAX_MS) {
        return;
    }
    // Out it hops, in three little hops, home.
    const bool found = e.ok > 0, empty = e.ok == 0 && e.failed > 0;
    const eyes_pos_t pts[] = {
        at(e.px, e.py, 3.4f),
        at(e.px * 0.45f, e.py * 0.45f + 12, 1.4f),
        at(0, 0, 1),
    };
    eyes_move(pts, 3, BACK_MS, EYES_PATH_HOP);
    eyes_look_around();
    if (found) {
        eyes_carry(e.rgb);
        eyes_gesture(EYES_GESTURE_CARRY);
        eyes_set_mood(EYES_MOOD_HAPPY);
        playback_sound(SOUND_BOING);
    } else if (empty) {
        eyes_set_mood(EYES_MOOD_SAD);
        puff();
        playback_sound(SOUND_UH_OH);
    } else {
        eyes_set_mood(EYES_MOOD_NEUTRAL); // it gave up waiting
    }
    go(E_BACK);
}

static void back_step(void)
{
    const uint32_t t = elapsed();
    if (t >= CLOSE_AT_MS) {
        e.open = fmaxf(0, 1 - (float) (t - CLOSE_AT_MS) / CLOSE_MS);
    }
    if (e.ok > 0 && t >= UNWRAP_AT_MS) {
        // Ta-da: the parcel bursts open in its hands.
        eyes_carry(0);
        eyes_poke();
        eyes_set_mood(EYES_MOOD_HAPPY);
        burst(0, PARCEL_DY);
        playback_sound(SOUND_TADA);
        go(E_UNWRAP);
        return;
    }
    if (e.ok == 0 && t >= BACK_MS + 900) {
        go(E_UNWRAP); // just the dust to settle
    }
}

static void timer_cb(lv_timer_t *t)
{
    e.gulp *= 0.82f;
    const bool parts = parts_step();
    switch (e.phase) {
    case E_OPEN:
        open_step();
        break;
    case E_DIVE:
        dive_step();
        break;
    case E_INSIDE:
        inside_step();
        break;
    case E_BACK:
        back_step();
        break;
    case E_UNWRAP:
        e.open = 0;
        if (!parts && !eyes_moving()) {
            invalidate();
            finish();
            return;
        }
        break;
    default:
        break;
    }
    invalidate();
}

/* ---- Calls ------------------------------------------------------------------------ */

void errand_init(lv_obj_t *parent, errand_cb_t cb)
{
    memset(&e, 0, sizeof(e));
    e.cb = cb;
    e.obj = lv_obj_create(parent);
    lv_obj_remove_style_all(e.obj);
    lv_obj_set_size(e.obj, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_center(e.obj);
    // Touches go through to Yoru.
    lv_obj_remove_flag(e.obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(e.obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(e.obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(e.obj, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
}

void errand_start(const char *id, const char *source)
{
    if (e.nout < MAX_OUT) {
        snprintf(e.out[e.nout++], sizeof(e.out[0]), "%s", id);
    }
    if (e.phase != E_IDLE) {
        return; // it is out already, or on its way: one trip for all of them
    }
    ESP_LOGI(TAG, "to %s", source);
    pick_look(source);
    e.ok = e.failed = 0;
    e.jumped = false;
    e.open = e.gulp = 0;
    memset(e.parts, 0, sizeof(e.parts));
    lv_obj_remove_flag(e.obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(e.obj);
    eyes_set_mood(EYES_MOOD_CURIOUS);
    eyes_look_at(e.px / PORTAL_DIST, e.py / PORTAL_DIST);
    playback_sound(SOUND_CHIRP);
    go(E_OPEN);
    e.timer = lv_timer_create(timer_cb, FRAME_MS, NULL);
    if (e.cb != NULL) {
        e.cb(true);
    }
}

void errand_end(const char *id, bool ok)
{
    for (int i = 0; i < e.nout; i++) {
        // An end without its id ends the oldest call.
        if (id[0] == '\0' || strcmp(e.out[i], id) == 0) {
            memmove(e.out[i], e.out[i + 1], (size_t) (e.nout - i - 1) * sizeof(e.out[0]));
            e.nout--;
            if (ok) {
                e.ok++;
            } else {
                e.failed++;
            }
            return;
        }
    }
}

bool errand_busy(void)
{
    return e.phase != E_IDLE;
}

void errand_abort(void)
{
    if (e.phase == E_IDLE) {
        return;
    }
    ESP_LOGI(TAG, "called back");
    eyes_carry(0);
    memset(e.parts, 0, sizeof(e.parts));
    e.phase = E_IDLE;
    e.open = 0;
    e.nout = 0;
    lv_timer_delete(e.timer);
    e.timer = NULL;
    lv_obj_invalidate(e.obj);
    lv_obj_add_flag(e.obj, LV_OBJ_FLAG_HIDDEN);
}
