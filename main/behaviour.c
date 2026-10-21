#include "behaviour.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agents.h"
#include "agents_ui.h"
#include "approval.h"
#include "board_button.h"
#include "board.h"
#include "board_display.h"
#include "board_settings.h"
#include "card.h"
#include "esp_log.h"
#include "esp_random.h"
#include "errand.h"
#include "eyes.h"
#include "inbox.h"
#include "inbox_ui.h"
#include "life.h"
#include "lvgl_port.h"
#include "menu.h"
#include "motion.h"
#include "metrics_model.h"
#include "playback.h"
#include "question.h"
#include "draft.h"
#include "remote_ui.h"
#include "timers.h"

/* Often enough for life.c's moves. */
#define TICK_MS 100
#define BRIGHTNESS_ASLEEP 12
/* Asleep this long, the screen goes dark; only PWR lights it again. The
 * board keeps listening to the host meanwhile. */
#define DARK_AFTER_MS 60000
/* `to` of a push-to-talk utterance, as JSON text. */
#define TO_MAX 160
/* Three pokes closer than this make it grumpy. */
#define POKE_WINDOW_MS 1500
/* How much finger travel counts as stroking. */
#define STROKE_PX 160
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
/* Rocked twice within this long, it falls asleep. */
#define LULLABY_MS 60000
#define LEAN_MS 50
/* How long "done" (green) and "error" (red) stay on the sphere. */
#define STATUS_DONE_MS 2500
#define STATUS_ERROR_MS 3500
/* Waiting on an answer, the eyes go between the request and the buttons. */
#define GLANCE_MIN_MS 1800
#define GLANCE_SPAN_MS 1600
/* While an agent waits on you, Yoru comes to knock this often. */
#define CALL_EVERY_MS 60000

static const char *TAG = "behaviour";

/* How each scene looks: a mood for a while, a poke or a blink to start it, a
 * gesture of the hands, and Yoru's sound for it (SOUND_COUNT for none;
 * playback.c drops them while its sounds are off). Names as in DualEye, so the
 * host's scenes mean the same on both boards; the gesture scenes are Yoru's
 * own. */
typedef enum { FX_NONE, FX_POKE, FX_BLINK } fx_t;

typedef struct {
    const char *name;
    eyes_mood_t mood;
    uint16_t ms;
    fx_t fx;
    eyes_gesture_t gesture;
    sound_t sound;
} scene_t;

#define G(name) EYES_GESTURE_##name
static const scene_t SCENES[] = {
    {"happy", EYES_MOOD_HAPPY, 2000, FX_NONE, G(NONE), SOUND_HAPPY},
    {"excited", EYES_MOOD_HAPPY, 2500, FX_POKE, G(CLAP), SOUND_EXCITED},
    {"wink", EYES_MOOD_HAPPY, 1200, FX_BLINK, G(NONE), SOUND_WINK},
    {"proud", EYES_MOOD_HAPPY, 2500, FX_NONE, G(CLAP), SOUND_TADA},
    {"relieved", EYES_MOOD_HAPPY, 1800, FX_BLINK, G(NONE), SOUND_PHEW},
    {"charged", EYES_MOOD_HAPPY, 1500, FX_POKE, G(NONE), SOUND_CHARGE},
    // Bobbing to the beat comes with the music, in M5.
    {"dance", EYES_MOOD_HAPPY, 3000, FX_POKE, G(CLAP), SOUND_BEAT},
    {"love", EYES_MOOD_LOVING, 3500, FX_NONE, G(NONE), SOUND_AWW},
    {"curious", EYES_MOOD_CURIOUS, 2500, FX_NONE, G(NONE), SOUND_HMM},
    {"thinking", EYES_MOOD_THINKING, 3000, FX_NONE, G(THINK), SOUND_COUNT},
    {"focus", EYES_MOOD_THINKING, 3000, FX_BLINK, G(NONE), SOUND_SCAN},
    {"angry", EYES_MOOD_GRUMPY, 2500, FX_POKE, G(NONE), SOUND_GRUMBLE},
    {"hot", EYES_MOOD_HOT, 4000, FX_NONE, G(NONE), SOUND_PANT},
    {"sleepy", EYES_MOOD_SLEEPY, 2500, FX_NONE, G(NONE), SOUND_COUNT},
    {"yawn", EYES_MOOD_SLEEPY, 2000, FX_NONE, G(STRETCH), SOUND_YAWN},
    {"tired", EYES_MOOD_SLEEPY, 2500, FX_BLINK, G(NONE), SOUND_DRAIN},
    // A sigh: squash, then a slow, heavy-lidded moment.
    {"sigh", EYES_MOOD_SLEEPY, 1200, FX_POKE, G(NONE), SOUND_SIGH},
    {"sad", EYES_MOOD_SAD, 3500, FX_NONE, G(NONE), SOUND_WHIMPER},
    {"surprised", EYES_MOOD_SURPRISED, 1500, FX_POKE, G(NONE), SOUND_GASP},
    {"scared", EYES_MOOD_SCARED, 3000, FX_POKE, G(COVER), SOUND_EEP},
    {"bored", EYES_MOOD_BORED, 3000, FX_NONE, G(NONE), SOUND_COUNT},
    {"look_around", EYES_MOOD_NEUTRAL, 0, FX_NONE, G(NONE), SOUND_COUNT},
    {"blink", EYES_MOOD_NEUTRAL, 0, FX_BLINK, G(NONE), SOUND_COUNT},
    // The hands on their own.
    {"wave", EYES_MOOD_HAPPY, 1800, FX_NONE, G(WAVE), SOUND_HELLO},
    {"point", EYES_MOOD_CURIOUS, 1800, FX_NONE, G(POINT), SOUND_HMM},
    {"shy", EYES_MOOD_HAPPY, 3000, FX_NONE, G(COVER), SOUND_COUNT},
    {"clap", EYES_MOOD_HAPPY, 1300, FX_NONE, G(CLAP), SOUND_COUNT},
    {"stretch", EYES_MOOD_SLEEPY, 1800, FX_NONE, G(STRETCH), SOUND_YAWN},
    // The case as an input (motion.c) and slaps.
    {"dizzy", EYES_MOOD_DIZZY, 3200, FX_POKE, G(HOLD), SOUND_CONFUSED},
    {"annoyed", EYES_MOOD_GRUMPY, 2800, FX_POKE, G(SHOO), SOUND_GRUMBLE},
    {"falling", EYES_MOOD_SCARED, 1500, FX_NONE, G(FLAIL), SOUND_EEP},
    {"drowsy", EYES_MOOD_SLEEPY, 4000, FX_NONE, G(NONE), SOUND_YAWN},
    {"lifted", EYES_MOOD_CURIOUS, 1800, FX_BLINK, G(NONE), SOUND_CHIRP},
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
    /* A permission request is up (approval.c); the glances between its
     * words and its buttons. */
    bool approval;
    uint32_t next_glance_ms;
    bool glance_up;
    /* Turned face down: asleep, and the mic is off until it's turned back. */
    bool face_down;
    uint32_t rocked_ms;
    lv_obj_t *face;
    /* An agent's question is up (question.c); the agents view covers the
     * screen (agents_ui.c). */
    bool question;
    bool view;
    /* The press started at the bottom edge: a swipe up opens the view. */
    bool from_bottom;
    /* The sphere is tinted because an agent works, not for the voice. */
    bool agent_tint;
    bool agents_waiting;
    uint32_t next_call_ms;
    /* A draft is up (draft.c). */
    bool draft;
    /* Asleep since then; the screen is dark (only PWR lights it). */
    uint32_t asleep_ms;
    bool dark;
    /* PWR is held and its words stream. */
    bool talking;
    /* It came to the glass for a notification, not for an agent. */
    bool inbox_call;
} s;

static void show_voice(voice_state_t state);
static bool card_up(void);
static void apply_rotation(void);
static void wait_for_answer(uint32_t now);

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

/* The host's clock and idle time, from its snapshots (minute -1 and idle 0
 * until it sends them). */
static void host_now(int *minute, uint32_t *idle_ms)
{
    static metrics_snapshot_t snap; // big for the LVGL task's stack
    metrics_model_get(&snap);
    *minute = snap.host.valid ? snap.host.minute : -1;
    *idle_ms = snap.host.valid ? (uint32_t) snap.host.idle_s * 1000 : UINT32_MAX;
}

/* At night (the host sends the time) the screen is dimmer. */
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
    return night() && dark < day ? dark : day;
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
        if (sc->fx == FX_POKE) {
            eyes_poke();
        } else if (sc->fx == FX_BLINK) {
            eyes_blink();
        }
        if (sc->gesture != EYES_GESTURE_NONE) {
            eyes_gesture(sc->gesture);
        }
        if (sc->ms > 0) {
            react(sc->mood, sc->ms);
        }
        return true;
    }
    ESP_LOGW(TAG, "no look for scene %s", name);
    return false;
}

static void fall_asleep(void)
{
    if (!s.asleep) {
        ESP_LOGI(TAG, "asleep");
        s.asleep_ms = lv_tick_get();
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
    eyes_blink();
    eyes_gesture(EYES_GESTURE_WAVE); // hello again
    react(EYES_MOOD_CURIOUS, 1500);
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

/* Where the finger is, as a gaze target. */
static void watch_finger(lv_indev_t *indev)
{
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    eyes_look_at((float) (p.x - BOARD_LCD_H_RES / 2) / 180.0f, (float) (p.y - BOARD_LCD_V_RES / 2) / 180.0f);
    s.stroke += abs(p.x - s.last_point.x) + abs(p.y - s.last_point.y);
    s.last_point = p;
    if (!s.stroked && !s.asleep && s.voice == VOICE_IDLE && s.stroke > STROKE_PX && !s.from_top && !s.from_bottom
        && lv_tick_elaps(s.press_ms) >= STROKE_MIN_MS && board_settings_on(SETTING_TOUCH_REACTIONS)) {
        s.stroked = true;
        life_attend();
        playback_sound(SOUND_PURR);
        react(EYES_MOOD_HAPPY, 2500);
    }
}

static void poke(uint32_t now)
{
    if (life_calling()) {
        // It was knocking for an agent: a touch says "I'm on it", and the
        // terminal comes to the front.
        agents_summary_t sum;
        agents_summary(&sum);
        if (sum.waiting > 0 && sum.focus) {
            agents_ui_shortcut("focus", sum.wait_id);
        } else if (s.inbox_call && inbox_unread(NULL) > 0) {
            // It knocked for a notification: here it is.
            s.inbox_call = false;
            life_attend();
            inbox_ui_open(NULL);
            return;
        }
    }
    s.pokes = (now - s.last_poke_ms < POKE_WINDOW_MS) ? s.pokes + 1 : 1;
    s.last_poke_ms = now;
    life_attend();
    eyes_poke();
    if (s.pokes >= 3) {
        play("annoyed");
    } else {
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
    const float len = sqrtf((float) (dx * dx + dy * dy));
    eyes_knock((float) dx / len * 0.9f, (float) dy / len * 0.5f);
    if (s.slaps >= 3) {
        s.slaps = 0;
        play("annoyed");
    } else {
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
        s.from_bottom = s.press_point.y > BOARD_LCD_V_RES - MENU_EDGE_PX;
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
            if (s.asleep) {
                wake_up();
            }
            lv_indev_wait_release(indev);
            menu_open();
            break;
        }
        if (s.from_bottom && !s.menu && s.press_point.y - s.last_point.y > MENU_SWIPE_PX) {
            // Pushed up from the bottom edge: the agents.
            s.from_bottom = false;
            s.pressed = false;
            if (s.asleep) {
                wake_up();
            }
            lv_indev_wait_release(indev);
            agents_ui_open();
            break;
        }
        if (!s.asleep) {
            watch_finger(indev);
        }
        break;
    case LV_EVENT_RELEASED: {
        s.pressed = false;
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        const int32_t dx = p.x - s.press_point.x, dy = p.y - s.press_point.y;
        if (!s.asleep && !s.from_top && !s.from_bottom && s.voice == VOICE_IDLE && lv_tick_elaps(s.press_ms) <= SLAP_MAX_MS
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
                poke(now);
            }
        }
        break;
    case LV_EVENT_LONG_PRESSED:
        if (s.voice != VOICE_IDLE || s.stroked || s.stroke > STROKE_PX) {
            break; // not in the middle of a conversation
        }
        eyes_look_around();
        fall_asleep();
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
        // With an agent waiting, it keeps still.
        .focus = board_settings_on(SETTING_FOCUS) || s.agents_waiting,
        .drowsy = lonely_ms >= (uint32_t) board_settings_get(SETTING_SLEEPY_AFTER_S) * 1000,
        .lonely = lonely_ms >= (uint32_t) board_settings_get(SETTING_ASLEEP_AFTER_S) * 1000,
        .agent_sounds = board_settings_on(SETTING_AGENTS_SOUNDS),
    };
}

static bool card_up(void)
{
    return s.approval || s.question || s.draft;
}

/* Long asleep: the screen off and deaf to touch, the board still at work. */
static void go_dark(void)
{
    ESP_LOGI(TAG, "screen dark");
    s.dark = true;
    set_brightness(0);
    lvgl_port_set_touch(false);
}

static void light_up(void)
{
    ESP_LOGI(TAG, "screen lit");
    s.dark = false;
    lvgl_port_set_touch(true);
    s.last_touch_ms = lv_tick_get();
}

/* Whichever view covers the screen: the agents' or the inbox. */
static void close_views(void)
{
    agents_ui_close();
    inbox_ui_close();
}

/* The agents' news (agents.c): a nod when a turn ends, a wince when it
 * fails, a blue tint while one works, and a knock on the glass while one
 * waits for you. Quiet (from the host) keeps it to the tint. */
static void agents_tick(uint32_t now)
{
    const uint32_t ev = agents_take_events();
    agents_summary_t sum;
    agents_summary(&sum);
    s.agents_waiting = sum.waiting > 0;
    const bool quiet = sum.quiet_min > 0;
    const bool sounds = board_settings_on(SETTING_AGENTS_SOUNDS) && !quiet && !board_settings_on(SETTING_FOCUS);
    const bool busy = card_up() || s.menu || s.view || s.voice != VOICE_IDLE || s.face_down;

    if (ev & AGENTS_EV_QUIET_ON) {
        char text[48];
        snprintf(text, sizeof(text), card_tr("Agents quiet for %d min", "Agenti in silenzio per %d min"),
                 sum.quiet_min);
        agents_ui_toast(text, 4, TOAST_INFO);
    } else if (ev & AGENTS_EV_QUIET_OFF) {
        agents_ui_toast(card_tr("Agents can call you again", "Gli agenti possono chiamarti"), 4, TOAST_INFO);
    }
    if ((ev & AGENTS_EV_WAIT) && !quiet) {
        if (s.asleep && !s.face_down) {
            wake_up(); // someone needs you
        }
        s.next_call_ms = now;
    }
    if ((ev & AGENTS_EV_DONE) && !s.asleep && !busy) {
        show_status(EYES_STATUS_DONE, STATUS_DONE_MS);
        if (sounds) {
            play("proud");
        } else {
            eyes_gesture(EYES_GESTURE_CLAP);
            react(EYES_MOOD_HAPPY, 2000);
        }
    } else if ((ev & AGENTS_EV_ERROR) && !s.asleep && !busy) {
        show_status(EYES_STATUS_ERROR, STATUS_ERROR_MS);
        if (sounds) {
            playback_sound(SOUND_UH_OH);
        }
        react(EYES_MOOD_SAD, 2500);
    }

    // Blue while an agent works, unless the voice or a moment's status has it.
    const bool working = sum.working > 0 && !s.asleep;
    if (s.voice == VOICE_IDLE && s.status_until_ms == 0) {
        if (working && eyes_get_status() == EYES_STATUS_IDLE) {
            eyes_set_status(EYES_STATUS_THINKING);
            s.agent_tint = true;
        } else if (!working && s.agent_tint && eyes_get_status() == EYES_STATUS_THINKING) {
            eyes_set_status(EYES_STATUS_IDLE);
            s.agent_tint = false;
        }
    }

    if (sum.waiting > 0 && !quiet && !busy && !s.asleep && (int32_t) (now - s.next_call_ms) >= 0) {
        if (life_call()) {
            s.next_call_ms = now + CALL_EVERY_MS;
            s.inbox_call = false;
        }
    }
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
 * flapping its hands, with the alarm and a line saying which one. The host
 * says it out loud too. */
static void timers_tick(void)
{
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
    agents_ui_toast(text, 6, TOAST_WAIT);
    if (card_up() || s.menu || s.view || s.voice != VOICE_IDLE) {
        return;
    }
    come_home();
    eyes_knock(0, -0.7f); // a jump
    eyes_gesture(EYES_GESTURE_FLAIL);
    react(EYES_MOOD_SURPRISED, 1800);
}

static void tick_cb(lv_timer_t *timer)
{
    const uint32_t now = lv_tick_get();

    if (s.status_until_ms != 0 && (int32_t) (now - s.status_until_ms) >= 0) {
        s.status_until_ms = 0;
        eyes_set_status(EYES_STATUS_IDLE);
    }
    apply_rotation();
    agents_tick(now);
    inbox_tick(now);
    timers_tick();
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
    const bool playing = s.until_ms != 0 && (int32_t) (now - s.until_ms) < 0;
    if (!playing) {
        s.until_ms = 0;
    }
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
        close_views();
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
    switch (ev) {
    case MOTION_LIFTED:
        play("lifted");
        hold_gaze(0, -0.2f, 1800); // at you

        break;
    case MOTION_PUT_DOWN:
        eyes_blink();
        eyes_knock(0, 0.15f);
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
        eyes_knock(0, 0.35f);
        poke(now);
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
        eyes_knock(0, 0.6f);
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
        break;
    case VOICE_THINKING:
        show_status(EYES_STATUS_THINKING, 0);
        eyes_set_mood(EYES_MOOD_THINKING);
        eyes_gesture(EYES_GESTURE_THINK);
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
        // The gaze turns to the noise, and drifts back on its own.
        hold_gaze(x * 0.9f, 0, HEARD_LOOK_MS);
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
        close_views();
    } else if (s.asleep) {
        wake_up();
    } else if (s.voice == VOICE_IDLE) {
        eyes_look_around();
        fall_asleep();
    }
}

/* PWR held: the words go where the page up says, or where the draft went. */
static void power_hold(void)
{
    if (s.dark) {
        light_up();
    }
    if (s.approval || s.question) {
        return; // answered with the buttons
    }
    if (s.asleep) {
        wake_up();
    }
    menu_close();
    char to[TO_MAX] = "";
    if (s.draft) {
        draft_redo(); // the host keeps where it went
    } else {
        remote_ui_target(to, sizeof(to));
    }
    s.talking = voice_talk(to);
    if (!s.talking) {
        agents_ui_toast(voice_available() ? card_tr("The mic is muted", "Il microfono e' spento")
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

/* A card came up (approval or question): awake, home, waiting. */
static void card_opened(uint32_t now)
{
    s.last_touch_ms = now;
    s.until_ms = 0;
    s.gaze_ms = 0;
    if (s.asleep && !s.face_down) {
        // A question wakes it, quietly.
        s.asleep = false;
        ESP_LOGI(TAG, "awake: an agent asks");
    }
    if (board_settings_on(SETTING_AGENTS_SOUNDS)) {
        playback_sound(SOUND_HMM);
    }
    eyes_blink();
    come_home();
    s.glance_up = false; // first at the question
    s.next_glance_ms = now;
    wait_for_answer(now);
}

static void on_question(bool open, int answered)
{
    const uint32_t now = lv_tick_get();
    s.question = open;
    // The card is black: Yoru under it would only cost drawing time.
    if (s.face != NULL) {
        if (open) {
            lv_obj_add_flag(s.face, LV_OBJ_FLAG_HIDDEN);
        } else if (!s.view) {
            lv_obj_remove_flag(s.face, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (open) {
        card_opened(now);
        return;
    }
    s.last_touch_ms = now;
    if (answered > 0) {
        playback_sound(SOUND_YES);
        react(EYES_MOOD_HAPPY, 1200);
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
    if (!board_settings_on(SETTING_BOOT_KEY) || card_up()) {
        return; // a card is answered on the screen
    }
    if (long_press) {
        agents_ui_shortcut("quiet", NULL);
        return;
    }
    if (s.asleep) {
        wake_up();
    }
    if (s.menu) {
        menu_close();
        return;
    }
    if (s.view) {
        close_views();
        return;
    }
    // The agent waiting on you first; then what's unread in the inbox;
    // otherwise what the agents are all doing.
    agents_summary_t sum;
    agents_summary(&sum);
    if (sum.waiting > 0 && sum.focus) {
        eyes_gesture(EYES_GESTURE_POINT);
        agents_ui_shortcut("focus", sum.wait_id);
        return;
    }
    if (sum.waiting == 0 && inbox_unread(NULL) > 0) {
        inbox_ui_open(NULL);
        return;
    }
    agents_ui_open();
}

static void on_draft(bool open, int sent)
{
    s.draft = open;
    s.last_touch_ms = lv_tick_get();
    if (open) {
        card_opened(lv_tick_get());
        return;
    }
    if (sent > 0) {
        playback_sound(SOUND_YES);
        react(EYES_MOOD_HAPPY, 1200);
    } else {
        eyes_blink();
    }
    eyes_look_around();
}

static void on_approval(bool open, int allow)
{
    const uint32_t now = lv_tick_get();
    s.approval = open;
    s.last_touch_ms = now;
    s.until_ms = 0;
    s.gaze_ms = 0;
    if (open) {
        card_opened(now);
        return;
    }
    // Answered here: a nod or a shake of the head. Otherwise just back.
    if (allow > 0) {
        playback_sound(SOUND_YES);
        react(EYES_MOOD_HAPPY, 1200);
    } else if (allow == 0) {
        playback_sound(SOUND_NOPE);
        eyes_knock(0.25f, 0);
    } else {
        eyes_blink();
    }
    eyes_look_around();
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

bool behaviour_fetch(const char *id, const char *source, const char *state)
{
    if (strcmp(state, "start") != 0) {
        errand_end(id, strcmp(state, "done") == 0);
        return errand_busy();
    }
    // Only where it can be seen, and not over what you are doing.
    if (s.asleep || s.dark || s.face_down || s.menu || s.view || card_up() || s.pressed
        || (s.voice != VOICE_IDLE && s.voice != VOICE_THINKING)) {
        return false;
    }
    errand_start(id, source);
    return true;
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
    question_init(on_question);
    draft_init(on_draft);
    agents_ui_init(face, on_view);
    inbox_ui_init(face, on_view);
    board_button_init(behaviour_boot_key);
}
