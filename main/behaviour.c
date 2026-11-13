#include "behaviour.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "approval.h"
#include "battery.h"
#include "board_button.h"
#include "board.h"
#include "board_display.h"
#include "board_settings.h"
#include "board_touch.h"
#include "card.h"
#include "clock.h"
#include "esp_log.h"
#include "esp_random.h"
#include "errand.h"
#include "eyes.h"
#include "growth.h"
#include "icons.h"
#include "inbox.h"
#include "inbox_ui.h"
#include "life.h"
#include "lvgl_port.h"
#include "menu.h"
#include "motion.h"
#include "metrics_model.h"
#include "playback.h"
#include "recmark.h"
#include "power.h"
#include "timers.h"
#include "toast.h"
#include "voice.h"

/* Often enough for life.c's moves. */
#define TICK_MS 100
#define BRIGHTNESS_ASLEEP 12
/* Asleep this long, the screen goes dark; only PWR lights it again. The
 * board keeps listening to the host meanwhile. */
#define DARK_AFTER_MS 60000
/* Three pokes closer than this make it grumpy. */
#define POKE_WINDOW_MS 1500
/* How much finger travel counts as stroking. */
#define STROKE_PX 160
/* Holding your finger: the body leans this much of the way to it, and after
 * this long it melts (hearts, a purr). */
#define HOLD_LEAN 0.3f
#define HOLD_LOVE_MS 1500
/* Back to the middle when something needs Yoru's attention. */
#define HOME_MS 350
/* The host's pointer is followed this long after its last move. */
#define GAZE_HOLD_MS 6000
/* A swipe down that starts this close to the top edge opens the menu. */
#define MENU_EDGE_PX 70
#define MENU_SWIPE_PX 80
/* A slap: a quick flick across the face. Three in a row and it's had enough. */
#define SLAP_MAX_MS 280
#define SLAP_PX 110
#define SLAP_WINDOW_MS 2500
/* A stroke is slower than a slap: the finger has been down this long. */
#define STROKE_MIN_MS 350
/* A sound from one side is looked at this long. */
#define HEARD_LOOK_MS 1500
/* The glow while it listens: the wake word's cyan. */
#define LISTEN_RGB 0x40C8E0
/* A noise this far to a side gets pointed at, at most this often. */
#define HEARD_POINT_X 0.45f
#define HEARD_POINT_GAP_MS 45000
/* Rocked twice within this long, it falls asleep. */
#define LULLABY_MS 60000
#define LEAN_MS 50
/* How long "done" (green) and "error" (red) stay on the sphere. */
#define STATUS_DONE_MS 2500
#define STATUS_ERROR_MS 3500
/* Waiting on an answer, the eyes go between the request and the buttons. */
#define GLANCE_MIN_MS 1800
#define GLANCE_SPAN_MS 1600

static const char *TAG = "behaviour";

/* How each scene looks: a clip from the Rive file acting it out (CLIP_NONE:
 * none), mirrored half the time if it has a side, its effects' colour (0:
 * gold), then a mood for a while, and Yoru's sound for it (SOUND_COUNT for
 * none; playback.c drops them while its sounds are off). The mood holds
 * that long after the clip, which eases into it. Names as in DualEye, so the
 * host's scenes mean the same on both boards; the hand scenes are Yoru's
 * own. */
typedef struct {
    const char *name;
    clip_id_t clip;
    bool either_side;
    uint32_t rgb;
    eyes_mood_t mood;
    uint16_t ms;
    sound_t sound;
} scene_t;

/* The effects' colours. */
#define RGB_GREEN 0x3DDC84
#define RGB_PINK 0xFF6FA8
#define RGB_ORANGE 0xFF8A3D
#define RGB_RED 0xFF4D5E

#define C(name) CLIP_##name
static const scene_t SCENES[] = {
    {"happy", C(HAPPY_HOP), true, 0, EYES_MOOD_HAPPY, 2000, SOUND_HAPPY},
    {"excited", C(EXCITED_CLAP), false, 0, EYES_MOOD_HAPPY, 1500, SOUND_EXCITED},
    {"wink", C(WINK), true, 0, EYES_MOOD_HAPPY, 600, SOUND_WINK},
    {"proud", C(CELEBRATE), false, 0, EYES_MOOD_HAPPY, 1000, SOUND_TADA},
    {"relieved", C(RELIEVED), false, 0, EYES_MOOD_HAPPY, 800, SOUND_PHEW},
    {"charged", C(CHARGED), false, RGB_GREEN, EYES_MOOD_HAPPY, 800, SOUND_CHARGE},
    // After the bolt of a cable plugged in (its sound came with the bolt).
    {"plugged", C(EXCITED_CLAP), false, RGB_GREEN, EYES_MOOD_HAPPY, 1000, SOUND_COUNT},
    {"dance", C(DANCE), true, 0, EYES_MOOD_HAPPY, 800, SOUND_BEAT},
    {"love", C(LOVE), false, RGB_PINK, EYES_MOOD_LOVING, 1500, SOUND_AWW},
    {"curious", C(CURIOUS_TILT), true, 0, EYES_MOOD_CURIOUS, 1000, SOUND_HMM},
    {"thinking", C(THINK), false, 0, EYES_MOOD_THINKING, 1000, SOUND_COUNT},
    {"focus", C(FOCUS), false, 0, EYES_MOOD_THINKING, 1200, SOUND_SCAN},
    {"angry", C(ANGRY_HUFF), true, RGB_RED, EYES_MOOD_GRUMPY, 1000, SOUND_GRUMBLE},
    {"hot", C(HOT_FAN), true, RGB_ORANGE, EYES_MOOD_HOT, 1500, SOUND_PANT},
    {"sleepy", C(SLEEPY_SWAY), true, 0, EYES_MOOD_SLEEPY, 1200, SOUND_COUNT},
    {"yawn", C(YAWN), false, 0, EYES_MOOD_SLEEPY, 800, SOUND_YAWN},
    {"tired", C(SLEEPY_SWAY), true, 0, EYES_MOOD_SLEEPY, 1200, SOUND_DRAIN},
    {"sigh", C(SIGH), true, 0, EYES_MOOD_SLEEPY, 600, SOUND_SIGH},
    {"sad", C(SAD_DROOP), false, 0, EYES_MOOD_SAD, 1500, SOUND_WHIMPER},
    {"surprised", C(STARTLE), true, 0, EYES_MOOD_SURPRISED, 600, SOUND_GASP},
    {"scared", C(SCARED_COVER), false, 0, EYES_MOOD_SCARED, 1000, SOUND_EEP},
    {"bored", C(BORED), true, 0, EYES_MOOD_BORED, 1000, SOUND_COUNT},
    {"look_around", C(LOOK_AROUND), true, 0, EYES_MOOD_NEUTRAL, 0, SOUND_COUNT},
    {"blink", C(NONE), false, 0, EYES_MOOD_NEUTRAL, 0, SOUND_COUNT},
    // The hands on their own.
    {"wave", C(WAVE), true, 0, EYES_MOOD_HAPPY, 600, SOUND_HELLO},
    {"point", C(POINT_SOUND), true, 0, EYES_MOOD_CURIOUS, 600, SOUND_HMM},
    {"shy", C(SHY_PEEK), false, 0, EYES_MOOD_HAPPY, 800, SOUND_COUNT},
    {"clap", C(EXCITED_CLAP), false, 0, EYES_MOOD_HAPPY, 600, SOUND_COUNT},
    {"stretch", C(WAKE_STRETCH), false, 0, EYES_MOOD_NEUTRAL, 600, SOUND_YAWN},
    // The case as an input (motion.c) and slaps.
    {"dizzy", C(DIZZY), true, 0, EYES_MOOD_DIZZY, 1200, SOUND_CONFUSED},
    {"annoyed", C(SHOO), true, 0, EYES_MOOD_GRUMPY, 1200, SOUND_GRUMBLE},
    {"falling", C(FALLING), false, 0, EYES_MOOD_SCARED, 400, SOUND_EEP},
    {"drowsy", C(SLEEPY_SWAY), true, 0, EYES_MOOD_SLEEPY, 1500, SOUND_YAWN},
    {"lifted", C(LIFTED), true, 0, EYES_MOOD_CURIOUS, 600, SOUND_CHIRP},
    {"giggle", C(GIGGLE), false, 0, EYES_MOOD_DELIGHTED, 600, SOUND_GIGGLE},
};

static struct {
    uint32_t last_touch_ms;
    /* A short reaction over the resting mood, until `until_ms`. */
    uint32_t until_ms;
    bool asleep;
    int pokes;
    uint32_t last_poke_ms;
    /* When it last woke: a nap's loneliness counts from there. */
    uint32_t woke_ms;
    lv_point_t last_point;
    int32_t stroke;
    bool stroked;
    int brightness;
    voice_state_t voice;
    /* The finger is on the screen; the host's pointer moved at gaze_ms. */
    bool pressed;
    uint32_t gaze_ms;
    /* "done" or "error" on the sphere until then (0: none). */
    uint32_t status_until_ms;
    /* The press: where and when it started, and whether from the top edge. */
    lv_point_t press_point;
    uint32_t press_ms;
    bool from_top;
    int slaps;
    uint32_t last_slap_ms;
    /* A scene to play a moment later (relief after a fall). */
    const char *later;
    uint32_t later_ms;
    bool menu;
    /* A confirmation is up (approval.c); the glances between its words and
     * its buttons. */
    bool approval;
    uint32_t next_glance_ms;
    bool glance_up;
    /* Turned face down: asleep, and the mic is off until it's turned back. */
    bool face_down;
    uint32_t rocked_ms;
    lv_obj_t *face;
    /* The inbox covers the screen (inbox_ui.c). */
    bool view;
    /* Asleep since then; the screen is dark (only PWR lights it). */
    uint32_t asleep_ms;
    bool dark;
    /* PWR is held and its words stream. */
    bool talking;
    /* It came to the glass for a notification. */
    bool inbox_call;
    /* It holds your finger (a long press on it): since when, whether it has
     * melted yet, and where it was before leaning to it. */
    bool holding;
    uint32_t hold_ms;
    bool hold_loved;
    eyes_pos_t hold_home;
} s;

static void show_voice(voice_state_t state);
static void apply_rotation(void);
static void meeting_tick(uint32_t now);
static bool card_up(void);
static void wait_for_answer(uint32_t now);
static void hold_gaze(float x, float y, uint32_t ms);
static void later(const char *scene, uint32_t after_ms);
static void let_go(bool goodbye);

static void set_brightness(int percent)
{
    if (s.dark) {
        percent = 0;
    }
    if (percent != s.brightness) {
        s.brightness = percent;
        board_display_set_brightness(percent);
    }
}

/* The time of day, from the board's clock (clock.c: SNTP or the host's), and
 * the host's idle time from its snapshots; minute -1 while nobody said it. */
static void host_now(int *minute, uint32_t *idle_ms)
{
    static metrics_snapshot_t snap; // big for the LVGL task's stack
    metrics_model_get(&snap);
    *minute = clock_valid() ? clock_minute() : snap.host.valid ? snap.host.minute : -1;
    // A host that went left its last idle time behind: nobody is at it now.
    *idle_ms = snap.host.valid && metrics_model_live() ? (uint32_t) snap.host.idle_s * 1000 : UINT32_MAX;
}

/* At night the screen is dimmer. */
static bool night(void)
{
    int minute;
    uint32_t idle_ms;
    host_now(&minute, &idle_ms);
    if (minute < 0) {
        return false;
    }
    const int h = minute / 60;
    const int from = (int) board_settings_get(SETTING_NIGHT_FROM), to = (int) board_settings_get(SETTING_NIGHT_TO);
    return from <= to ? (h >= from && h < to) : (h >= from || h < to);
}

static int awake_brightness(void)
{
    const int day = (int) board_settings_get(SETTING_BRIGHTNESS);
    const int dark = (int) board_settings_get(SETTING_NIGHT_BRIGHTNESS);
    const int want = night() && dark < day ? dark : day;
    const int cap = power_brightness_cap(); // on battery
    return want < cap ? want : cap;
}

static int asleep_brightness(void)
{
    int bright = awake_brightness();
    return board_settings_on(SETTING_AUTO_DIM) && bright > BRIGHTNESS_ASLEEP ? BRIGHTNESS_ASLEEP : bright;
}

static void react(eyes_mood_t mood, uint32_t for_ms)
{
    s.until_ms = lv_tick_get() + for_ms;
    eyes_set_mood(mood);
}

/* The clips' gold, when a scene names no colour. */
#define SCENE_RGB 0xFFD45C

/* Show one of the scenes; false if it isn't one these eyes know. */
static bool play(const char *name)
{
    if (name == NULL) {
        return false;
    }
    for (size_t i = 0; i < sizeof(SCENES) / sizeof(SCENES[0]); i++) {
        const scene_t *sc = &SCENES[i];
        if (strcmp(name, sc->name) != 0) {
            continue;
        }
        if (sc->sound != SOUND_COUNT) {
            playback_sound(sc->sound);
        }
        uint32_t clip_ms = 0;
        const clip_t *c = clips_get(sc->clip);
        if (c != NULL) {
            eyes_play_fx(sc->clip, sc->either_side && (esp_random() & 1), sc->rgb ? sc->rgb : SCENE_RGB, NULL);
            clip_ms = (uint32_t) c->n * 1000 / 30;
        } else {
            eyes_blink();
        }
        if (sc->ms > 0) {
            react(sc->mood, clip_ms + sc->ms);
        }
        return true;
    }
    ESP_LOGW(TAG, "no look for scene %s", name);
    return false;
}

static void fall_asleep(void)
{
    let_go(false);
    if (!s.asleep) {
        ESP_LOGI(TAG, "asleep");
        s.asleep_ms = lv_tick_get();
        eyes_play(CLIP_FALL_ASLEEP, false); // nods off, into the asleep face
    }
    s.asleep = true;
    s.until_ms = 0;
    eyes_set_mood(EYES_MOOD_ASLEEP);
    set_brightness(asleep_brightness());
    life_slept();
}

/* Woken, it says hello. */
static void wake_up(void)
{
    ESP_LOGI(TAG, "awake");
    s.asleep = false;
    s.woke_ms = lv_tick_get();
    set_brightness(awake_brightness());
    eyes_play(CLIP_WAKE_UP, esp_random() & 1); // eyes open, hello again
    react(EYES_MOOD_CURIOUS, 3000);
    life_woke();
}

/* Something needs Yoru here: back to the middle, whatever it was doing,
 * out of an errand's portal too. */
static void come_home(void)
{
    errand_abort();
    life_attend();
    eyes_move_to(0, 0, 1, HOME_MS, EYES_PATH_GLIDE);
}

/* A status on the sphere for a while, then back to none. */
static void show_status(eyes_status_t status, uint32_t for_ms)
{
    eyes_set_status(status);
    s.status_until_ms = for_ms ? lv_tick_get() + for_ms : 0;
    if (s.status_until_ms == 0 && for_ms) {
        s.status_until_ms = 1; // 0 means none
    }
}

/* The hands on the finger, the body leaning a little of the way to it. */
static void hold_follow(lv_point_t p)
{
    eyes_clutch(p.x, p.y);
    const eyes_pos_t f = eyes_unproject(p.x, p.y, s.hold_home.z), h = s.hold_home;
    eyes_follow(h.x + (f.x - h.x) * HOLD_LEAN, h.y + (f.y - h.y) * HOLD_LEAN, h.z, 10);
}

/* A long press on Yoru: it takes your finger in both hands, happy. */
static void hold_finger(uint32_t now, lv_point_t p)
{
    s.holding = true;
    s.hold_ms = now;
    s.hold_loved = false;
    s.hold_home = eyes_where();
    life_attend();
    life_feel(LIFE_FEEL_HOLD);
    playback_sound(SOUND_CHIRP);
    react(EYES_MOOD_HAPPY, 1000); // the tick keeps it going
    hold_follow(p);
}

/* Held a while, it melts: hearts and a purr. */
static void hold_step(uint32_t now)
{
    if (!s.hold_loved && now - s.hold_ms >= HOLD_LOVE_MS) {
        s.hold_loved = true;
        playback_sound(SOUND_PURR);
        eyes_set_mood(EYES_MOOD_LOVING);
    }
    s.until_ms = now + 1000;
}

/* The finger lifted, or something else needs Yoru: the hands let go and it
 * floats back. With `goodbye` it waves after the finger. */
static void let_go(bool goodbye)
{
    if (!s.holding) {
        return;
    }
    s.holding = false;
    eyes_let_go();
    eyes_move_to(s.hold_home.x, s.hold_home.y, s.hold_home.z, 600, EYES_PATH_GLIDE);
    if (goodbye) {
        eyes_play(CLIP_WAVE, s.hold_home.x > 0); // waved off towards the middle
        playback_sound(SOUND_HMM);
        react(EYES_MOOD_HAPPY, 2400);
    } else {
        s.until_ms = 0;
    }
}

/* Where the finger is, as a gaze target. */
static void watch_finger(lv_indev_t *indev)
{
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    eyes_look_at((float) (p.x - BOARD_LCD_H_RES / 2) / 180.0f, (float) (p.y - BOARD_LCD_V_RES / 2) / 180.0f);
    s.stroke += abs(p.x - s.last_point.x) + abs(p.y - s.last_point.y);
    s.last_point = p;
    if (s.holding) {
        hold_follow(p);
        return; // dragged along, not stroked
    }
    if (!s.stroked && !s.asleep && s.voice == VOICE_IDLE && s.stroke > STROKE_PX && !s.from_top
        && lv_tick_elaps(s.press_ms) >= STROKE_MIN_MS && board_settings_on(SETTING_TOUCH_REACTIONS)) {
        s.stroked = true;
        life_attend();
        life_feel(LIFE_FEEL_STROKE);
        playback_sound(SOUND_PURR);
        eyes_play(CLIP_MELT, false); // melts into the hand
        react(EYES_MOOD_HAPPY, 3000);
    }
}

/* A tap (or a knock on the case, `clip`): it reacts, and the third in a row
 * annoys it. */
static void poke(uint32_t now, clip_id_t clip)
{
    if (life_calling() && s.inbox_call && inbox_unread(NULL) > 0) {
        // It knocked for a notification: here it is.
        s.inbox_call = false;
        life_attend();
        inbox_ui_open(NULL);
        return;
    }
    s.pokes = (now - s.last_poke_ms < POKE_WINDOW_MS) ? s.pokes + 1 : 1;
    s.last_poke_ms = now;
    life_attend();
    life_feel(LIFE_FEEL_POKE);
    if (s.pokes >= 3) {
        play("annoyed");
    } else {
        eyes_play(clip, esp_random() & 1);
        playback_sound(SOUND_BOOP);
        react(EYES_MOOD_HAPPY, 1800);
    }
}

/* A quick flick across the face knocks the body that way. The third in a
 * row: it pushes you away. */
static void slap(uint32_t now, int32_t dx, int32_t dy)
{
    s.slaps = (now - s.last_slap_ms < SLAP_WINDOW_MS) ? s.slaps + 1 : 1;
    s.last_slap_ms = now;
    life_attend();
    life_feel(LIFE_FEEL_SLAP);
    if (s.slaps >= 3) {
        s.slaps = 0;
        play("annoyed");
    } else {
        // Knocked the way the finger went: the clip is slapped to the right.
        eyes_play(CLIP_BONK, dx < 0);
        playback_sound(SOUND_BOING);
        react(EYES_MOOD_SURPRISED, 900);
    }
}

static void touch_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    const uint32_t now = lv_tick_get();
    s.last_touch_ms = now;

    switch (code) {
    case LV_EVENT_PRESSED:
        s.pressed = true;
        lv_indev_get_point(indev, &s.last_point);
        s.press_point = s.last_point;
        s.press_ms = now;
        s.from_top = s.press_point.y < MENU_EDGE_PX;
        s.stroke = 0;
        s.stroked = false;
        motion_touched();
        if (!s.asleep) {
            watch_finger(indev);
        }
        break;
    case LV_EVENT_PRESSING:
        if (s.from_top && !s.menu && s.last_point.y - s.press_point.y > MENU_SWIPE_PX) {
            // Pulled down from the top edge: the menu, whatever else goes on.
            s.from_top = false;
            s.pressed = false; // no release comes after wait_release
            let_go(false);
            if (s.asleep) {
                wake_up();
            }
            lv_indev_wait_release(indev);
            menu_open();
            break;
        }
        if (!s.asleep) {
            watch_finger(indev);
        }
        break;
    case LV_EVENT_RELEASED: {
        s.pressed = false;
        let_go(true); // if it held the finger
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        const int32_t dx = p.x - s.press_point.x, dy = p.y - s.press_point.y;
        if (!s.asleep && !s.from_top && s.voice == VOICE_IDLE && lv_tick_elaps(s.press_ms) <= SLAP_MAX_MS
            && abs(dx) > SLAP_PX && board_settings_on(SETTING_TOUCH_REACTIONS)) {
            s.stroked = true; // not a poke too
            slap(now, dx, dy);
        }
        if (s.voice == VOICE_IDLE) {
            eyes_look_around();
        } else {
            show_voice(s.voice); // back to looking at you
        }
        break;
    }
    case LV_EVENT_SHORT_CLICKED:
        // Waking and sleeping work with reactions off too: they're the controls.
        if (s.asleep) {
            wake_up();
        } else if (!s.stroked && s.voice == VOICE_IDLE && board_settings_on(SETTING_TOUCH_REACTIONS)) {
            // On Yoru it's a poke; the black is just the black.
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            if (eyes_hit(p.x, p.y)) {
                poke(now, CLIP_TAPPED);
            }
        }
        break;
    case LV_EVENT_LONG_PRESSED:
        // Held on Yoru (not a stroke, not mid-conversation): it holds the finger.
        if (!s.asleep && !s.from_top && s.voice == VOICE_IDLE && !s.stroked && s.stroke <= STROKE_PX
            && board_settings_on(SETTING_TOUCH_REACTIONS) && eyes_hit(s.last_point.x, s.last_point.y)) {
            hold_finger(now, s.last_point);
        }
        break;
    default:
        break;
    }
}

/* What life.c needs to know this tick. Someone is around if they touched,
 * talked or moved the case lately, or use the computer (the host's idle
 * time). */
static life_ctx_t life_context(uint32_t now, bool playing)
{
    int minute;
    uint32_t host_idle_ms;
    host_now(&minute, &host_idle_ms);
    uint32_t alone_ms = now - s.last_touch_ms;
    alone_ms = host_idle_ms < alone_ms ? host_idle_ms : alone_ms;
    const uint32_t awake_ms = now - s.woke_ms;
    const uint32_t lonely_ms = alone_ms < awake_ms ? alone_ms : awake_ms;
    return (life_ctx_t) {
        .free = !s.pressed && s.voice == VOICE_IDLE && !s.menu && !s.view && !card_up() && !s.face_down && !playing
                && !errand_busy(),
        .asleep = s.asleep,
        .focus = board_settings_on(SETTING_FOCUS),
        // Low on battery it's tired whoever is around: sleepy eyes, slower.
        .drowsy = lonely_ms >= (uint32_t) board_settings_get(SETTING_SLEEPY_AFTER_S) * 1000 || battery_low(),
        .lonely = lonely_ms >= (uint32_t) board_settings_get(SETTING_ASLEEP_AFTER_S) * 1000,
        .alone_ms = alone_ms,
        .minute = minute,
        .day = clock_valid() ? clock_day() : -1,
    };
}

static bool card_up(void)
{
    return s.approval;
}

/* Long asleep: the screen off and deaf to touch, the board still at work. */
static void go_dark(void)
{
    ESP_LOGI(TAG, "screen dark");
    s.dark = true;
    set_brightness(0);
    lvgl_port_set_touch(false);
    board_touch_set_sleep(true);
    eyes_set_pace(EYES_PACE_STOPPED); // power.c keeps it so
    board_display_set_sleep(true);
}

static void light_up(void)
{
    ESP_LOGI(TAG, "screen lit");
    s.dark = false;
    board_display_set_sleep(false);
    eyes_set_pace(EYES_PACE_SMOOTH); // power.c slows it again if it should
    board_touch_set_sleep(false);
    lvgl_port_set_touch(true);
    s.last_touch_ms = lv_tick_get();
}

/* A notification came (inbox.c), as loud as inbox_add() said: normal
 * chirps (inbox_ui.c shows its banner), high also wakes a dimmed screen and
 * knocks on the glass once, urgent also lights a dark screen and sounds the
 * alarm. While the screen is dark only urgent shows; face down, nothing. */
static void inbox_tick(uint32_t now)
{
    const inbox_urgency_t u = inbox_take_alert();
    if (u < INBOX_NORMAL || s.face_down || (s.dark && u < INBOX_URGENT)) {
        return;
    }
    if (s.dark) {
        light_up();
    }
    if (u >= INBOX_HIGH && s.asleep) {
        s.last_touch_ms = now;
        wake_up();
    }
    if (u == INBOX_URGENT) {
        // Twice: longer and harder to miss than a chirp. An earcon, so it
        // plays with Yoru's sounds off too.
        playback_sound(SOUND_ALARM);
        playback_sound(SOUND_ALARM);
    } else {
        playback_sound(SOUND_CHIRP);
    }
    const bool busy = card_up() || s.menu || s.view || s.voice != VOICE_IDLE;
    if (u >= INBOX_HIGH && !busy && !s.asleep && life_call()) {
        s.inbox_call = true;
    }
}

/* A timer or a reminder rang (timers.c): Yoru jumps like an alarm clock,
 * flapping its hands, with the alarm and a line saying which one. A host
 * says it out loud too, if there is one. */
static void timers_tick(void)
{
    timers_poll();
    char label[TIMERS_LABEL_CHARS + 1];
    if (!timers_take_ring(label, sizeof(label)) || s.face_down) {
        return;
    }
    if (s.dark) {
        light_up();
    }
    if (s.asleep) {
        s.last_touch_ms = lv_tick_get();
        wake_up();
    }
    playback_sound(SOUND_ALARM);
    char text[48];
    if (label[0] != '\0') {
        snprintf(text, sizeof(text), "%s " LV_SYMBOL_BELL, label);
    } else {
        snprintf(text, sizeof(text), "%s", card_tr("Time's up! " LV_SYMBOL_BELL, "Tempo scaduto! " LV_SYMBOL_BELL));
    }
    toast_show(text, 6, TOAST_WAIT);
    life_feel(LIFE_FEEL_TIMER);
    if (card_up() || s.menu || s.view || s.voice != VOICE_IDLE) {
        return;
    }
    come_home();
    eyes_play(CLIP_STARTLE, esp_random() & 1); // a jump
    react(EYES_MOOD_SURPRISED, 2600);
}

/* "Charging: 45%", in the language of the labels. */
static void battery_toast(const char *en, const char *it, int seconds, toast_tone_t tone)
{
    char text[48];
    const int pct = battery_percent();
    if (pct >= 0) {
        snprintf(text, sizeof(text), "%s: %d%%", card_tr(en, it), pct);
    } else {
        snprintf(text, sizeof(text), "%s", card_tr(en, it));
    }
    toast_show(text, seconds, tone);
}

/* The cable and the charge (battery.c). Plugged in, a bolt of energy
 * shoots into Yoru and it perks up; pulled out, a little "oh?"; full, a
 * proud clap; running low, tired, and nearly empty it asks to be plugged
 * in, lighting the dark screen for it. Asleep it doesn't wake: plugged in,
 * the dot jolts and the screen lights a moment to show the charge ring. */
static void battery_tick(uint32_t now)
{
    const battery_event_t ev = battery_step();
    if (ev == BATTERY_NOTHING || s.face_down) {
        return;
    }
    const bool free = !s.asleep && !card_up() && !s.menu && !s.view && s.voice == VOICE_IDLE;
    switch (ev) {
    case BATTERY_PLUGGED:
        if (s.dark) {
            light_up();
            s.asleep_ms = now; // dark again a minute from now
        }
        eyes_zap();
        life_feel(LIFE_FEEL_PLUGGED);
        battery_toast("Charging", "In carica", 3, TOAST_INFO);
        if (free) {
            s.last_touch_ms = now;
            life_attend();
            // Wide eyes down at the bolt coming up, the jolt with the
            // sound's "bip!", then a happy clap.
            playback_sound(SOUND_CHARGE);
            react(EYES_MOOD_SURPRISED, 600);
            hold_gaze(0, 0.9f, 500);
            later("plugged", 600);
        }
        break;
    case BATTERY_UNPLUGGED:
        if (s.dark) {
            break;
        }
        battery_toast("On battery", "A batteria", 3, TOAST_INFO);
        if (free) {
            s.last_touch_ms = now;
            life_attend();
            playback_sound(SOUND_UNPLUG);
            eyes_play(CLIP_JOLT, false); // the support gone from under it
            react(EYES_MOOD_CURIOUS, 1800);
            hold_gaze(0, 0.9f, 1200); // down where the cable was
        }
        break;
    case BATTERY_FULL:
        if (s.dark) {
            break;
        }
        toast_show(card_tr("Fully charged", "Carica completa"), 4, TOAST_DONE);
        if (free) {
            s.last_touch_ms = now;
            life_attend();
            play("proud");
            show_status(EYES_STATUS_DONE, STATUS_DONE_MS);
        }
        break;
    case BATTERY_LOW:
        if (s.dark) {
            break;
        }
        battery_toast("Battery low", "Batteria scarica", 5, TOAST_WAIT);
        if (free) {
            life_attend();
            play("tired");
        }
        break;
    case BATTERY_CRITICAL:
        if (s.dark) {
            light_up();
            s.asleep_ms = now;
        }
        battery_toast("Plug me in", "Mettimi in carica", 8, TOAST_ERROR);
        playback_sound(SOUND_BATTERY_LOW);
        if (free) {
            life_attend();
            eyes_blink();
            react(EYES_MOOD_SAD, 3500);
            hold_gaze(0, 0.9f, 2500);
        }
        break;
    default:
        break;
    }
}

/* What power.c needs to know; it may hibernate, and then Yoru wakes up
 * (unless it's still face down). */
static void power_step(void)
{
    const power_hold_t hold = timers_count() > 0                     ? POWER_HOLD_TIMER
                              : errand_busy()                        ? POWER_HOLD_ERRAND
                              : s.voice != VOICE_IDLE || s.talking   ? POWER_HOLD_VOICE
                              : card_up() || s.menu || s.view        ? POWER_HOLD_SCREEN
                                                                     : POWER_HOLD_NONE;
    const power_scene_t scene = {
        .asleep = s.asleep,
        .dark = s.dark,
        .busy = s.voice != VOICE_IDLE || s.talking,
        .hold = hold,
    };
    if (power_tick(&scene) && !s.face_down) {
        light_up();
        if (s.asleep) {
            wake_up();
        }
    }
}

static void tick_cb(lv_timer_t *timer)
{
    const uint32_t now = lv_tick_get();

    power_step();

    if (s.status_until_ms != 0 && (int32_t) (now - s.status_until_ms) >= 0) {
        s.status_until_ms = 0;
        eyes_set_status(EYES_STATUS_IDLE);
    }
    apply_rotation();
    inbox_tick(now);
    timers_tick();
    battery_tick(now);
    if (card_up()) {
        wait_for_answer(now);
        return;
    }
    if (s.menu || s.view) {
        // The menu is company, and Yoru only watches it.
        s.last_touch_ms = now;
        set_brightness(awake_brightness());
        return;
    }
    if (s.later != NULL && (int32_t) (now - s.later_ms) >= 0) {
        const char *later = s.later;
        s.later = NULL;
        if (!s.asleep && s.voice == VOICE_IDLE) {
            play(later);
        }
    }

    if (s.asleep) {
        set_brightness(asleep_brightness()); // follows the menu's changes
        if (!s.dark && s.voice == VOICE_IDLE && now - s.asleep_ms >= DARK_AFTER_MS) {
            go_dark();
        }
        const life_ctx_t ctx = life_context(now, false);
        life_step(&ctx);
        return;
    }
    set_brightness(awake_brightness());
    // A milestone reached (growth.c), told when Yoru is awake and free, so
    // that a count at night doesn't wake it.
    if (!s.pressed && !s.face_down && s.voice == VOICE_IDLE) {
        const char *news = growth_take_news();
        if (news != NULL) {
            play("proud");
            toast_show(news, 5, TOAST_DONE);
        }
    }
    if (s.gaze_ms != 0 && now - s.gaze_ms >= GAZE_HOLD_MS) {
        s.gaze_ms = 0;
        if (!s.pressed && s.voice == VOICE_IDLE) {
            eyes_look_around();
        }
    }
    if (s.voice != VOICE_IDLE) {
        // A conversation counts as company, and owns the eyes.
        s.last_touch_ms = now;
        return;
    }
    if (s.holding) {
        hold_step(now);
        return;
    }
    const bool playing = s.until_ms != 0 && (int32_t) (now - s.until_ms) < 0;
    if (!playing) {
        s.until_ms = 0;
    }
    meeting_tick(now);
    const life_ctx_t ctx = life_context(now, playing);
    if (life_step(&ctx) == LIFE_SLEEP) {
        fall_asleep();
    }
}

/* The rotation setting: four fixed ones, or (4) following how the badge
 * stands. Lying flat, it keeps the last one. */
static void apply_rotation(void)
{
    int rot = (int) board_settings_get(SETTING_ROTATION);
    if (rot >= 4) {
        const int up = motion_upright();
        if (up < 0) {
            return;
        }
        // Turned a quarter clockwise, the panel's left edge is up: LVGL's
        // 90° puts the top of the picture there.
        rot = up;
    }
    lvgl_port_set_rotation(rot);
}

/* The lean, from the panel's axes to the picture's. */
static void lean_cb(lv_timer_t *t)
{
    float x = 0, y = 0;
    if (!s.asleep && !s.menu && !s.view && board_settings_on(SETTING_MOTION_REACTIONS)) {
        motion_lean(&x, &y);
    }
    float lx = x, ly = y;
    switch (lvgl_port_rotation()) {
    case 1:
        lx = -y;
        ly = x;
        break;
    case 2:
        lx = -x;
        ly = -y;
        break;
    case 3:
        lx = y;
        ly = -x;
        break;
    default:
        break;
    }
    eyes_lean(lx, ly);
}

/* Look at a point for `ms`, then around again (the tick lets go). */
static void hold_gaze(float x, float y, uint32_t ms)
{
    s.gaze_ms = lv_tick_get() - (GAZE_HOLD_MS - ms);
    if (s.gaze_ms == 0) {
        s.gaze_ms = 1; // 0 means none
    }
    eyes_look_at(x, y);
}

static void later(const char *scene, uint32_t after_ms)
{
    s.later = scene;
    s.later_ms = lv_tick_get() + after_ms;
}

/* The mic off while it's face down; back as the setting says after. */
static void set_face_down(bool down)
{
    s.face_down = down;
    voice_set_muted(down || board_settings_on(SETTING_MIC_MUTED));
}

static void on_motion(motion_event_t ev)
{
    const uint32_t now = lv_tick_get();
    const bool reactions = board_settings_on(SETTING_MOTION_REACTIONS) && !board_settings_on(SETTING_FOCUS);
    // Face down and back are a control, like a long press: always on, even
    // in a conversation (it's the privacy gesture).
    if (ev == MOTION_FACE_DOWN) {
        if (s.menu) {
            menu_close();
        }
        inbox_ui_close();
        eyes_look_around();
        fall_asleep();
        set_face_down(true);
        return;
    }
    if (ev == MOTION_FACE_UP) {
        if (s.face_down) {
            set_face_down(false);
            s.last_touch_ms = now;
            if (s.asleep) {
                wake_up();
            }
        }
        return;
    }
    if (s.menu || s.view || card_up() || s.voice != VOICE_IDLE || s.face_down) {
        return;
    }
    // Picking it up is the gentlest way to wake it.
    if (ev == MOTION_LIFTED && s.asleep) {
        s.last_touch_ms = now;
        wake_up();
        return;
    }
    if (ev == MOTION_DOUBLE_KNOCK) {
        // Called, like two claps.
        s.last_touch_ms = now;
        if (s.asleep) {
            wake_up();
        }
        if (reactions) {
            life_attend();
            play("curious");
        }
        return;
    }
    if (s.asleep || !reactions) {
        return;
    }
    s.last_touch_ms = now;
    life_attend();
    life_feel(ev == MOTION_SHAKEN || ev == MOTION_SPUN || ev == MOTION_SHAKEN_LONG ? LIFE_FEEL_SHAKE
              : ev == MOTION_DROPPED                                            ? LIFE_FEEL_DROP
              : ev == MOTION_LIFTED                                             ? LIFE_FEEL_LIFT
                                                                                : LIFE_FEEL_POKE);
    switch (ev) {
    case MOTION_LIFTED:
        play("lifted");
        hold_gaze(0, -0.2f, 1800); // at you

        break;
    case MOTION_PUT_DOWN:
        eyes_play(CLIP_SET_DOWN, false);
        eyes_look_around();
        break;
    case MOTION_SHAKEN:
    case MOTION_SPUN:
        play("dizzy");
        break;
    case MOTION_SHAKEN_LONG:
        play("annoyed");
        break;
    case MOTION_KNOCK:
        poke(now, CLIP_JOLT);
        break;
    case MOTION_ROCKED:
        if (s.rocked_ms != 0 && now - s.rocked_ms < LULLABY_MS) {
            s.rocked_ms = 0;
            fall_asleep();
        } else {
            s.rocked_ms = now;
            play("drowsy");
        }
        break;
    case MOTION_DROPPED:
        play("falling");
        break;
    case MOTION_LANDED:
        eyes_play(CLIP_LANDED, false);
        later("relieved", 1300);
        break;
    default:
        break;
    }
}

/* How each voice state looks. */
static void show_voice(voice_state_t state)
{
    switch (state) {
    case VOICE_LISTENING:
        show_status(EYES_STATUS_IDLE, 0);
        eyes_set_mood(EYES_MOOD_LISTENING);
        eyes_look_at(0, -0.15f); // at you
        // A hand cupped by its ear, a glow breathing, until it stops listening.
        eyes_play_fx(CLIP_LISTEN_LOOP, false, LISTEN_RGB, NULL);
        break;
    case VOICE_THINKING:
        show_status(EYES_STATUS_THINKING, 0);
        eyes_set_mood(EYES_MOOD_THINKING);
        // A hand under its chin, until the answer comes.
        eyes_play(CLIP_THINK_LOOP, false);
        eyes_look_around();
        break;
    case VOICE_SPEAKING:
        show_status(EYES_STATUS_IDLE, 0);
        eyes_set_mood(EYES_MOOD_SPEAKING);
        eyes_look_at(0, 0);
        break;
    case VOICE_ERROR:
        show_status(EYES_STATUS_ERROR, STATUS_ERROR_MS);
        eyes_set_mood(EYES_MOOD_SAD);
        eyes_look_around();
        break;
    default:
        break;
    }
}

void behaviour_voice(voice_state_t state)
{
    const voice_state_t was = s.voice;
    const clip_id_t clip = eyes_clip();
    if ((state != VOICE_LISTENING && clip == CLIP_LISTEN_LOOP) || (state != VOICE_THINKING && clip == CLIP_THINK_LOOP)) {
        eyes_stop_clip();
    }
    if (state != VOICE_IDLE) {
        let_go(false); // it listens with its hands free
    }
    s.voice = state;
    s.last_touch_ms = lv_tick_get();
    if (state == VOICE_IDLE) {
        // Back to its own life; the next tick sets the resting mood. An
        // answer that was spoken to the end shows "done" for a moment.
        s.until_ms = 0;
        if (was == VOICE_SPEAKING) {
            show_status(EYES_STATUS_DONE, STATUS_DONE_MS);
        } else if (eyes_get_status() == EYES_STATUS_THINKING) {
            show_status(EYES_STATUS_IDLE, 0);
        }
        if (!s.pressed) {
            eyes_look_around();
        }
        return;
    }
    if (s.dark) {
        light_up(); // called by name: the screen comes back to listen
    }
    if (was == VOICE_IDLE) {
        life_feel(LIFE_FEEL_TALK);
    }
    if (s.asleep) {
        // The wake word wakes it.
        s.asleep = false;
        set_brightness(awake_brightness());
        ESP_LOGI(TAG, "awake: called");
    }
    if (was == VOICE_IDLE) {
        eyes_blink();
        come_home();
    }
    show_voice(state);
}

bool behaviour_play(const char *name)
{
    bool known = false;
    for (size_t i = 0; i < sizeof(SCENES) / sizeof(SCENES[0]) && !known; i++) {
        known = strcmp(name, SCENES[i].name) == 0;
    }
    if (!known) {
        return false;
    }
    s.last_touch_ms = lv_tick_get();
    if (s.asleep) {
        wake_up();
    }
    return play(name);
}

bool behaviour_face(const char *name)
{
    if (s.asleep) {
        return false;
    }
    for (size_t i = 0; i < sizeof(SCENES) / sizeof(SCENES[0]); i++) {
        const scene_t *sc = &SCENES[i];
        if (strcmp(name, sc->name) != 0) {
            continue;
        }
        // Silent: it is talking, and its voice is the sound.
        uint32_t clip_ms = 0;
        const clip_t *c = clips_get(sc->clip);
        if (c != NULL) {
            eyes_play_fx(sc->clip, sc->either_side && (esp_random() & 1), sc->rgb ? sc->rgb : SCENE_RGB, NULL);
            clip_ms = (uint32_t) c->n * 1000 / 30;
        }
        if (sc->ms > 0) {
            react(sc->mood, clip_ms + sc->ms);
        }
        return true;
    }
    return false;
}

int behaviour_scenes(const char **names, int max)
{
    int n = 0;
    for (size_t i = 0; i < sizeof(SCENES) / sizeof(SCENES[0]) && n < max; i++) {
        names[n++] = SCENES[i].name;
    }
    return n;
}

void behaviour_gaze(float x, float y)
{
    lvgl_port_lock();
    if (!s.asleep && !s.pressed && !card_up() && !s.view && s.voice == VOICE_IDLE) {
        s.gaze_ms = lv_tick_get();
        if (s.gaze_ms == 0) {
            s.gaze_ms = 1; // 0 means none
        }
        eyes_look_at(x, y);
    }
    lvgl_port_unlock();
}

void behaviour_motion(motion_event_t event)
{
    lvgl_port_lock();
    on_motion(event);
    lvgl_port_unlock();
}

void behaviour_heard_from(float x)
{
    lvgl_port_lock();
    if (!s.asleep && !s.pressed && !s.menu && !s.view && !card_up() && s.voice == VOICE_IDLE
        && board_settings_on(SETTING_PET_HEARING)) {
        // The gaze turns to the noise, and drifts back on its own. A clear one
        // off to a side, now and then, gets pointed at.
        static uint32_t pointed_ms;
        const uint32_t now = lv_tick_get();
        if (fabsf(x) > HEARD_POINT_X && !eyes_playing() && now - pointed_ms > HEARD_POINT_GAP_MS) {
            pointed_ms = now;
            eyes_play(CLIP_POINT_SOUND, x < 0);
        }
        hold_gaze(x * 0.9f, 0, HEARD_LOOK_MS);
        life_feel(LIFE_FEEL_SOUND);
    }
    lvgl_port_unlock();
}

/* A tap of PWR. */
static void power_tap(void)
{
    if (s.dark) {
        light_up(); // whatever came meanwhile is on the screen now
        if (s.asleep) {
            wake_up();
        }
    } else if (card_up()) {
        // Not to sleep with a question waiting: the buttons answer it.
    } else if (s.menu) {
        menu_close();
    } else if (s.view) {
        inbox_ui_close();
    } else if (s.asleep) {
        wake_up();
    } else if (s.voice == VOICE_IDLE) {
        eyes_look_around();
        fall_asleep();
    }
}

void behaviour_sleep(void)
{
    if (card_up()) {
        return; // a question waits for its answer
    }
    menu_close();
    inbox_ui_close();
    if (!s.asleep) {
        eyes_look_around();
        fall_asleep();
    }
}

/* PWR held: talking to Yoru without the wake word. */
static void power_hold(void)
{
    if (s.dark) {
        light_up();
    }
    if (card_up()) {
        return; // answered with the buttons
    }
    if (s.asleep) {
        wake_up();
    }
    menu_close();
    s.talking = voice_talk();
    if (!s.talking) {
        toast_show(voice_available() ? card_tr("The mic is muted", "Il microfono e' spento")
                                     : card_tr("No voice on this board", "Niente voce su questa board"),
                   3, TOAST_ERROR);
    }
}

void behaviour_power_key(board_pmu_key_t ev)
{
    lvgl_port_lock();
    s.last_touch_ms = lv_tick_get();
    switch (ev) {
    case BOARD_PMU_KEY_SHORT:
        power_tap();
        break;
    case BOARD_PMU_KEY_HOLD:
        power_hold();
        break;
    case BOARD_PMU_KEY_RELEASE:
        if (s.talking) {
            s.talking = false;
            voice_talk_end();
        }
        break;
    }
    lvgl_port_unlock();
}

static void on_menu(bool open)
{
    s.menu = open;
    s.last_touch_ms = lv_tick_get();
    if (open) {
        s.until_ms = 0;
        come_home();
        eyes_set_mood(EYES_MOOD_PEEK);
        eyes_look_at(0, -1);
    } else {
        eyes_blink();
        eyes_look_around();
    }
}

/* With a request up: awake, small and expectant, glancing from the
 * question to the buttons. A conversation keeps the eyes it has. */
static void wait_for_answer(uint32_t now)
{
    s.last_touch_ms = now;
    if (s.asleep) {
        set_brightness(asleep_brightness()); // face down: it stays there
        return;
    }
    set_brightness(awake_brightness());
    if (s.voice != VOICE_IDLE) {
        return;
    }
    eyes_set_mood(EYES_MOOD_WAITING);
    if ((int32_t) (now - s.next_glance_ms) >= 0) {
        s.next_glance_ms = now + GLANCE_MIN_MS + esp_random() % GLANCE_SPAN_MS;
        s.glance_up = !s.glance_up;
        eyes_look_at(0, s.glance_up ? -1 : 1);
    }
}

/* A confirmation came up, or went: awake, home and waiting while it's up;
 * a nod or a shake of the head when it was answered here. */
static void on_approval(bool open, int allow)
{
    const uint32_t now = lv_tick_get();
    s.approval = open;
    s.last_touch_ms = now;
    s.until_ms = 0;
    s.gaze_ms = 0;
    if (open) {
        if (s.asleep && !s.face_down) {
            s.asleep = false; // it wakes, quietly
            ESP_LOGI(TAG, "awake: Yoru asks");
        }
        playback_sound(SOUND_HMM);
        eyes_blink();
        come_home();
        s.glance_up = false; // first at the question
        s.next_glance_ms = now;
        wait_for_answer(now);
        return;
    }
    if (allow > 0) {
        playback_sound(SOUND_YES);
        eyes_play(CLIP_NOD_YES, false);
        react(EYES_MOOD_HAPPY, 2000);
    } else if (allow == 0) {
        playback_sound(SOUND_NOPE);
        eyes_play(CLIP_SHAKE_NO, false);
    } else {
        eyes_blink();
    }
    eyes_look_around();
}

static void on_view(bool open)
{
    s.view = open;
    s.last_touch_ms = lv_tick_get();
    if (open) {
        if (s.asleep) {
            wake_up(); // a tap on the inbox's banner opens it while dimmed
        }
        s.until_ms = 0;
        come_home();
    } else {
        eyes_blink();
        eyes_look_around();
    }
}

void behaviour_boot_key(bool long_press)
{
    if (s.dark) {
        return; // only PWR lights the screen
    }
    s.last_touch_ms = lv_tick_get();
    if (!board_settings_on(SETTING_BOOT_KEY) || long_press || card_up()) {
        return; // a card is answered on the screen
    }
    if (s.asleep) {
        wake_up();
    }
    if (s.menu) {
        menu_close();
        return;
    }
    if (s.view) {
        inbox_ui_close();
        return;
    }
    inbox_ui_open(NULL);
}

/* An errand ended: its happy (or sad) face stays a moment, then back to
 * the conversation's, or to its own life. */
static void on_errand(bool busy)
{
    const uint32_t now = lv_tick_get();
    s.last_touch_ms = now;
    if (busy) {
        s.until_ms = 0;
        s.gaze_ms = 0;
        return;
    }
    if (s.voice != VOICE_IDLE) {
        show_voice(s.voice);
    } else {
        s.until_ms = now + 1500;
    }
}

/* Where Yoru can show what it does for you: awake, on the screen, and not
 * over what you are doing. */
static bool can_show_work(void)
{
    return !s.asleep && !s.dark && !s.face_down && !s.menu && !s.view && !card_up() && !s.pressed
           && (s.voice == VOICE_IDLE || s.voice == VOICE_THINKING);
}

/* The searches out now. A web search spins a globe; two or more at once are
 * juggled; when the last comes back, one ending for all of them. */
#define SEARCHES_MAX 6
static struct {
    char id[SEARCHES_MAX][24];
    int n, ok;
} s_search;

static bool search_showing(void)
{
    const clip_id_t c = eyes_clip();
    return c == CLIP_SEARCH_LOOP || c == CLIP_WEB_SEARCH_LOOP || c == CLIP_SEARCH_JUGGLE_LOOP;
}

static bool search_start(const char *id, const char *source, uint32_t rgb, const char *glyph)
{
    if (!search_showing()) {
        s_search.n = s_search.ok = 0; // the last ones were cut short
        if (!can_show_work()) {
            return false;
        }
    }
    if (s_search.n < SEARCHES_MAX) {
        snprintf(s_search.id[s_search.n++], sizeof(s_search.id[0]), "%s", id);
    }
    clip_id_t clip = CLIP_SEARCH_LOOP;
    if (s_search.n > 1) {
        clip = CLIP_SEARCH_JUGGLE_LOOP;
    } else if (source[0] == '\0' || strcasecmp(source, "web") == 0) {
        clip = CLIP_WEB_SEARCH_LOOP;
    }
    if (eyes_clip() != clip) {
        eyes_play_fx(clip, false, rgb, glyph);
    }
    return true;
}

static bool search_end(const char *id, bool ok, uint32_t rgb, const char *glyph)
{
    int i = 0;
    while (i < s_search.n && strcmp(s_search.id[i], id) != 0) {
        i++;
    }
    if (i == s_search.n) {
        return false;
    }
    memmove(s_search.id[i], s_search.id[i + 1], (size_t) (s_search.n - i - 1) * sizeof(s_search.id[0]));
    s_search.n--;
    s_search.ok += ok;
    if (s_search.n > 0 || !search_showing()) {
        return false; // still juggling the others, or the clip was cut short
    }
    const bool found = s_search.ok > 0;
    s_search.ok = 0;
    eyes_play_fx(found ? CLIP_SEARCH_FOUND : CLIP_TOOL_FAILED, false, rgb, glyph);
    if (s.voice == VOICE_IDLE) {
        playback_sound(found ? SOUND_TADA : SOUND_WHIMPER);
    }
    return true;
}

/* A tool that isn't an errand: a clip in the service's colour. A search
 * plays until its end comes; the rest are over in a moment. */
static bool tool_clip(const char *id, const char *source, const char *state, const char *kind)
{
    const bool start = strcmp(state, "start") == 0;
    uint32_t rgb;
    const char *glyph;
    if (strcmp(kind, "search") == 0) {
        errand_look(source[0] != '\0' ? source : "web", &rgb, &glyph);
        return start ? search_start(id, source, rgb, glyph) : search_end(id, strcmp(state, "done") == 0, rgb, glyph);
    }
    if (strcmp(kind, "send") == 0) {
        errand_look(source, &rgb, &glyph);
        if (start) {
            if (!can_show_work()) {
                return false;
            }
            eyes_play_fx(CLIP_SEND_NOTE, eyes_where().x > 0, rgb, glyph);
            if (s.voice == VOICE_IDLE) {
                playback_sound(SOUND_WHISTLE);
            }
            return true;
        }
        if (strcmp(state, "failed") == 0 && can_show_work()) {
            eyes_play_fx(CLIP_TOOL_FAILED, false, rgb, glyph);
            return true;
        }
        return false;
    }
    if (!start || !can_show_work()) {
        return false; // a memory or a note is over as it starts
    }
    if (strcmp(kind, "note") == 0) {
        errand_look(source, &rgb, &glyph);
        eyes_play_fx(CLIP_JOT_NOTE, false, rgb, glyph);
    } else {
        errand_look("memory", &rgb, &glyph);
        const bool forget = strcmp(kind, "forget") == 0;
        eyes_play_fx(forget ? CLIP_MEMORY_FORGET : CLIP_MEMORY_SAVE, eyes_where().x > 0, rgb, NULL);
    }
    if (s.voice == VOICE_IDLE) {
        playback_sound(SOUND_CHIRP);
    }
    return true;
}

bool behaviour_fetch(const char *id, const char *source, const char *state, const char *kind)
{
    if (kind[0] != '\0' && strcmp(kind, "fetch") != 0) {
        s.last_touch_ms = lv_tick_get();
        const bool shown = tool_clip(id, source, state, kind);
        // The service's own icon on the card, when the host sent it; not on
        // Yoru's memory's orb.
        if (shown && strcmp(kind, "remember") != 0 && strcmp(kind, "forget") != 0) {
            eyes_fx_icon(icons_find(source));
        }
        return shown;
    }
    if (strcmp(state, "start") != 0) {
        errand_end(id, strcmp(state, "done") == 0);
        return errand_busy();
    }
    if (!can_show_work()) {
        return false;
    }
    errand_start(id, source);
    return true;
}

/* A meeting the host records (`set_meeting`): the recording mark while it
 * records, a nod now and then that Yoru is listening, then Yoru writing the
 * transcript and squeezing it into a summary, and the summary held up. */
#define MEET_LISTEN_EVERY_MS (150 * 1000)
#define MEET_WORK_MAX_MS (20 * 60 * 1000)
#define MEET_HOST_GONE_MS (30 * 1000)
typedef enum { MEET_IDLE, MEET_REC, MEET_WRITE, MEET_SQUEEZE } meet_t;
static struct {
    meet_t state;
    char source[ICON_NAME_MAX + 1];
    uint32_t rgb;
    const char *glyph;
    uint32_t since_ms, listen_ms, host_seen_ms;
} s_meet;

static clip_id_t meet_loop(void)
{
    return s_meet.state == MEET_WRITE ? CLIP_MEETING_WRITE_LOOP : CLIP_MEETING_SQUEEZE_LOOP;
}

static void meet_play(clip_id_t clip)
{
    eyes_play_fx(clip, false, s_meet.rgb, s_meet.glyph);
    eyes_fx_icon(icons_find(s_meet.source));
}

/* Stop the work loop if it's the one playing. */
static void meet_stop_loop(void)
{
    const clip_id_t c = eyes_clip();
    if (c == CLIP_MEETING_WRITE_LOOP || c == CLIP_MEETING_SQUEEZE_LOOP) {
        eyes_stop_clip();
    }
}

static void meet_go_idle(void)
{
    meet_stop_loop();
    s_meet.state = MEET_IDLE;
    recmark_set(NULL, 0, NULL);
}

bool behaviour_meeting(const char *state, const char *source)
{
    const uint32_t now = lv_tick_get();
    s_meet.host_seen_ms = now;
    if (source[0] != '\0' || strcmp(state, "recording") == 0) {
        snprintf(s_meet.source, sizeof(s_meet.source), "%s", source[0] != '\0' ? source : "Computer");
        errand_look(s_meet.source, &s_meet.rgb, &s_meet.glyph);
    }
    const meet_t was = s_meet.state;
    if (strcmp(state, "recording") == 0) {
        s_meet.state = MEET_REC;
        recmark_set(s_meet.source, s_meet.rgb, s_meet.glyph);
        if (was == MEET_REC) {
            return true;
        }
        meet_stop_loop();
        s_meet.since_ms = now;
        s_meet.listen_ms = now + MEET_LISTEN_EVERY_MS;
        if (!can_show_work()) {
            return false;
        }
        // No sound: the microphone is recording the meeting.
        s.last_touch_ms = now;
        meet_play(CLIP_MEETING_START);
        return true;
    }
    if (strcmp(state, "transcribing") == 0 || strcmp(state, "summarizing") == 0) {
        recmark_set(NULL, 0, NULL);
        s_meet.state = state[0] == 't' ? MEET_WRITE : MEET_SQUEEZE;
        if (was != s_meet.state) {
            s_meet.since_ms = now;
        }
        if (eyes_clip() == meet_loop() || !can_show_work()) {
            return eyes_clip() == meet_loop();
        }
        meet_play(meet_loop());
        return true;
    }
    const bool done = strcmp(state, "done") == 0, failed = strcmp(state, "failed") == 0;
    meet_go_idle();
    if ((!done && !failed) || was == MEET_IDLE || !can_show_work()) {
        return false;
    }
    s.last_touch_ms = now;
    meet_play(done ? CLIP_MEETING_DONE : CLIP_TOOL_FAILED);
    if (s.voice == VOICE_IDLE) {
        playback_sound(done ? SOUND_TADA : SOUND_WHIMPER);
    }
    return true;
}

/* While a meeting goes on: the work loop back after something cut it short,
 * a nod now and then while it records, and nothing left over if the host
 * went away. */
static void meeting_tick(uint32_t now)
{
    if (s_meet.state == MEET_IDLE) {
        return;
    }
    if (metrics_model_live()) {
        s_meet.host_seen_ms = now;
    } else if (now - s_meet.host_seen_ms >= MEET_HOST_GONE_MS) {
        meet_go_idle();
        return;
    }
    if (s_meet.state != MEET_REC) {
        if (now - s_meet.since_ms >= MEET_WORK_MAX_MS) {
            meet_go_idle();
        } else if (!eyes_playing() && can_show_work() && s.until_ms == 0) {
            meet_play(meet_loop());
        }
        return;
    }
    if ((int32_t) (now - s_meet.listen_ms) >= 0) {
        s_meet.listen_ms = now + MEET_LISTEN_EVERY_MS;
        if (!eyes_playing() && can_show_work()) {
            meet_play(CLIP_MEETING_LISTEN);
        }
    }
}

const char *behaviour_meeting_state(void)
{
    static const char *const NAMES[] = {"idle", "recording", "transcribing", "summarizing"};
    return NAMES[s_meet.state];
}

void behaviour_start(lv_obj_t *face)
{
    const uint32_t now = lv_tick_get();
    s.last_touch_ms = now;
    s.woke_ms = now;
    s.brightness = -1;
    set_brightness(awake_brightness());
    life_init();

    lv_obj_add_event_cb(face, touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(face, touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(face, touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(face, touch_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(face, touch_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_timer_create(tick_cb, TICK_MS, NULL);
    lv_timer_create(lean_cb, LEAN_MS, NULL);
    s.face = face;
    errand_init(lv_obj_get_parent(face), on_errand);
    menu_init(face, on_menu);
    approval_init(on_approval);
    toast_init();
    recmark_init();
    inbox_ui_init(face, on_view);
    board_button_init(behaviour_boot_key);
}
