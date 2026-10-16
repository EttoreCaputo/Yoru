#include "behaviour.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "board_display.h"
#include "board_settings.h"
#include "esp_log.h"
#include "esp_random.h"
#include "eyes.h"
#include "lvgl_port.h"
#include "menu.h"
#include "motion.h"
#include "pet.h"
#include "playback.h"

#define TICK_MS 250
#define BRIGHTNESS_ASLEEP 12
/* Three pokes closer than this make it grumpy. */
#define POKE_WINDOW_MS 1500
/* How much finger travel counts as stroking. */
#define STROKE_PX 160
/* A whim every 15-40 s, times pet_idle_pace(). */
#define WHIM_MIN_MS 15000
#define WHIM_SPAN_MS 25000
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

static const char *TAG = "behaviour";

/* How each of pet.c's scenes looks: a mood for a while, a poke or a blink to
 * start it, a gesture of the hands, and the pet's sound for it (SOUND_COUNT
 * for none; playback.c drops them while pet sounds are off). Names as in
 * DualEye, so the host's scenes and the pet's reactions mean the same on both
 * boards; the gesture scenes are Yoru's own. */
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

/* Where the eyes rest in each of the pet's moods. The cheerful ones rest
 * neutral: happy eyes all the time would mean nothing. */
static const eyes_mood_t RESTING[PET_MOOD_COUNT] = {
    [PET_MOOD_CONTENT] = EYES_MOOD_NEUTRAL, [PET_MOOD_HAPPY] = EYES_MOOD_NEUTRAL,
    [PET_MOOD_EXCITED] = EYES_MOOD_NEUTRAL, [PET_MOOD_LOVING] = EYES_MOOD_NEUTRAL,
    [PET_MOOD_BORED] = EYES_MOOD_BORED,     [PET_MOOD_GRUMPY] = EYES_MOOD_GRUMPY,
    [PET_MOOD_SAD] = EYES_MOOD_SAD,         [PET_MOOD_SLEEPY] = EYES_MOOD_SLEEPY,
    [PET_MOOD_HOT] = EYES_MOOD_HOT,
};

static struct {
    uint32_t last_touch_ms;
    /* A short reaction over the resting mood, until `until_ms`. */
    uint32_t until_ms;
    bool asleep;
    int pokes;
    uint32_t last_poke_ms;
    uint32_t next_whim_ms;
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
    /* Turned face down: asleep, and the mic is off until it's turned back. */
    bool face_down;
    uint32_t rocked_ms;
} s;

static void show_voice(voice_state_t state);
static void apply_rotation(void);

static void set_brightness(int percent)
{
    if (percent != s.brightness) {
        s.brightness = percent;
        board_display_set_brightness(percent);
    }
}

/* At night (the host sends the time) the screen is dimmer. */
static bool night(void)
{
    pet_state_t pet;
    pet_get(&pet);
    if (pet.minute < 0) {
        return false;
    }
    const int h = pet.minute / 60;
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

/* Show one of pet.c's scenes; false if it isn't one these eyes know. */
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
    }
    s.asleep = true;
    s.until_ms = 0;
    eyes_set_mood(EYES_MOOD_ASLEEP);
    set_brightness(asleep_brightness());
}

static void wake_up(void)
{
    ESP_LOGI(TAG, "awake");
    s.asleep = false;
    set_brightness(awake_brightness());
    eyes_blink();
    eyes_gesture(EYES_GESTURE_WAVE); // hello again
    react(EYES_MOOD_CURIOUS, 1500);
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

static void schedule_whim(uint32_t now)
{
    s.next_whim_ms = now + (uint32_t) ((float) (WHIM_MIN_MS + esp_random() % WHIM_SPAN_MS) * pet_idle_pace());
}

/* Where the finger is, as a gaze target. */
static void watch_finger(lv_indev_t *indev)
{
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    eyes_look_at((float) (p.x - BOARD_LCD_H_RES / 2) / 180.0f, (float) (p.y - BOARD_LCD_V_RES / 2) / 180.0f);
    s.stroke += abs(p.x - s.last_point.x) + abs(p.y - s.last_point.y);
    s.last_point = p;
    if (!s.stroked && !s.asleep && s.voice == VOICE_IDLE && s.stroke > STROKE_PX && !s.from_top
        && lv_tick_elaps(s.press_ms) >= STROKE_MIN_MS && board_settings_on(SETTING_TOUCH_REACTIONS)) {
        s.stroked = true;
        pet_event(PET_EVENT_STROKED);
        playback_sound(SOUND_PURR);
        react(pet_mood() == PET_MOOD_LOVING ? EYES_MOOD_LOVING : EYES_MOOD_HAPPY, 2500);
    }
}

static void poke(uint32_t now)
{
    s.pokes = (now - s.last_poke_ms < POKE_WINDOW_MS) ? s.pokes + 1 : 1;
    s.last_poke_ms = now;
    eyes_poke();
    if (s.pokes >= 3) {
        pet_event(PET_EVENT_ANNOYED);
        play("annoyed");
    } else {
        pet_event(PET_EVENT_POKED);
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
    const float len = sqrtf((float) (dx * dx + dy * dy));
    eyes_knock((float) dx / len * 0.9f, (float) dy / len * 0.5f);
    if (s.slaps >= 3) {
        s.slaps = 0;
        pet_event(PET_EVENT_ANNOYED);
        play("annoyed");
    } else {
        pet_event(PET_EVENT_POKED);
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
            poke(now);
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

static void tick_cb(lv_timer_t *timer)
{
    const uint32_t now = lv_tick_get();
    const uint32_t idle = now - s.last_touch_ms;
    const bool focus = board_settings_on(SETTING_FOCUS);
    // Taken even when it can't be shown, so an old one doesn't play later.
    const char *reaction = pet_take_reaction();

    if (s.status_until_ms != 0 && (int32_t) (now - s.status_until_ms) >= 0) {
        s.status_until_ms = 0;
        eyes_set_status(EYES_STATUS_IDLE);
    }
    apply_rotation();
    if (s.menu) {
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
    if (idle >= (uint32_t) board_settings_get(SETTING_ASLEEP_AFTER_S) * 1000) {
        fall_asleep();
        return;
    }
    if (reaction != NULL && !focus && play(reaction)) {
        return;
    }
    if (s.until_ms != 0 && (int32_t) (now - s.until_ms) < 0) {
        return; // a reaction is still playing
    }
    s.until_ms = 0;

    if (idle >= (uint32_t) board_settings_get(SETTING_SLEEPY_AFTER_S) * 1000) {
        eyes_set_mood(EYES_MOOD_SLEEPY);
        return;
    }
    const pet_mood_t mood = pet_mood();
    eyes_set_mood(mood < PET_MOOD_COUNT ? RESTING[mood] : EYES_MOOD_NEUTRAL);

    // Little whims, so it never looks frozen.
    if ((int32_t) (now - s.next_whim_ms) >= 0) {
        schedule_whim(now);
        if (!focus && !pet_quiet()) {
            play(pet_idle_scene());
        }
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
    if (!s.asleep && !s.menu && board_settings_on(SETTING_MOTION_REACTIONS)) {
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
    if (s.menu || s.voice != VOICE_IDLE || s.face_down) {
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
            pet_heard(PET_HEARD_CLAPS, 2);
        }
        return;
    }
    if (s.asleep || !reactions) {
        return;
    }
    s.last_touch_ms = now;
    switch (ev) {
    case MOTION_LIFTED:
        pet_event(PET_EVENT_LIFTED);
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
        pet_event(PET_EVENT_SHAKEN);
        play("dizzy");
        break;
    case MOTION_SHAKEN_LONG:
        pet_event(PET_EVENT_ANNOYED);
        play("annoyed");
        break;
    case MOTION_KNOCK:
        eyes_knock(0, 0.35f);
        poke(now);
        break;
    case MOTION_ROCKED:
        pet_event(PET_EVENT_ROCKED);
        if (s.rocked_ms != 0 && now - s.rocked_ms < LULLABY_MS) {
            s.rocked_ms = 0;
            fall_asleep();
        } else {
            s.rocked_ms = now;
            play("drowsy");
        }
        break;
    case MOTION_DROPPED:
        pet_event(PET_EVENT_DROPPED);
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
    if (s.asleep) {
        // The wake word wakes it.
        s.asleep = false;
        set_brightness(awake_brightness());
        ESP_LOGI(TAG, "awake: called");
    }
    if (was == VOICE_IDLE) {
        eyes_blink();
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
    if (!s.asleep && !s.pressed && s.voice == VOICE_IDLE) {
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
    if (!s.asleep && !s.pressed && !s.menu && s.voice == VOICE_IDLE && board_settings_on(SETTING_PET_HEARING)) {
        // The gaze turns to the noise, and drifts back on its own.
        hold_gaze(x * 0.9f, 0, HEARD_LOOK_MS);
    }
    lvgl_port_unlock();
}

void behaviour_power_key(void)
{
    lvgl_port_lock();
    s.last_touch_ms = lv_tick_get();
    if (s.menu) {
        menu_close();
    } else if (s.asleep) {
        wake_up();
    } else if (s.voice == VOICE_IDLE) {
        eyes_look_around();
        fall_asleep();
    }
    lvgl_port_unlock();
}

static void on_menu(bool open)
{
    s.menu = open;
    s.last_touch_ms = lv_tick_get();
    if (open) {
        s.until_ms = 0;
        eyes_set_mood(EYES_MOOD_PEEK);
        eyes_look_at(0, -1);
    } else {
        eyes_blink();
        eyes_look_around();
    }
}

void behaviour_start(lv_obj_t *face)
{
    const uint32_t now = lv_tick_get();
    s.last_touch_ms = now;
    s.brightness = -1;
    schedule_whim(now);
    set_brightness(awake_brightness());

    lv_obj_add_event_cb(face, touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(face, touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(face, touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(face, touch_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(face, touch_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_timer_create(tick_cb, TICK_MS, NULL);
    lv_timer_create(lean_cb, LEAN_MS, NULL);
    menu_init(face, on_menu);
}
