#include "behaviour.h"

#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "board_display.h"
#include "board_settings.h"
#include "esp_log.h"
#include "esp_random.h"
#include "eyes.h"
#include "lvgl_port.h"
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

static const char *TAG = "behaviour";

/* How each of pet.c's scenes looks with these eyes: a mood for a while, a
 * poke or a blink to start it, and the pet's sound for it (SOUND_COUNT for
 * none; playback.c drops them while pet sounds are off). Names as in DualEye, so the host's scenes
 * and the pet's reactions mean the same on both boards. */
typedef enum { FX_NONE, FX_POKE, FX_BLINK } fx_t;

typedef struct {
    const char *name;
    eyes_mood_t mood;
    uint16_t ms;
    fx_t fx;
    sound_t sound;
} scene_t;

static const scene_t SCENES[] = {
    {"happy", EYES_MOOD_HAPPY, 2000, FX_NONE, SOUND_HAPPY},
    {"excited", EYES_MOOD_HAPPY, 2500, FX_POKE, SOUND_EXCITED},
    {"wink", EYES_MOOD_HAPPY, 1200, FX_BLINK, SOUND_WINK},
    {"proud", EYES_MOOD_HAPPY, 2500, FX_NONE, SOUND_TADA},
    {"relieved", EYES_MOOD_HAPPY, 1800, FX_BLINK, SOUND_PHEW},
    {"charged", EYES_MOOD_HAPPY, 1500, FX_POKE, SOUND_CHARGE},
    // Bobbing to the beat comes with the music, in M5.
    {"dance", EYES_MOOD_HAPPY, 3000, FX_POKE, SOUND_BEAT},
    {"love", EYES_MOOD_LOVING, 3500, FX_NONE, SOUND_AWW},
    {"curious", EYES_MOOD_CURIOUS, 2500, FX_NONE, SOUND_HMM},
    {"thinking", EYES_MOOD_THINKING, 3000, FX_NONE, SOUND_COUNT},
    {"focus", EYES_MOOD_THINKING, 3000, FX_BLINK, SOUND_SCAN},
    {"angry", EYES_MOOD_GRUMPY, 2500, FX_POKE, SOUND_GRUMBLE},
    {"hot", EYES_MOOD_HOT, 4000, FX_NONE, SOUND_PANT},
    {"sleepy", EYES_MOOD_SLEEPY, 2500, FX_NONE, SOUND_COUNT},
    {"yawn", EYES_MOOD_SLEEPY, 2000, FX_POKE, SOUND_YAWN},
    {"tired", EYES_MOOD_SLEEPY, 2500, FX_BLINK, SOUND_DRAIN},
    // A sigh: squash, then a slow, heavy-lidded moment.
    {"sigh", EYES_MOOD_SLEEPY, 1200, FX_POKE, SOUND_SIGH},
    {"sad", EYES_MOOD_SAD, 3500, FX_NONE, SOUND_WHIMPER},
    {"surprised", EYES_MOOD_SURPRISED, 1500, FX_POKE, SOUND_GASP},
    {"scared", EYES_MOOD_SURPRISED, 2000, FX_POKE, SOUND_EEP},
    {"bored", EYES_MOOD_BORED, 3000, FX_NONE, SOUND_COUNT},
    {"look_around", EYES_MOOD_NEUTRAL, 0, FX_NONE, SOUND_COUNT},
    {"blink", EYES_MOOD_NEUTRAL, 0, FX_BLINK, SOUND_COUNT},
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
} s;

static void show_voice(voice_state_t state);

static void set_brightness(int percent)
{
    if (percent != s.brightness) {
        s.brightness = percent;
        board_display_set_brightness(percent);
    }
}

static int awake_brightness(void)
{
    return (int) board_settings_get(SETTING_BRIGHTNESS);
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
    react(EYES_MOOD_CURIOUS, 1500);
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
    if (!s.stroked && !s.asleep && s.voice == VOICE_IDLE && s.stroke > STROKE_PX
        && board_settings_on(SETTING_TOUCH_REACTIONS)) {
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
        playback_sound(SOUND_GRUMBLE);
        react(EYES_MOOD_GRUMPY, 2500);
    } else {
        pet_event(PET_EVENT_POKED);
        playback_sound(SOUND_BOOP);
        react(EYES_MOOD_HAPPY, 1800);
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
        s.stroke = 0;
        s.stroked = false;
        if (!s.asleep) {
            watch_finger(indev);
        }
        break;
    case LV_EVENT_PRESSING:
        if (!s.asleep) {
            watch_finger(indev);
        }
        break;
    case LV_EVENT_RELEASED:
        s.pressed = false;
        if (s.voice == VOICE_IDLE) {
            eyes_look_around();
        } else {
            show_voice(s.voice); // back to looking at you
        }
        break;
    case LV_EVENT_SHORT_CLICKED:
        // Waking and sleeping work with reactions off too: they're the controls.
        if (s.asleep) {
            wake_up();
        } else if (!s.stroked && s.voice == VOICE_IDLE && board_settings_on(SETTING_TOUCH_REACTIONS)) {
            poke(now);
        }
        break;
    case LV_EVENT_LONG_PRESSED:
        if (s.voice != VOICE_IDLE) {
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

/* How each voice state looks. */
static void show_voice(voice_state_t state)
{
    switch (state) {
    case VOICE_LISTENING:
        eyes_set_mood(EYES_MOOD_LISTENING);
        eyes_look_at(0, -0.15f); // at you
        break;
    case VOICE_THINKING:
        eyes_set_mood(EYES_MOOD_THINKING);
        eyes_look_around();
        break;
    case VOICE_SPEAKING:
        eyes_set_mood(EYES_MOOD_SPEAKING);
        eyes_look_at(0, 0);
        break;
    case VOICE_ERROR:
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
        // Back to its own life; the next tick sets the resting mood.
        s.until_ms = 0;
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
}
