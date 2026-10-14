#include "behaviour.h"

#include <stdlib.h>

#include "board.h"
#include "board_display.h"
#include "esp_log.h"
#include "esp_random.h"
#include "eyes.h"

#define TICK_MS 250
#define SLEEPY_AFTER_MS (45 * 1000)
#define ASLEEP_AFTER_MS (120 * 1000)
#define BRIGHTNESS_AWAKE 80
#define BRIGHTNESS_ASLEEP 12
/* Three pokes closer than this make it grumpy. */
#define POKE_WINDOW_MS 1500
/* How much finger travel counts as stroking. */
#define STROKE_PX 160

static const char *TAG = "behaviour";

static struct {
    uint32_t last_touch_ms;
    /* A short reaction over the resting mood, until `until_ms`. */
    eyes_mood_t reaction;
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

static void react(eyes_mood_t mood, uint32_t for_ms)
{
    s.reaction = mood;
    s.until_ms = lv_tick_get() + for_ms;
    eyes_set_mood(mood);
}

static void fall_asleep(void)
{
    if (!s.asleep) {
        ESP_LOGI(TAG, "asleep");
    }
    s.asleep = true;
    s.until_ms = 0;
    eyes_set_mood(EYES_MOOD_ASLEEP);
    set_brightness(BRIGHTNESS_ASLEEP);
}

static void wake_up(void)
{
    ESP_LOGI(TAG, "awake");
    s.asleep = false;
    set_brightness(BRIGHTNESS_AWAKE);
    eyes_blink();
    react(EYES_MOOD_CURIOUS, 1500);
}

static void schedule_whim(uint32_t now)
{
    s.next_whim_ms = now + 15000 + esp_random() % 25000;
}

/* Where the finger is, as a gaze target. */
static void watch_finger(lv_indev_t *indev)
{
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    eyes_look_at((float) (p.x - BOARD_LCD_H_RES / 2) / 180.0f, (float) (p.y - BOARD_LCD_V_RES / 2) / 180.0f);
    s.stroke += abs(p.x - s.last_point.x) + abs(p.y - s.last_point.y);
    s.last_point = p;
    if (!s.stroked && !s.asleep && s.stroke > STROKE_PX) {
        s.stroked = true;
        react(EYES_MOOD_HAPPY, 2500);
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
        if (s.asleep) {
            wake_up();
            break;
        }
        if (s.stroked) {
            break;
        }
        s.pokes = (now - s.last_poke_ms < POKE_WINDOW_MS) ? s.pokes + 1 : 1;
        s.last_poke_ms = now;
        eyes_poke();
        react(s.pokes >= 3 ? EYES_MOOD_GRUMPY : EYES_MOOD_HAPPY, s.pokes >= 3 ? 2500 : 1800);
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

    if (s.asleep) {
        return;
    }
    if (idle >= ASLEEP_AFTER_MS) {
        fall_asleep();
        return;
    }
    if (s.until_ms != 0 && (int32_t) (now - s.until_ms) < 0) {
        return; // a reaction is still playing
    }
    s.until_ms = 0;

    if (idle >= SLEEPY_AFTER_MS) {
        eyes_set_mood(EYES_MOOD_SLEEPY);
        return;
    }
    eyes_set_mood(EYES_MOOD_NEUTRAL);

    // Little whims, so it never looks frozen.
    if ((int32_t) (now - s.next_whim_ms) >= 0) {
        schedule_whim(now);
        switch (esp_random() % 3) {
        case 0:
            react(EYES_MOOD_CURIOUS, 2500);
            break;
        case 1:
            react(EYES_MOOD_THINKING, 3000);
            break;
        default:
            eyes_blink();
            break;
        }
    }
}

void behaviour_start(lv_obj_t *face)
{
    const uint32_t now = lv_tick_get();
    s.last_touch_ms = now;
    s.brightness = -1;
    schedule_whim(now);
    set_brightness(BRIGHTNESS_AWAKE);

    lv_obj_add_event_cb(face, touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(face, touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(face, touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(face, touch_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(face, touch_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_timer_create(tick_cb, TICK_MS, NULL);
}
