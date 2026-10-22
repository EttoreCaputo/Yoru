#include "game_sling.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "card.h"
#include "game_kit.h"
#include "playback.h"

/* Slingshot: Yoru finds his way home across the dark, the way he came. Pull
 * him back and let go: he flies, and the little planets bend his path with
 * their pull. Black holes swallow him (back to the start, shaken), planets
 * bump him, and past the edge he drifts off into the dark. The light is
 * home. Fewer shots is better; the last level reached is kept. */

#define CX ((float) KIT_CX)
#define CY ((float) KIT_CY)
#define FRAME_MS 16
#define BALL_R 14.0f
#define GOAL_R 22.0f
/* Pulled this far he goes this fast; the pull is drawn as dots. */
#define PULL_MAX 110.0f
#define LAUNCH_V 470.0f
#define GRAB_R 56.0f
#define DOTS 8
#define DOT_R 3.0f
/* Each planet pulls with GM = G_PLANET r², a black hole much harder. */
#define G_PLANET 2600.0f
#define G_HOLE 9.0e6f
#define SUBSTEPS 4
/* Lost past this distance from the middle, or after this long. */
#define LOST_R 250.0f
#define FLIGHT_MS 9000
#define SINK_MS 520
#define CHEER_MS 1300
#define INTRO_MS 1300
#define MAX_BODIES 6
#define SAVE_KEY "sling"

#define COL_HOME lv_color_hex(0x7FA7FF)
#define COL_TITLE lv_color_hex(0x5BE07A)
#define COL_DOT lv_color_hex(0x8C90A6)
#define COL_PAD lv_color_hex(0x262833)

typedef enum { B_NONE, B_PLANET, B_HOLE, B_MOON } body_kind_t;

/* A planet or a hole sits still; a moon goes round body `of` at distance
 * `orbit` once every `period` ms. */
typedef struct {
    body_kind_t kind;
    int16_t x, y, r;
    uint32_t top, bottom;
    int8_t of;
    int16_t orbit;
    int16_t period; // negative: the other way round
    int16_t phase;  // degrees
} body_t;

typedef struct {
    int16_t sx, sy, hx, hy;
    body_t bodies[MAX_BODIES];
} level_t;

#define PLANET(x, y, r, top, bottom) {B_PLANET, x, y, r, top, bottom, 0, 0, 0, 0}
#define HOLE(x, y, r) {B_HOLE, x, y, r, 0, 0, 0, 0, 0, 0}
#define MOON(of, orbit, period, phase, r) {B_MOON, 0, 0, r, 0xB8B8C4, 0x5C5C68, of, orbit, period, phase}

static const level_t LEVELS[] = {
    // 1: round the planet.
    {-150, 120, 140, -110, {PLANET(0, 0, 34, 0xFF9F6B, 0xB8432A)}},
    // 2: between two.
    {0, 175, 0, -160, {PLANET(-62, -10, 26, 0x7FD6A0, 0x2A7A55), PLANET(70, 15, 28, 0xC49BFF, 0x5B3AA8)}},
    // 3: a big one to swing round.
    {-170, 30, 170, -30, {PLANET(0, 0, 48, 0xFFD36B, 0xB87A1E)}},
    // 4: home hiding behind a planet.
    {0, 178, 0, -70, {PLANET(0, 40, 36, 0x6BC8FF, 0x1E5E9E), PLANET(-125, -95, 20, 0xFF7FA8, 0xA83A62)}},
    // 5: the first hole into the dark.
    {-165, 105, 155, 110, {HOLE(0, 60, 16), PLANET(0, -110, 30, 0x9BE07A, 0x3E8A2A)}},
    // 6: a moon in the way.
    {0, 178, 0, -170, {PLANET(0, 0, 30, 0xFF9F6B, 0xB8432A), MOON(0, 74, 4200, 0, 10)}},
    // 7: two holes and a planet to thread them.
    {-175, -20, 172, 25, {HOLE(-55, 95, 15), HOLE(60, -95, 15), PLANET(0, 0, 30, 0x6BC8FF, 0x1E5E9E)}},
    // 8: home in the middle, two moons round it.
    {0, 182, 0, 0,
     {PLANET(-115, -70, 22, 0xC49BFF, 0x5B3AA8), PLANET(115, -70, 22, 0xFFD36B, 0xB87A1E), MOON(-1, 62, 3600, 0, 9),
      MOON(-1, 62, -3600, 180, 9)}},
};
#define LEVEL_COUNT ((int) (sizeof(LEVELS) / sizeof(LEVELS[0])))

typedef enum { ST_INTRO, ST_AIM, ST_FLY, ST_LOST, ST_CRASH, ST_SWALLOW, ST_SINK, ST_CHEER } state_t;

static struct {
    lv_obj_t *obj;
    lv_timer_t *timer;
    void (*quit)(void);
    int level;
    const level_t *lv;
    state_t st;
    uint32_t st_ms, last_ms, fly_ms, gasp_ms;
    int shots;
    /* Yoru, and how far he is pulled back while aiming. */
    float x, y, vx, vy, size, px, py;
    bool grabbed;
    float gx, gy;
    float hole_x, hole_y;
    kit_face_t face;
    lv_area_t ball_drawn;
    uint32_t sig;
    /* Where each body is now (moons move) and where it was drawn. */
    float bx[MAX_BODIES], by[MAX_BODIES];
    lv_area_t body_drawn[MAX_BODIES];
    /* The aim's dots. */
    float dx[DOTS], dy[DOTS];
    int dots;
    lv_area_t dots_drawn[DOTS];
    char note[32], note2[40];
    lv_area_t note_drawn;
    uint8_t glow;
} s;

static bool body_used(int i)
{
    const body_t *b = &s.lv->bodies[i];
    return b->kind != B_NONE && b->r > 0;
}

/* ---- Where things are ---------------------------------------------------------- */

static void place_bodies(uint32_t now)
{
    for (int i = 0; i < MAX_BODIES; i++) {
        const body_t *b = &s.lv->bodies[i];
        if (b->kind != B_MOON) {
            s.bx[i] = b->x;
            s.by[i] = b->y;
            continue;
        }
        // Round the body it belongs to, or the home light (-1).
        const float ox = b->of >= 0 ? s.bx[b->of] : s.lv->hx, oy = b->of >= 0 ? s.by[b->of] : s.lv->hy;
        float a = (float) b->phase * (float) M_PI / 180.0f;
        if (b->period != 0) {
            const uint32_t p = (uint32_t) abs(b->period);
            a += (b->period > 0 ? 1 : -1) * 2 * (float) M_PI * (float) (now % p) / (float) p;
        }
        s.bx[i] = ox + b->orbit * cosf(a);
        s.by[i] = oy + b->orbit * sinf(a);
    }
}

static lv_area_t body_area(int i)
{
    const float r = s.lv->bodies[i].r + (s.lv->bodies[i].kind == B_HOLE ? 8 : 1);
    return kit_disc_area(CX + s.bx[i], CY + s.by[i], r);
}

static lv_area_t ball_area(void)
{
    const float r = s.size * BALL_R;
    return kit_yoru_area(CX + s.x, CY + s.y, r, r);
}

/* ---- Drawing -------------------------------------------------------------------- */

static void draw_body(lv_layer_t *layer, int i)
{
    const body_t *b = &s.lv->bodies[i];
    const float x = CX + s.bx[i], y = CY + s.by[i], r = b->r;
    if (b->kind == B_HOLE) {
        // A hole into the dark: a faint ring of what it is swallowing.
        kit_disc(layer, x, y, r + 8, lv_color_hex(0x140E22));
        kit_disc(layer, x, y, r + 4, lv_color_hex(0x2A1C48));
        kit_disc(layer, x, y, r, lv_color_black());
        return;
    }
    // Lit from the top left, as Yoru is.
    kit_disc(layer, x, y, r, lv_color_hex(b->bottom));
    kit_disc(layer, x - 0.16f * r, y - 0.16f * r, 0.78f * r, lv_color_mix(lv_color_hex(b->top), lv_color_hex(b->bottom), 170));
    kit_disc(layer, x - 0.3f * r, y - 0.3f * r, 0.45f * r, lv_color_hex(b->top));
}

static void draw_home(lv_layer_t *layer)
{
    const float x = CX + s.lv->hx, y = CY + s.lv->hy;
    const float k = 0.75f + 0.25f * (float) s.glow / 255.0f;
    kit_disc(layer, x, y, GOAL_R + 12, kit_dim(COL_HOME, 0.10f * k));
    kit_disc(layer, x, y, GOAL_R + 6, kit_dim(COL_HOME, 0.22f * k));
    kit_disc(layer, x, y, GOAL_R, kit_dim(COL_HOME, 0.9f * k));
    kit_disc(layer, x, y, GOAL_R - 4, lv_color_hex(0x0A0E1C));
}

static void title(char *out, size_t len)
{
    snprintf(out, len, LV_SYMBOL_LEFT "  %s %d", card_tr("Level", "Livello"), s.level + 1);
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_area_t *clip = &layer->_clip_area;
    char str[32];
    title(str, sizeof(str));
    const lv_area_t ta = kit_text_area(&lv_font_montserrat_20, CX, 30, str);
    if (kit_overlap(clip, &ta)) {
        kit_text(layer, &lv_font_montserrat_20, CX, 30, COL_TITLE, str);
    }
    // The pad he is launched from.
    const lv_area_t pa = kit_disc_area(CX + s.lv->sx, CY + s.lv->sy, BALL_R + 6);
    if (kit_overlap(clip, &pa)) {
        kit_disc(layer, CX + s.lv->sx, CY + s.lv->sy, BALL_R + 6, COL_PAD);
        kit_disc(layer, CX + s.lv->sx, CY + s.lv->sy, BALL_R + 3, lv_color_black());
    }
    const lv_area_t ha = kit_disc_area(CX + s.lv->hx, CY + s.lv->hy, GOAL_R + 12);
    if (kit_overlap(clip, &ha)) {
        draw_home(layer);
    }
    for (int i = 0; i < MAX_BODIES; i++) {
        if (body_used(i) && kit_overlap(clip, &s.body_drawn[i])) {
            draw_body(layer, i);
        }
    }
    for (int i = 0; i < s.dots; i++) {
        if (kit_overlap(clip, &s.dots_drawn[i])) {
            kit_disc(layer, CX + s.dx[i], CY + s.dy[i], DOT_R * (1 - 0.07f * (float) i), kit_dim(COL_DOT, 1 - 0.09f * (float) i));
        }
    }
    if (s.st != ST_INTRO && s.size > 0.05f) {
        const float r = s.size * BALL_R;
        kit_yoru(layer, CX + s.x, CY + s.y, r, r, &s.face);
    }
    if (s.note[0] != '\0' && kit_overlap(clip, &s.note_drawn)) {
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

static void clear_dots(void)
{
    for (int i = 0; i < s.dots; i++) {
        kit_dirty(s.obj, s.dots_drawn[i]);
    }
    s.dots = 0;
}

static void to_pad(void)
{
    s.x = s.lv->sx;
    s.y = s.lv->sy;
    s.vx = s.vy = 0;
    s.px = s.py = 0;
}

static void load_level(int level, uint32_t now)
{
    s.level = level;
    s.lv = &LEVELS[level];
    s.shots = 0;
    s.dots = 0;
    s.grabbed = false;
    to_pad();
    s.size = 0;
    s.ball_drawn = (lv_area_t) {0, 0, -1, -1};
    place_bodies(now);
    for (int i = 0; i < MAX_BODIES; i++) {
        s.body_drawn[i] = body_used(i) ? body_area(i) : (lv_area_t) {0, 0, -1, -1};
    }
    s.note[0] = '\0';
    lv_obj_invalidate(s.obj);
    char big[24];
    snprintf(big, sizeof(big), "%s %d", card_tr("Level", "Livello"), level + 1);
    set_note(big, level == 0 ? card_tr("Pull Yoru back, let go", "Tira indietro Yoru e lascia") : NULL);
    set_state(ST_INTRO, now);
}

/* ---- Flying ------------------------------------------------------------------- */

/* The pull of everything at (x, y). */
static void gravity(float x, float y, float *ax, float *ay)
{
    *ax = *ay = 0;
    for (int i = 0; i < MAX_BODIES; i++) {
        const body_t *b = &s.lv->bodies[i];
        if (!body_used(i)) {
            continue;
        }
        const float gm = b->kind == B_HOLE ? G_HOLE : G_PLANET * (float) b->r * (float) b->r;
        const float dx = s.bx[i] - x, dy = s.by[i] - y;
        const float d2 = dx * dx + dy * dy + 400, d = sqrtf(d2);
        *ax += gm * dx / (d2 * d);
        *ay += gm * dy / (d2 * d);
    }
}

/* What he ran into: -1, a body's index, or MAX_BODIES for home. */
static int touching(float x, float y)
{
    if (hypotf(x - s.lv->hx, y - s.lv->hy) < GOAL_R - 4) {
        return MAX_BODIES;
    }
    for (int i = 0; i < MAX_BODIES; i++) {
        if (!body_used(i)) {
            continue;
        }
        const float reach = s.lv->bodies[i].kind == B_HOLE ? s.lv->bodies[i].r + 2 : s.lv->bodies[i].r + BALL_R - 2;
        if (hypotf(x - s.bx[i], y - s.by[i]) < reach) {
            return i;
        }
    }
    return -1;
}

static void fly(float dt, uint32_t now)
{
    const float h = dt / SUBSTEPS;
    for (int k = 0; k < SUBSTEPS; k++) {
        float ax, ay;
        gravity(s.x, s.y, &ax, &ay);
        s.vx += ax * h;
        s.vy += ay * h;
        s.x += s.vx * h;
        s.y += s.vy * h;
        const int hit = touching(s.x, s.y);
        if (hit == MAX_BODIES) {
            s.hole_x = s.lv->hx;
            s.hole_y = s.lv->hy;
            set_state(ST_SINK, now);
            playback_sound(SOUND_HAPPY);
            return;
        }
        if (hit >= 0 && s.lv->bodies[hit].kind == B_HOLE) {
            s.hole_x = s.bx[hit];
            s.hole_y = s.by[hit];
            set_state(ST_SWALLOW, now);
            playback_sound(SOUND_EEP);
            return;
        }
        if (hit >= 0) {
            // Bumped off: he bounces a little, dazed, and is fetched back.
            const float nx = s.x - s.bx[hit], ny = s.y - s.by[hit], d = fmaxf(1, hypotf(nx, ny));
            const float vn = (s.vx * nx + s.vy * ny) / d;
            s.vx = (s.vx - 1.6f * vn * nx / d) * 0.35f;
            s.vy = (s.vy - 1.6f * vn * ny / d) * 0.35f;
            set_state(ST_CRASH, now);
            playback_sound(SOUND_BOING);
            return;
        }
    }
    // A near pass: he gasps.
    for (int i = 0; i < MAX_BODIES; i++) {
        if (body_used(i) && s.lv->bodies[i].kind == B_HOLE && hypotf(s.x - s.bx[i], s.y - s.by[i]) < 60 &&
            now - s.gasp_ms > 1500) {
            s.gasp_ms = now;
            playback_sound(SOUND_GASP);
        }
    }
    if (hypotf(s.x, s.y) > LOST_R || now - s.fly_ms > FLIGHT_MS) {
        set_state(ST_LOST, now);
        playback_sound(SOUND_WHIMPER);
    }
}

/* Where the shot would go: dots along the first part of the path. */
static void aim_dots(void)
{
    float x = s.lv->sx, y = s.lv->sy;
    float vx = -s.px / PULL_MAX * LAUNCH_V, vy = -s.py / PULL_MAX * LAUNCH_V;
    const float h = 0.012f;
    int n = 0;
    for (int step = 1; step <= DOTS * 4 && n < DOTS; step++) {
        float ax, ay;
        gravity(x, y, &ax, &ay);
        vx += ax * h;
        vy += ay * h;
        x += vx * h;
        y += vy * h;
        if (touching(x, y) >= 0) {
            break;
        }
        if (step % 4 == 0) {
            s.dx[n] = x;
            s.dy[n] = y;
            n++;
        }
    }
    for (int i = 0; i < LV_MAX(n, s.dots); i++) {
        if (i < n) {
            kit_moved(s.obj, &s.dots_drawn[i], kit_disc_area(CX + s.dx[i], CY + s.dy[i], DOT_R + 1));
        } else {
            kit_dirty(s.obj, s.dots_drawn[i]);
        }
    }
    s.dots = n;
}

/* ---- His face -------------------------------------------------------------------- */

static void face(uint32_t now)
{
    float tgx = 0, tgy = -0.15f;
    kit_eyes_t eyes = KIT_EYES_OPEN;
    const float pull = hypotf(s.px, s.py);
    switch (s.st) {
    case ST_AIM:
        if (pull > PULL_MAX * 0.7f) {
            eyes = KIT_EYES_SHUT; // bracing
        } else if (pull > 8) {
            // He looks where he will go.
            tgx = -s.px / pull * 0.8f;
            tgy = -s.py / pull * 0.8f;
        } else {
            const float dx = s.lv->hx - s.x, dy = s.lv->hy - s.y, d = fmaxf(1, hypotf(dx, dy));
            const bool home = (now / 1500) % 3 == 0;
            tgx = home ? dx / d * 0.6f : 0;
            tgy = home ? dy / d * 0.6f : -0.15f;
        }
        break;
    case ST_FLY: {
        const float v = fmaxf(1, hypotf(s.vx, s.vy));
        tgx = s.vx / v * 0.8f;
        tgy = s.vy / v * 0.8f;
        eyes = v > 380 ? KIT_EYES_WIDE : KIT_EYES_OPEN;
        break;
    }
    case ST_CRASH:
        eyes = KIT_EYES_SHUT;
        break;
    case ST_LOST:
    case ST_SWALLOW:
        eyes = KIT_EYES_WIDE;
        tgy = -0.6f;
        break;
    case ST_SINK:
    case ST_CHEER:
        eyes = KIT_EYES_HAPPY;
        tgy = 0;
        break;
    default:
        break;
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
    h = h * 31 + (uint32_t) s.face.eyes * 7 + (uint32_t) lroundf(s.face.lid * 8);
    return h * 31 + (uint32_t) lroundf(s.size * 40);
}

/* ---- The frame ----------------------------------------------------------------- */

static float ease_out(float k)
{
    return 1 - (1 - k) * (1 - k);
}

static void back_to_pad(uint32_t now)
{
    to_pad();
    s.size = 0;
    set_state(ST_AIM, now);
    playback_sound(SOUND_BOOP);
}

static void tick_cb(lv_timer_t *t)
{
    const uint32_t now = lv_tick_get();
    const float dt = fminf(0.04f, (float) (now - s.last_ms) / 1000.0f);
    s.last_ms = now;
    const uint32_t in = now - s.st_ms;
    place_bodies(now);
    switch (s.st) {
    case ST_INTRO:
        if (in >= INTRO_MS) {
            set_note(NULL, NULL);
            back_to_pad(now);
        }
        break;
    case ST_AIM:
        // He pops onto the pad, and follows the pull.
        s.size = fminf(1, s.size + dt / 0.25f);
        s.x = s.lv->sx + 0.3f * s.px;
        s.y = s.lv->sy + 0.3f * s.py;
        break;
    case ST_FLY:
        fly(dt, now);
        break;
    case ST_CRASH:
        s.x += s.vx * dt;
        s.y += s.vy * dt;
        if (in >= 700) {
            back_to_pad(now);
        }
        break;
    case ST_LOST:
        // Off into the dark, smaller and smaller.
        s.x += s.vx * dt;
        s.y += s.vy * dt;
        s.size = 1 - fminf(1, (float) in / 700);
        if (in >= 900) {
            back_to_pad(now);
        }
        break;
    case ST_SWALLOW:
    case ST_SINK: {
        const float k = fminf(1, (float) in / SINK_MS);
        s.x += (s.hole_x - s.x) * fminf(1, dt * 14);
        s.y += (s.hole_y - s.y) * fminf(1, dt * 14);
        s.size = 1 - ease_out(k);
        if (k < 1) {
            break;
        }
        if (s.st == ST_SWALLOW) {
            back_to_pad(now);
            playback_sound(SOUND_WHIMPER);
            break;
        }
        char small[40];
        snprintf(small, sizeof(small), s.shots == 1 ? card_tr("In one shot!", "Al primo colpo!")
                                                    : card_tr("In %d shots", "In %d tiri"),
                 s.shots);
        const bool last = s.level + 1 == LEVEL_COUNT;
        set_note(last ? card_tr("All done!", "Finito!") : card_tr("Home!", "A casa!"), small);
        set_state(ST_CHEER, now);
        playback_sound(last || s.shots == 1 ? SOUND_EXCITED : SOUND_TADA);
        kit_save(SAVE_KEY, (s.level + 1) % LEVEL_COUNT);
        break;
    }
    case ST_CHEER:
        if (in >= CHEER_MS + (s.level + 1 == LEVEL_COUNT ? 1200 : 0)) {
            load_level((s.level + 1) % LEVEL_COUNT, now);
        }
        break;
    }
    face(now);
    for (int i = 0; i < MAX_BODIES; i++) {
        if (body_used(i) && s.lv->bodies[i].kind == B_MOON) {
            kit_moved(s.obj, &s.body_drawn[i], body_area(i));
        }
    }
    const uint8_t glow = (uint8_t) (127.5f + 127.5f * sinf((float) now / 450.0f));
    if (abs((int) glow - (int) s.glow) > 24) {
        s.glow = glow;
        kit_dirty(s.obj, kit_disc_area(CX + s.lv->hx, CY + s.lv->hy, GOAL_R + 12));
    }
    const uint32_t sig = ball_sig();
    if (sig != s.sig) {
        s.sig = sig;
        kit_moved(s.obj, &s.ball_drawn, ball_area());
    }
    if (s.st != ST_AIM || s.grabbed) {
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

static void press_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    const float fx = (float) p.x - CX, fy = (float) p.y - CY;
    const uint32_t now = lv_tick_get();
    if (code == LV_EVENT_PRESSED) {
        if (p.y < 70) {
            lv_async_call(quit_async, NULL); // the title: back to the games
            return;
        }
        s.grabbed = s.st == ST_AIM && s.size > 0.9f && hypotf(fx - s.lv->sx, fy - s.lv->sy) < GRAB_R;
        return;
    }
    if (!s.grabbed || s.st != ST_AIM) {
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        float px = fx - s.lv->sx, py = fy - s.lv->sy;
        const float d = hypotf(px, py);
        if (d > PULL_MAX) {
            px *= PULL_MAX / d;
            py *= PULL_MAX / d;
        }
        s.px = px;
        s.py = py;
        if (d > 12) {
            aim_dots();
        } else {
            clear_dots();
        }
        return;
    }
    // Let go.
    s.grabbed = false;
    clear_dots();
    const float pull = hypotf(s.px, s.py);
    if (pull < 18) {
        s.px = s.py = 0; // just a touch
        return;
    }
    s.vx = -s.px / PULL_MAX * LAUNCH_V;
    s.vy = -s.py / PULL_MAX * LAUNCH_V;
    s.x = s.lv->sx;
    s.y = s.lv->sy;
    s.px = s.py = 0;
    s.shots++;
    s.fly_ms = now;
    set_state(ST_FLY, now);
    playback_sound(SOUND_FLUTTER);
}

static void deleted_cb(lv_event_t *e)
{
    if (s.timer != NULL) {
        lv_timer_delete(s.timer);
    }
    memset(&s, 0, sizeof(s));
}

lv_obj_t *game_sling_start(lv_obj_t *parent, void (*quit)(void))
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
    load_level(level >= 0 && level < LEVEL_COUNT ? level : 0, now);
    s.timer = lv_timer_create(tick_cb, FRAME_MS, NULL);
    return s.obj;
}

void game_sling_icon(lv_layer_t *layer, float x, float y, float r)
{
    // A planet, and Yoru flying past it on a dotted path.
    kit_disc(layer, x + 0.22f * r, y + 0.2f * r, 0.32f * r, lv_color_hex(0xB8432A));
    kit_disc(layer, x + 0.14f * r, y + 0.12f * r, 0.2f * r, lv_color_hex(0xFF9F6B));
    for (int i = 0; i < 3; i++) {
        const float a = (float) M_PI * (0.95f + 0.22f * (float) i);
        kit_disc(layer, x + 0.22f * r + 0.6f * r * cosf(a), y + 0.2f * r + 0.6f * r * sinf(a), 0.05f * r,
                 lv_color_hex(0xC8CCDA));
    }
    const kit_face_t f = {.gx = 0.7f, .gy = -0.4f, .lid = 1, .eyes = KIT_EYES_WIDE};
    kit_yoru(layer, x - 0.18f * r, y - 0.38f * r, 0.26f * r, 0.26f * r, &f);
}
