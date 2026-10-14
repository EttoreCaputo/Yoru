#include "behaviour.h"

#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "board_display.h"
#include "board_settings.h"
#include "esp_log.h"
#include "esp_random.h"
#include "eyes.h"
#include "pet.h"

#define TICK_MS 250
#define BRIGHTNESS_ASLEEP 12
/* Three pokes closer than this make it grumpy. */
#define POKE_WINDOW_MS 1500
/* How much finger travel counts as stroking. */
#define STROKE_PX 160
/* A whim every 15-40 s, times pet_idle_pace(). */
#define WHIM_MIN_MS 15000
#define WHIM_SPAN_MS 25000

static const char *TAG = "behaviour";

/* How each of pet.c's scenes looks with these eyes: a mood for a while, and
 * a poke or a blink to start it. Names as in DualEye, so the host's scenes
 * and the pet's reactions mean the same on both boards. */
typedef enum { FX_NONE, FX_POKE, FX_BLINK } fx_t;

typedef struct {
    const char *name;
    eyes_mood_t mood;
    uint16_t ms;
    fx_t fx;
} scene_t;

static const scene_t SCENES[] = {
    {"happy", EYES_MOOD_HAPPY, 2000, FX_NONE},
    {"excited", EYES_MOOD_HAPPY, 2500, FX_POKE},
    {"wink", EYES_MOOD_HAPPY, 1200, FX_BLINK},
    {"proud", EYES_MOOD_HAPPY, 2500, FX_NONE},
    {"relieved", EYES_MOOD_HAPPY, 1800, FX_BLINK},
    {"charged", EYES_MOOD_HAPPY, 1500, FX_POKE},
    // Bobbing to the beat comes with the music, in M5.
    {"dance", EYES_MOOD_HAPPY, 3000, FX_POKE},
    {"love", EYES_MOOD_LOVING, 3500, FX_NONE},
    {"curious", EYES_MOOD_CURIOUS, 2500, FX_NONE},
    {"thinking", EYES_MOOD_THINKING, 3000, FX_NONE},
    {"focus", EYES_MOOD_THINKING, 3000, FX_BLINK},
    {"angry", EYES_MOOD_GRUMPY, 2500, FX_POKE},
    {"hot", EYES_MOOD_HOT, 4000, FX_NONE},
    {"sleepy", EYES_MOOD_SLEEPY, 2500, FX_NONE},
    {"yawn", EYES_MOOD_SLEEPY, 2000, FX_POKE},
    {"tired", EYES_MOOD_SLEEPY, 2500, FX_BLINK},
    // A sigh: squash, then a slow, heavy-lidded moment.
    {"sigh", EYES_MOOD_SLEEPY, 1200, FX_POKE},
    {"sad", EYES_MOOD_SAD, 3500, FX_NONE},
    {"surprised", EYES_MOOD_SURPRISED, 1500, FX_POKE},
    {"scared", EYES_MOOD_SURPRISED, 2000, FX_POKE},
    {"bored", EYES_MOOD_BORED, 3000, FX_NONE},
    {"look_around", EYES_MOOD_NEUTRAL, 0, FX_NONE},
    {"blink", EYES_MOOD_NEUTRAL, 0, FX_BLINK},
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
} s;

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
    for (size_t i = 0; i < sizeof(SCENES) / sizeof(SCENES[0]); i++) {
        const scene_t *sc = &SCENES[i];
        if (strcmp(name, sc->name) != 0) {
            continue;
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
    if (!s.stroked && !s.asleep && s.stroke > STROKE_PX && board_settings_on(SETTING_TOUCH_REACTIONS)) {
        s.stroked = true;
        pet_event(PET_EVENT_STROKED);
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
        react(EYES_MOOD_GRUMPY, 2500);
    } else {
        pet_event(PET_EVENT_POKED);
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
        eyes_look_around();
        break;
    case LV_EVENT_SHORT_CLICKED:
        // Waking and sleeping work with reactions off too: they're the controls.
        if (s.asleep) {
            wake_up();
        } else if (!s.stroked && board_settings_on(SETTING_TOUCH_REACTIONS)) {
            poke(now);
        }
        break;
    case LV_EVENT_LONG_PRESSED:
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
