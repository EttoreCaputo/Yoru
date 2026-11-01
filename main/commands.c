#include "commands.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "behaviour.h"
#include "board_settings.h"
#include "card.h"
#include "clock.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "habits.h"
#include "lvgl_port.h"
#include "menu.h"
#include "playback.h"
#include "timers.h"
#include "toast.h"
#include "voice.h"

static const char *TAG = "commands";

enum {
    CMD_POMODORO = 1,
    CMD_STOP,
    CMD_PAUSE,
    CMD_RESUME,
    CMD_LEFT,
    CMD_TIME,
    CMD_SLEEP,
    CMD_PLAY,
    CMD_TIMERS,
    CMD_HABITS,
    CMD_INBOX,
    CMD_LOUDER,
    CMD_QUIETER,
    CMD_HELLO,
    /* + minutes */
    CMD_TIMER = 100,
    /* + the habit's index */
    CMD_HABIT = 400,
};

static const struct {
    int id;
    const char *text;
} FIXED[] = {
    {CMD_POMODORO, "start a pomodoro"},
    {CMD_POMODORO, "start pomodoro"},
    {CMD_POMODORO, "focus time"},
    {CMD_STOP, "stop the timer"},
    {CMD_STOP, "cancel the timer"},
    {CMD_STOP, "stop the alarm"},
    {CMD_STOP, "stop"},
    {CMD_PAUSE, "pause the timer"},
    {CMD_RESUME, "resume the timer"},
    {CMD_RESUME, "continue the timer"},
    {CMD_LEFT, "how much time is left"},
    {CMD_LEFT, "how long is left"},
    {CMD_TIME, "what time is it"},
    {CMD_TIME, "what is the time"},
    {CMD_SLEEP, "go to sleep"},
    {CMD_SLEEP, "good night"},
    {CMD_PLAY, "lets play"},
    {CMD_PLAY, "play a game"},
    {CMD_TIMERS, "show my timers"},
    {CMD_TIMERS, "open timers"},
    {CMD_HABITS, "show my habits"},
    {CMD_HABITS, "open habits"},
    {CMD_INBOX, "show my notifications"},
    {CMD_INBOX, "open notifications"},
    {CMD_LOUDER, "volume up"},
    {CMD_LOUDER, "louder"},
    {CMD_QUIETER, "volume down"},
    {CMD_QUIETER, "quieter"},
    {CMD_HELLO, "hello there"},
    {CMD_HELLO, "how are you"},
};

/* The lengths a timer can be asked for: as a span ("set a timer for five
 * minutes") and before "timer" ("a five minute timer"). */
static const struct {
    int min;
    const char *span, *before;
} SPANS[] = {
    {1, "one minute", "one minute"},
    {2, "two minutes", "two minute"},
    {3, "three minutes", "three minute"},
    {4, "four minutes", "four minute"},
    {5, "five minutes", "five minute"},
    {6, "six minutes", "six minute"},
    {7, "seven minutes", "seven minute"},
    {8, "eight minutes", "eight minute"},
    {9, "nine minutes", "nine minute"},
    {10, "ten minutes", "ten minute"},
    {12, "twelve minutes", "twelve minute"},
    {15, "fifteen minutes", "fifteen minute"},
    {20, "twenty minutes", "twenty minute"},
    {25, "twenty five minutes", "twenty five minute"},
    {30, "thirty minutes", "thirty minute"},
    {40, "forty minutes", "forty minute"},
    {45, "forty five minutes", "forty five minute"},
    {60, "one hour", "one hour"},
    {90, "an hour and a half", NULL},
    {120, "two hours", "two hour"},
};

#define FIXED_N (sizeof(FIXED) / sizeof(FIXED[0]))
#define SPANS_N (sizeof(SPANS) / sizeof(SPANS[0]))
#define PHRASES_MAX (FIXED_N + 3 * SPANS_N + 2 * HABITS_MAX)

EXT_RAM_BSS_ATTR static voice_phrase_t s_list[PHRASES_MAX];
/* The habits' names the phrases were made from. */
static uint32_t s_names = UINT32_MAX;

/* FNV-1a of the habits' names, to notice when they change. */
static uint32_t names_hash(void)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < habits_count(); i++) {
        for (const char *p = habits_at(i)->name; *p != '\0'; p++) {
            h = (h ^ (uint8_t) *p) * 16777619u;
        }
        h = (h ^ '|') * 16777619u;
    }
    return h;
}

static int add(int n, int id, const char *text)
{
    if (n < (int) PHRASES_MAX && text[0] != '\0') {
        s_list[n].id = id;
        snprintf(s_list[n].text, sizeof(s_list[n].text), "%s", text);
        n++;
    }
    return n;
}

/* A habit's name as MultiNet can take it: lower case letters and single
 * spaces; empty if too short to tell apart. */
static void spoken_name(const char *name, char *out, size_t len)
{
    size_t n = 0;
    for (const char *p = name; *p != '\0' && n + 1 < len; p++) {
        if (isalpha((unsigned char) *p)) {
            out[n++] = (char) tolower((unsigned char) *p);
        } else if (n > 0 && out[n - 1] != ' ') {
            out[n++] = ' ';
        }
    }
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    out[n] = '\0';
    if (n < 3) {
        out[0] = '\0';
    }
}

static void handle(int id);

static void send_phrases(void)
{
    int n = 0;
    for (size_t i = 0; i < FIXED_N; i++) {
        n = add(n, FIXED[i].id, FIXED[i].text);
    }
    char text[VOICE_PHRASE_MAX + 1];
    for (size_t i = 0; i < SPANS_N; i++) {
        snprintf(text, sizeof(text), "set a timer for %s", SPANS[i].span);
        n = add(n, CMD_TIMER + SPANS[i].min, text);
        snprintf(text, sizeof(text), "timer for %s", SPANS[i].span);
        n = add(n, CMD_TIMER + SPANS[i].min, text);
        if (SPANS[i].before != NULL) {
            snprintf(text, sizeof(text), "%s timer", SPANS[i].before);
            n = add(n, CMD_TIMER + SPANS[i].min, text);
        }
    }
    for (int i = 0; i < habits_count(); i++) {
        char name[HABITS_NAME_CHARS + 1];
        spoken_name(habits_at(i)->name, name, sizeof(name));
        if (name[0] != '\0') {
            snprintf(text, sizeof(text), "check %s", name);
            n = add(n, CMD_HABIT + i, text);
            snprintf(text, sizeof(text), "%s done", name);
            n = add(n, CMD_HABIT + i, text);
        }
    }
    s_names = names_hash();
    voice_set_commands(s_list, n, handle);
}

void commands_start(void)
{
    if (!voice_commands_available()) {
        return;
    }
    lvgl_port_lock();
    send_phrases();
    lvgl_port_unlock();
}

void commands_poll(void)
{
    // MultiNet takes a moment over a new list: only when the names change.
    static uint32_t version = UINT32_MAX;
    if (!voice_commands_available() || habits_version() == version) {
        return;
    }
    version = habits_version();
    if (names_hash() != s_names) {
        send_phrases();
    }
}

/* ---- What each one does ---------------------------------------------------------- */

static void say(const char *text, toast_tone_t tone)
{
    toast_show(text, 4, tone);
}

/* The first timer, the ringing ones and the one that ends first first,
 * that is in `state` (or any, for -1). */
static bool first_timer(int state, timer_view_t *out)
{
    for (int i = 0; i < timers_count(); i++) {
        if (timers_at(i, out) && (state < 0 || out->state == (timer_state_t) state)) {
            return true;
        }
    }
    return false;
}

static void new_timer(int min)
{
    char text[40];
    if (!timers_start(min * 60, NULL)) {
        say(card_tr("No room for more timers", "Non c'e' posto per altri timer"), TOAST_ERROR);
        return;
    }
    if (min % 60 == 0) {
        snprintf(text, sizeof(text), "Timer %d h", min / 60);
    } else {
        snprintf(text, sizeof(text), "Timer %d min", min);
    }
    say(text, TOAST_DONE);
    behaviour_play("happy");
}

static void time_left(void)
{
    timer_view_t t;
    if (!first_timer(TIMER_RUN, &t) && !first_timer(TIMER_PAUSE, &t)) {
        say(card_tr("No timers", "Nessun timer"), TOAST_INFO);
        return;
    }
    const int left = (int) t.left_s;
    char text[40];
    snprintf(text, sizeof(text), card_tr("%d:%02d left", "Mancano %d:%02d"), left / 60, left % 60);
    say(text, TOAST_INFO);
}

static void tell_time(void)
{
    const int m = clock_minute();
    if (m < 0) {
        say(card_tr("I don't know the time yet", "Non so ancora che ore sono"), TOAST_INFO);
        return;
    }
    char text[24];
    snprintf(text, sizeof(text), card_tr("It's %02d:%02d", "Sono le %02d:%02d"), m / 60, m % 60);
    say(text, TOAST_INFO);
    behaviour_play("wink");
}

static void stop_timer(void)
{
    timer_view_t t;
    if (first_timer(TIMER_RING, &t)) {
        timers_act(t.id, "dismiss");
        playback_sound(SOUND_PHEW);
    } else if (first_timer(-1, &t)) {
        timers_act(t.id, "cancel");
        say(card_tr("Timer cancelled", "Timer annullato"), TOAST_DONE);
        playback_sound(SOUND_PFFT);
    } else {
        say(card_tr("No timers", "Nessun timer"), TOAST_INFO);
    }
}

static void pause_timer(bool pause)
{
    timer_view_t t;
    for (int i = 0; i < timers_count(); i++) {
        if (timers_at(i, &t) && t.kind != TIMER_KIND_REMINDER && t.state == (pause ? TIMER_RUN : TIMER_PAUSE)) {
            timers_act(t.id, pause ? "pause" : "resume");
            say(pause ? card_tr("Timer paused", "Timer in pausa") : card_tr("Timer resumed", "Timer ripartito"), TOAST_DONE);
            return;
        }
    }
    say(card_tr("No timers", "Nessun timer"), TOAST_INFO);
}

static void volume(int by)
{
    int v = (int) board_settings_get(SETTING_VOLUME) + by;
    v = v < 0 ? 0 : v > 100 ? 100 : v;
    board_settings_set(SETTING_VOLUME, v);
    playback_sound(SOUND_VOLUME);
    char text[24];
    snprintf(text, sizeof(text), "Volume %d%%", v);
    say(text, TOAST_INFO);
}

static void tick_habit(int i)
{
    const habit_t *h = habits_at(i);
    if (h == NULL) {
        return;
    }
    char text[48];
    const int done = habits_tick(i, 1);
    if (done < 0) {
        snprintf(text, sizeof(text), card_tr("%s: done already", "%s: gia' fatto"), h->name);
        say(text, TOAST_INFO);
        return;
    }
    snprintf(text, sizeof(text), "%s %d/%d", h->name, done, h->goal);
    say(text, TOAST_DONE);
    behaviour_play(done >= h->goal ? "proud" : "happy");
}

static void open_page(const char *name)
{
    if (!menu_open_page(name)) {
        ESP_LOGW(TAG, "no page %s", name);
    }
}

static void handle(int id)
{
    lvgl_port_lock();
    ESP_LOGI(TAG, "command %d", id);
    if (id >= CMD_HABIT) {
        tick_habit(id - CMD_HABIT);
    } else if (id >= CMD_TIMER) {
        new_timer(id - CMD_TIMER);
    } else {
        switch (id) {
        case CMD_POMODORO:
            if (timers_start_pomodoro()) {
                say(card_tr("Pomodoro: 25 min of focus", "Pomodoro: 25 min di concentrazione"), TOAST_DONE);
                behaviour_play("focus");
            } else {
                say(card_tr("No room for more timers", "Non c'e' posto per altri timer"), TOAST_ERROR);
            }
            break;
        case CMD_STOP:
            stop_timer();
            break;
        case CMD_PAUSE:
        case CMD_RESUME:
            pause_timer(id == CMD_PAUSE);
            break;
        case CMD_LEFT:
            time_left();
            break;
        case CMD_TIME:
            tell_time();
            break;
        case CMD_SLEEP:
            behaviour_sleep();
            break;
        case CMD_PLAY:
            open_page("games");
            break;
        case CMD_TIMERS:
            open_page("timers");
            break;
        case CMD_HABITS:
            open_page("habits");
            break;
        case CMD_INBOX:
            open_page("inbox");
            break;
        case CMD_LOUDER:
        case CMD_QUIETER:
            volume(id == CMD_LOUDER ? 15 : -15);
            break;
        case CMD_HELLO:
            behaviour_play("wave");
            break;
        default:
            // Not one it knows: a puzzled "hm?", and a hint.
            behaviour_play("curious");
            say(card_tr("Without the Mac I know only a few words", "Senza il Mac capisco solo poche parole"), TOAST_INFO);
            break;
        }
    }
    lvgl_port_unlock();
}
