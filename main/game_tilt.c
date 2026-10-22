#include "game_tilt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "card.h"
#include "game_kit.h"
#include "playback.h"

/* Tilt ball: Yoru curled up into a ball, rolled home through the dark by
 * tilting the case. Pits are holes into the dark he came from (he hates
 * them), the walls bump him, fireflies on the way are a snack, and the
 * glowing hole is the way home. The levels get harder; the last one reached
 * is kept. */

#define CX ((float) KIT_CX)
#define CY ((float) KIT_CY)
#define FRAME_MS 16
/* The arena: a disc inside the screen's rim, its edge a wall. */
#define ARENA_R 214.0f
#define RIM_W 4.0f
#define BALL_R 17.0f
#define GOAL_R 24.0f
/* Rolling: px/s² for 1 g of tilt, the damping per second, the bounce off a
 * wall and the fastest he goes. */
#define GAIN 1400.0f
#define TILT_DEAD 0.025f
#define TILT_MAX 0.6f
#define DAMPING 0.8f
#define BOUNCE 0.45f
#define MAX_V 560.0f
/* A bump this hard is felt (px/s into the wall). */
#define HIT_V 140.0f
#define INTRO_MS 1400
#define FALL_MS 520
#define POP_MS 300
#define SINK_MS 560
#define CHEER_MS 1100
#define MAX_WALLS 9
#define MAX_PITS 9
#define MAX_FLIES 3
#define SAVE_KEY "tilt"

#define COL_WALL lv_color_hex(0x2E3142)
#define COL_RIM lv_color_hex(0x23252F)
#define COL_PIT_LIP lv_color_hex(0x1B1C25)
#define COL_HOME lv_color_hex(0x7FA7FF)
#define COL_FLY lv_color_hex(0xE8FF7A)
#define COL_TITLE lv_color_hex(0x5BE07A)

/* Positions from the middle of the screen. A wall that moves slides by
 * (mx, my) and back over `period` ms. */
typedef struct {
    int16_t x, y, w, h;
    int16_t mx, my;
    uint16_t period;
} wall_t;

typedef struct {
    int16_t x, y, r;
} pit_t;

typedef struct {
    int16_t x, y;
} spot_t;

typedef struct {
    int16_t x, y;
    bool on;
} fly_t;

typedef struct {
    spot_t start, home;
    wall_t walls[MAX_WALLS];
    pit_t pits[MAX_PITS];
    fly_t flies[MAX_FLIES];
} level_t;

/* Zero-sized walls and pits end each list. */
static const level_t LEVELS[] = {
    // 1: just roll him home.
    {.start = {-130, 30}, .home = {130, 30}, .flies = {{0, -40, 1}}},
    // 2: a wall in the way.
    {.start = {-130, -40},
     .home = {130, -40},
     .walls = {{0, -55, 16, 230}},
     .flies = {{0, 120, 1}, {-60, -150, 1}}},
    // 3: the first holes.
    {.start = {0, 160},
     .home = {0, -150},
     .pits = {{-62, 10, 28}, {62, 10, 28}, {0, -70, 26}},
     .flies = {{-130, -40, 1}, {130, -40, 1}}},
    // 4: a zigzag.
    {.start = {0, -165},
     .home = {0, 165},
     .walls = {{-50, -70, 320, 16}, {50, 70, 320, 16}},
     .flies = {{150, -110, 1}, {0, 0, 1}, {-150, 110, 1}}},
    // 5: a field of holes.
    {.start = {-160, 0},
     .home = {160, 0},
     .pits = {{-80, -50, 24}, {-80, 50, 24}, {0, 0, 26}, {80, -50, 24}, {80, 50, 24}, {0, -105, 22}, {0, 105, 22}},
     .flies = {{-40, 0, 1}, {40, 0, 1}, {0, -165, 1}}},
    // 6: home in a box, its door guarded.
    {.start = {-165, 0},
     .home = {0, 0},
     .walls = {{0, -72, 160, 16}, {0, 72, 160, 16}, {-72, 0, 16, 160}},
     .pits = {{120, 0, 30}},
     .flies = {{0, -150, 1}, {0, 150, 1}, {165, 0, 1}}},
    // 7: a gate that slides.
    {.start = {-150, 0},
     .home = {150, 0},
     .walls = {{0, -160, 16, 120}, {0, 160, 16, 120}, {0, 0, 16, 120, 0, 70, 2600}},
     .flies = {{-90, -130, 1}, {90, 130, 1}}},
    // 8: the zigzag again, with holes.
    {.start = {0, -165},
     .home = {0, 165},
     .walls = {{-50, -70, 320, 16}, {50, 70, 320, 16}},
     .pits = {{-30, 5, 22}, {85, -10, 22}, {100, -125, 18}, {-100, 125, 18}},
     .flies = {{-120, -10, 1}, {35, -150, 1}}},
    // 9: two gates out of step.
    {.start = {-175, 0},
     .home = {175, 0},
     .walls = {{-70, -160, 16, 130},
               {-70, 160, 16, 130},
               {-70, 0, 16, 110, 0, 60, 2200},
               {70, -160, 16, 130},
               {70, 160, 16, 130},
               {70, 0, 16, 110, 0, -60, 1700}},
     .pits = {{0, -60, 22}, {0, 60, 22}},
     .flies = {{0, 0, 1}, {0, -130, 1}, {0, 130, 1}}},
    // 10: home in a ring of holes, a bar sweeping past.
    {.start = {0, 170},
     .home = {0, -10},
     .walls = {{0, -150, 110, 16, 70, 0, 3000}},
     .pits = {{80, -10, 22}, {40, 59, 22}, {-40, 59, 22}, {-80, -10, 22}, {-40, -79, 22}, {40, -79, 22}},
     .flies = {{-150, -60, 1}, {150, -60, 1}, {0, -185, 1}}},
};
#define LEVEL_COUNT ((int) (sizeof(LEVELS) / sizeof(LEVELS[0])))

typedef enum { ST_INTRO, ST_PLAY, ST_FALL, ST_POP, ST_SINK, ST_CHEER, ST_DONE } state_t;

static struct {
    lv_obj_t *obj;
    lv_timer_t *timer;
    void (*quit)(void);
    int level;
    const level_t *lv;
    state_t st;
    uint32_t st_ms, last_ms, activity_ms, sound_ms, gasp_ms;
    /* Yoru: where, how fast, how squashed, how he looks. */
    float x, y, vx, vy, size, sq_x, sq_y;
    float gx, gy;
    uint32_t hit_ms, happy_ms;
    int fear_pit;
    /* Where he fell, or the hole he goes home through. */
    float hole_x, hole_y;
    kit_face_t face;
    lv_area_t ball_drawn;
    /* The walls that move: where they are now and where they were drawn. */
    float wx[MAX_WALLS], wy[MAX_WALLS];
    lv_area_t wall_drawn[MAX_WALLS];
    bool eaten[MAX_FLIES];
    lv_area_t fly_drawn[MAX_FLIES];
    int eaten_count;
    uint8_t glow;
    /* What's written in the middle, and where it was drawn. */
    char note[32], note2[40];
    lv_area_t note_drawn;
    uint32_t sig;
} s;

static bool wall_used(const wall_t *w)
{
    return w->w > 0;
}

static bool pit_used(const pit_t *p)
{
    return p->r > 0;
}

static bool fly_exists(int i)
{
    return s.lv->flies[i].on;
}

static void sound(sound_t snd, uint32_t now)
{
    if (now - s.sound_ms > 160) {
        s.sound_ms = now;
        playback_sound(snd);
    }
}

/* ---- Where things are ------------------------------------------------------------ */

static lv_area_t wall_area(int i)
{
    const wall_t *w = &s.lv->walls[i];
    const float x = CX + s.wx[i], y = CY + s.wy[i];
    return (lv_area_t) {(int32_t) lroundf(x - w->w / 2.0f), (int32_t) lroundf(y - w->h / 2.0f),
                        (int32_t) lroundf(x + w->w / 2.0f) - 1, (int32_t) lroundf(y + w->h / 2.0f) - 1};
}

static void fly_at(int i, uint32_t now, float *x, float *y)
{
    // They hover a little, each in its own time.
    const float t = (float) now / 1000.0f + 1.7f * (float) i;
    *x = CX + s.lv->flies[i].x + 3 * sinf(1.3f * t);
    *y = CY + s.lv->flies[i].y + 3 * sinf(2.1f * t + 0.5f);
}

static lv_area_t ball_area(void)
{
    const float r = s.size * BALL_R;
    return kit_yoru_area(CX + s.x, CY + s.y, r * s.sq_x, r * s.sq_y);
}

/* ---- Drawing ------------------------------------------------------------------- */

static bool clip_meets(const lv_area_t *clip, const lv_area_t *a)
{
    return kit_overlap(clip, a);
}

/* The arena's edge, with a gap at the top for the title. An arc's mask is
 * worked out in every area it is asked to draw in: only where it shows. */
static void draw_rim(lv_layer_t *layer, const lv_area_t *clip)
{
    float near = 1e9f, far = 0;
    const float xs[2] = {(float) clip->x1 - CX, (float) clip->x2 + 1 - CX};
    const float ys[2] = {(float) clip->y1 - CY, (float) clip->y2 + 1 - CY};
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            far = fmaxf(far, hypotf(xs[i], ys[j]));
        }
    }
    const float nx = xs[0] > 0 ? xs[0] : xs[1] < 0 ? xs[1] : 0;
    const float ny = ys[0] > 0 ? ys[0] : ys[1] < 0 ? ys[1] : 0;
    near = hypotf(nx, ny);
    if (far < ARENA_R - 1 || near > ARENA_R + RIM_W + 1) {
        return;
    }
    kit_arc(layer, CX, CY, ARENA_R + RIM_W, RIM_W, 292, 248 + 360, COL_RIM);
}

static void draw_home(lv_layer_t *layer)
{
    const float x = CX + s.lv->home.x, y = CY + s.lv->home.y;
    const float k = 0.75f + 0.25f * (float) s.glow / 255.0f;
    kit_disc(layer, x, y, GOAL_R + 12, kit_dim(COL_HOME, 0.10f * k));
    kit_disc(layer, x, y, GOAL_R + 6, kit_dim(COL_HOME, 0.22f * k));
    kit_disc(layer, x, y, GOAL_R, kit_dim(COL_HOME, 0.9f * k));
    kit_disc(layer, x, y, GOAL_R - 4, lv_color_hex(0x0A0E1C));
}

static void draw_pit(lv_layer_t *layer, const pit_t *p)
{
    const float x = CX + p->x, y = CY + p->y;
    kit_disc(layer, x, y, p->r + 3, COL_PIT_LIP);
    kit_disc(layer, x, y, p->r - 1, lv_color_hex(0x050508));
}

static void draw_fly(lv_layer_t *layer, int i, uint32_t now)
{
    float x, y;
    fly_at(i, now, &x, &y);
    const float k = 0.6f + 0.4f * sinf((float) now / 240.0f + 2.3f * (float) i);
    kit_disc(layer, x, y, 8, kit_dim(COL_FLY, 0.16f * k));
    kit_disc(layer, x, y, 5, kit_dim(COL_FLY, 0.4f * k));
    kit_disc(layer, x, y, 2.5f, kit_dim(COL_FLY, 0.6f + 0.4f * k));
}

static void title(char *out, size_t len)
{
    snprintf(out, len, LV_SYMBOL_LEFT "  %s %d", card_tr("Level", "Livello"), s.level + 1);
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_area_t *clip = &layer->_clip_area;
    const uint32_t now = lv_tick_get();
    draw_rim(layer, clip);
    char str[32];
    title(str, sizeof(str));
    const lv_area_t ta = kit_text_area(&lv_font_montserrat_20, CX, 30, str);
    if (clip_meets(clip, &ta)) {
        kit_text(layer, &lv_font_montserrat_20, CX, 30, COL_TITLE, str);
    }
    const lv_area_t ha = kit_disc_area(CX + s.lv->home.x, CY + s.lv->home.y, GOAL_R + 12);
    if (clip_meets(clip, &ha)) {
        draw_home(layer);
    }
    for (int i = 0; i < MAX_PITS && pit_used(&s.lv->pits[i]); i++) {
        const pit_t *p = &s.lv->pits[i];
        const lv_area_t a = kit_disc_area(CX + p->x, CY + p->y, p->r + 3);
        if (clip_meets(clip, &a)) {
            draw_pit(layer, p);
        }
    }
    for (int i = 0; i < MAX_WALLS && wall_used(&s.lv->walls[i]); i++) {
        const lv_area_t a = wall_area(i);
        if (clip_meets(clip, &a)) {
            kit_rect(layer, &a, 6, COL_WALL);
        }
    }
    for (int i = 0; i < MAX_FLIES; i++) {
        if (fly_exists(i) && !s.eaten[i] && clip_meets(clip, &s.fly_drawn[i])) {
            draw_fly(layer, i, now);
        }
    }
    if (s.st != ST_INTRO && s.st != ST_DONE && s.size > 0.05f) {
        const float r = s.size * BALL_R;
        kit_yoru(layer, CX + s.x, CY + s.y, r * s.sq_x, r * s.sq_y, &s.face);
    }
    if (s.note[0] != '\0' && clip_meets(clip, &s.note_drawn)) {
        kit_text(layer, &lv_font_montserrat_40, CX, CY - 14, lv_color_hex(0xF2F2F5), s.note);
        if (s.note2[0] != '\0') {
            kit_text(layer, &lv_font_montserrat_20, CX, CY + 30, lv_color_hex(0x8C90A6), s.note2);
        }
    }
}

/* ---- The levels --------------------------------------------------------------- */

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
    lv_area_t a = kit_text_area(&lv_font_montserrat_40, CX, CY - 14, s.note);
    if (s.note2[0] != '\0') {
        const lv_area_t b = kit_text_area(&lv_font_montserrat_20, CX, CY + 30, s.note2);
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

static void place_walls(uint32_t now)
{
    for (int i = 0; i < MAX_WALLS && wall_used(&s.lv->walls[i]); i++) {
        const wall_t *w = &s.lv->walls[i];
        float k = 0;
        if (w->period > 0) {
            k = sinf(2 * (float) M_PI * (float) (now % w->period) / (float) w->period);
        }
        s.wx[i] = w->x + k * w->mx;
        s.wy[i] = w->y + k * w->my;
    }
}

static void load_level(int level, uint32_t now)
{
    s.level = level;
    s.lv = &LEVELS[level];
    memset(s.eaten, 0, sizeof(s.eaten));
    s.eaten_count = 0;
    s.x = s.lv->start.x;
    s.y = s.lv->start.y;
    s.vx = s.vy = 0;
    s.size = 0;
    s.sq_x = s.sq_y = 1;
    s.ball_drawn = (lv_area_t) {0, 0, -1, -1};
    place_walls(now);
    for (int i = 0; i < MAX_WALLS; i++) {
        s.wall_drawn[i] = wall_used(&s.lv->walls[i]) ? wall_area(i) : (lv_area_t) {0, 0, -1, -1};
    }
    for (int i = 0; i < MAX_FLIES; i++) {
        float x, y;
        fly_at(i, now, &x, &y);
        s.fly_drawn[i] = kit_disc_area(x, y, 9);
    }
    s.note[0] = '\0';
    lv_obj_invalidate(s.obj); // a new board: the one full frame
    char big[24];
    snprintf(big, sizeof(big), "%s %d", card_tr("Level", "Livello"), level + 1);
    set_note(big, level == 0 ? card_tr("Tilt to roll Yoru home", "Inclina per portarlo a casa") : NULL);
    set_state(ST_INTRO, now);
}

/* ---- Rolling ------------------------------------------------------------------ */

/* Out of a wall, bouncing; how hard it hit (px/s into it). */
static float collide_rect(float l, float t, float r, float b, float *nx_out, float *ny_out)
{
    const float qx = kit_clamp(s.x, l, r), qy = kit_clamp(s.y, t, b);
    float dx = s.x - qx, dy = s.y - qy;
    float d = hypotf(dx, dy);
    if (d >= BALL_R) {
        return 0;
    }
    float nx, ny;
    if (d > 0.01f) {
        nx = dx / d;
        ny = dy / d;
    } else {
        // His middle inside it (a moving wall caught him): out the near side.
        const float el = s.x - l, er = r - s.x, et = s.y - t, eb = b - s.y;
        const float m = fminf(fminf(el, er), fminf(et, eb));
        nx = m == el ? -1 : m == er ? 1 : 0;
        ny = nx != 0 ? 0 : m == et ? -1 : 1;
        d = -(m == el ? el : m == er ? er : m == et ? et : eb);
    }
    s.x += nx * (BALL_R - d);
    s.y += ny * (BALL_R - d);
    const float vn = s.vx * nx + s.vy * ny;
    if (vn >= 0) {
        return 0;
    }
    s.vx -= (1 + BOUNCE) * vn * nx;
    s.vy -= (1 + BOUNCE) * vn * ny;
    *nx_out = nx;
    *ny_out = ny;
    return -vn;
}

static float collide(float *nx, float *ny)
{
    float hit = 0;
    const float d = hypotf(s.x, s.y), lim = ARENA_R - BALL_R;
    if (d > lim) {
        const float ux = s.x / d, uy = s.y / d;
        s.x = ux * lim;
        s.y = uy * lim;
        const float vn = s.vx * ux + s.vy * uy;
        if (vn > 0) {
            s.vx -= (1 + BOUNCE) * vn * ux;
            s.vy -= (1 + BOUNCE) * vn * uy;
            hit = vn;
            *nx = -ux;
            *ny = -uy;
        }
    }
    for (int i = 0; i < MAX_WALLS && wall_used(&s.lv->walls[i]); i++) {
        const wall_t *w = &s.lv->walls[i];
        float wnx = 0, wny = 0;
        const float v = collide_rect(s.wx[i] - w->w / 2.0f, s.wy[i] - w->h / 2.0f, s.wx[i] + w->w / 2.0f,
                                     s.wy[i] + w->h / 2.0f, &wnx, &wny);
        if (v > hit) {
            hit = v;
            *nx = wnx;
            *ny = wny;
        }
    }
    return hit;
}

/* A hole's lip pulls him in; past it he falls. -1, or the pit he fell in
 * (MAX_PITS for home). */
static int holes(float dt, uint32_t now)
{
    s.fear_pit = -1;
    float nearest = 1e9f;
    for (int i = 0; i <= MAX_PITS; i++) {
        const bool home = i == MAX_PITS;
        if (!home && !pit_used(&s.lv->pits[i])) {
            continue;
        }
        const float px = home ? s.lv->home.x : s.lv->pits[i].x, py = home ? s.lv->home.y : s.lv->pits[i].y;
        const float pr = home ? GOAL_R : s.lv->pits[i].r;
        const float dx = px - s.x, dy = py - s.y, d = hypotf(dx, dy);
        if (d < pr - 0.35f * BALL_R) {
            s.hole_x = px;
            s.hole_y = py;
            return i;
        }
        if (d < pr + 0.5f * BALL_R && d > 0.01f) {
            // On the lip: it slopes in.
            const float pull = 900.0f * (1 - (d - (pr - 0.35f * BALL_R)) / (0.85f * BALL_R));
            s.vx += dx / d * pull * dt;
            s.vy += dy / d * pull * dt;
        }
        if (!home && d - pr < 46 && d < nearest) {
            nearest = d;
            s.fear_pit = i;
        }
    }
    if (s.fear_pit >= 0 && now - s.gasp_ms > 2500) {
        s.gasp_ms = now;
        sound(SOUND_GASP, now);
    }
    return -1;
}

static void eat_flies(uint32_t now)
{
    for (int i = 0; i < MAX_FLIES; i++) {
        if (!fly_exists(i) || s.eaten[i]) {
            continue;
        }
        float x, y;
        fly_at(i, now, &x, &y);
        if (hypotf(x - CX - s.x, y - CY - s.y) < BALL_R + 7) {
            s.eaten[i] = true;
            s.eaten_count++;
            s.happy_ms = now;
            kit_dirty(s.obj, s.fly_drawn[i]);
            sound(SOUND_CHIRP, now);
        }
    }
}

static void roll(float dt, uint32_t now)
{
    float tx, ty;
    kit_tilt(&tx, &ty);
    const float mag = hypotf(tx, ty);
    if (mag < TILT_DEAD) {
        tx = ty = 0;
    } else {
        const float k = (fminf(mag, TILT_MAX) - TILT_DEAD) / mag; // past the dead zone, up to a cap
        tx *= k;
        ty *= k;
    }
    if (mag > 0.08f) {
        s.activity_ms = now; // tilting is playing: the menu stays
    }
    // Two half steps: at full speed he moves ~9 px a frame, half a wall.
    const float h = dt / 2;
    float hit = 0, hnx = 0, hny = 0;
    for (int step = 0; step < 2; step++) {
        s.vx += tx * GAIN * h;
        s.vy += ty * GAIN * h;
        const float damp = 1 - DAMPING * h;
        s.vx *= damp;
        s.vy *= damp;
        const float v = hypotf(s.vx, s.vy);
        if (v > MAX_V) {
            s.vx *= MAX_V / v;
            s.vy *= MAX_V / v;
        }
        s.x += s.vx * h;
        s.y += s.vy * h;
        float nx = 0, ny = 0;
        const float hv = collide(&nx, &ny);
        if (hv > hit) {
            hit = hv;
            hnx = nx;
            hny = ny;
        }
    }
    if (hit > HIT_V) {
        s.hit_ms = now;
        // Squashed against what he hit, more the harder.
        const float k = fminf(0.28f, hit / 1500.0f);
        if (fabsf(hnx) > fabsf(hny)) {
            s.sq_x = 1 - k;
            s.sq_y = 1 + 0.6f * k;
        } else {
            s.sq_x = 1 + 0.6f * k;
            s.sq_y = 1 - k;
        }
        sound(hit > 380 ? SOUND_BOING : SOUND_BOOP, now);
    }
    // The squash springs back.
    s.sq_x += (1 - s.sq_x) * fminf(1, dt * 12);
    s.sq_y += (1 - s.sq_y) * fminf(1, dt * 12);
    eat_flies(now);
    const int fell = holes(dt, now);
    if (fell == MAX_PITS) {
        set_state(ST_SINK, now);
        sound(SOUND_HAPPY, now);
    } else if (fell >= 0) {
        set_state(ST_FALL, now);
        playback_sound(SOUND_EEP);
    }
}

/* ---- His face ------------------------------------------------------------------ */

static void face(uint32_t now)
{
    const float v = hypotf(s.vx, s.vy);
    float tgx, tgy;
    kit_eyes_t eyes = KIT_EYES_OPEN;
    if (s.st == ST_FALL) {
        eyes = KIT_EYES_WIDE;
        tgx = 0;
        tgy = -0.6f; // up at you, as he goes
    } else if (s.st == ST_SINK || s.st == ST_CHEER || now - s.happy_ms < 650) {
        eyes = KIT_EYES_HAPPY;
        tgx = tgy = 0;
    } else if (now - s.hit_ms < 260) {
        eyes = KIT_EYES_SHUT;
        tgx = tgy = 0;
    } else if (s.fear_pit >= 0) {
        // He stares at the hole.
        const pit_t *p = &s.lv->pits[s.fear_pit];
        const float dx = p->x - s.x, dy = p->y - s.y, d = fmaxf(1, hypotf(dx, dy));
        eyes = KIT_EYES_WIDE;
        tgx = dx / d * 0.8f;
        tgy = dy / d * 0.8f;
    } else if (v > 30) {
        // His eyes lead the way, and he's thrilled when it's fast.
        tgx = s.vx / 300;
        tgy = s.vy / 300;
        const float m = hypotf(tgx, tgy);
        if (m > 0.9f) {
            tgx *= 0.9f / m;
            tgy *= 0.9f / m;
        }
        if (v > 420) {
            eyes = KIT_EYES_WIDE;
        }
    } else {
        // Still: a look home, then at you.
        const float dx = s.lv->home.x - s.x, dy = s.lv->home.y - s.y, d = fmaxf(1, hypotf(dx, dy));
        const bool home = (now / 1600) % 3 == 0;
        tgx = home ? dx / d * 0.6f : 0;
        tgy = home ? dy / d * 0.6f : -0.15f;
    }
    s.gx += (tgx - s.gx) * 0.25f;
    s.gy += (tgy - s.gy) * 0.25f;
    s.face.gx = s.gx;
    s.face.gy = s.gy;
    s.face.eyes = eyes;
    s.face.lid = eyes == KIT_EYES_OPEN ? kit_blink(now) : 1;
}

/* What the ball's picture depends on, roughly: redrawn when it changes. */
static uint32_t ball_sig(void)
{
    uint32_t h = (uint32_t) lroundf(s.x * 2) * 31 + (uint32_t) lroundf(s.y * 2);
    h = h * 31 + (uint32_t) lroundf(s.gx * 24) + 97 * (uint32_t) lroundf(s.gy * 24);
    h = h * 31 + (uint32_t) s.face.eyes * 7 + (uint32_t) lroundf(s.face.lid * 8);
    h = h * 31 + (uint32_t) lroundf(s.size * 40) + 53 * (uint32_t) lroundf(s.sq_x * 50) +
        (uint32_t) lroundf(s.sq_y * 50);
    return h;
}

/* ---- The frame --------------------------------------------------------------- */

static float ease_out(float k)
{
    return 1 - (1 - k) * (1 - k);
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
            kit_tilt_zero(); // level is however it lies now
            set_state(ST_POP, now);
            playback_sound(SOUND_BOOP);
        }
        break;
    case ST_POP:
        s.size = ease_out(fminf(1, (float) in / POP_MS)) * 1.1f;
        if (in >= POP_MS) {
            s.size = 1;
            s.vx = s.vy = 0;
            set_state(ST_PLAY, now);
        }
        break;
    case ST_PLAY:
        roll(dt, now);
        break;
    case ST_FALL:
    case ST_SINK: {
        // Into the hole, smaller and smaller.
        const float k = fminf(1, (float) in / (s.st == ST_FALL ? FALL_MS : SINK_MS));
        s.x += (s.hole_x - s.x) * fminf(1, dt * 14);
        s.y += (s.hole_y - s.y) * fminf(1, dt * 14);
        s.size = 1 - k;
        if (k >= 1) {
            if (s.st == ST_FALL) {
                // Back at the start, a little shaken.
                s.x = s.lv->start.x;
                s.y = s.lv->start.y;
                s.vx = s.vy = 0;
                set_state(ST_POP, now);
                playback_sound(SOUND_WHIMPER);
            } else {
                char small[40];
                int flies = 0;
                for (int i = 0; i < MAX_FLIES; i++) {
                    flies += fly_exists(i);
                }
                snprintf(small, sizeof(small), "%s %d/%d", card_tr("Fireflies", "Lucciole"), s.eaten_count, flies);
                set_note(s.level + 1 < LEVEL_COUNT ? card_tr("Home!", "A casa!") : card_tr("All done!", "Finito!"),
                         small);
                set_state(ST_CHEER, now);
                playback_sound(s.level + 1 < LEVEL_COUNT ? SOUND_TADA : SOUND_EXCITED);
                const int next = (s.level + 1) % LEVEL_COUNT;
                kit_save(SAVE_KEY, next);
            }
        }
        break;
    }
    case ST_CHEER:
        if (in >= CHEER_MS + (s.level + 1 == LEVEL_COUNT ? 1200 : 0)) {
            load_level((s.level + 1) % LEVEL_COUNT, now);
        }
        break;
    default:
        break;
    }
    face(now);
    place_walls(now);
    for (int i = 0; i < MAX_WALLS && wall_used(&s.lv->walls[i]); i++) {
        if (s.lv->walls[i].period > 0) {
            kit_moved(s.obj, &s.wall_drawn[i], wall_area(i));
        }
    }
    // The fireflies glow and hover: a few times a second is enough.
    if (now / 60 != (now - (uint32_t) (dt * 1000)) / 60) {
        for (int i = 0; i < MAX_FLIES; i++) {
            if (fly_exists(i) && !s.eaten[i]) {
                float x, y;
                fly_at(i, now, &x, &y);
                kit_moved(s.obj, &s.fly_drawn[i], kit_disc_area(x, y, 9));
            }
        }
    }
    const uint8_t glow = (uint8_t) (127.5f + 127.5f * sinf((float) now / 450.0f));
    if (abs((int) glow - (int) s.glow) > 24) {
        s.glow = glow;
        kit_dirty(s.obj, kit_disc_area(CX + s.lv->home.x, CY + s.lv->home.y, GOAL_R + 12));
    }
    const uint32_t sig = ball_sig();
    if (sig != s.sig) {
        s.sig = sig;
        kit_moved(s.obj, &s.ball_drawn, ball_area());
    }
    if (now - s.activity_ms < 1000 || s.st != ST_PLAY) {
        lv_display_trigger_activity(NULL);
    }
}

/* ---- Touch ------------------------------------------------------------------ */

static void quit_async(void *arg)
{
    if (s.quit != NULL) {
        s.quit();
    }
}

static void clicked_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (p.y < 70) {
        lv_async_call(quit_async, NULL); // the title: back to the games
        return;
    }
    const uint32_t now = lv_tick_get();
    s.activity_ms = now;
    if (s.st == ST_PLAY && hypotf((float) p.x - CX - s.x, (float) p.y - CY - s.y) < BALL_R + 14) {
        // A poke: he giggles.
        s.happy_ms = now;
        sound(SOUND_GIGGLE, now);
    } else if (s.st == ST_PLAY) {
        // Tapping the black: "level" is however it lies now.
        kit_tilt_zero();
    }
}

static void deleted_cb(lv_event_t *e)
{
    if (s.timer != NULL) {
        lv_timer_delete(s.timer);
    }
    memset(&s, 0, sizeof(s));
}

lv_obj_t *game_tilt_start(lv_obj_t *parent, void (*quit)(void))
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
    lv_obj_add_event_cb(s.obj, clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s.obj, deleted_cb, LV_EVENT_DELETE, NULL);
    const uint32_t now = lv_tick_get();
    s.last_ms = s.activity_ms = now;
    s.fear_pit = -1;
    int level = (int) kit_load(SAVE_KEY);
    load_level(level >= 0 && level < LEVEL_COUNT ? level : 0, now);
    s.timer = lv_timer_create(tick_cb, FRAME_MS, NULL);
    return s.obj;
}

void game_tilt_icon(lv_layer_t *layer, float x, float y, float r)
{
    // A hole of light and Yoru rolling towards it.
    kit_disc(layer, x + 0.32f * r, y - 0.28f * r, 0.3f * r, kit_dim(COL_HOME, 0.9f));
    kit_disc(layer, x + 0.32f * r, y - 0.28f * r, 0.2f * r, lv_color_hex(0x0A0E1C));
    const kit_face_t f = {.gx = 0.6f, .gy = -0.5f, .lid = 1, .eyes = KIT_EYES_OPEN};
    kit_yoru(layer, x - 0.2f * r, y + 0.22f * r, 0.36f * r, 0.36f * r, &f);
}
