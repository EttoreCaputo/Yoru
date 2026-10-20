#include "board_tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agents.h"
#include "agents_ui.h"
#include "approval.h"
#include "behaviour.h"
#include "board_pmu.h"
#include "board_settings.h"
#include "card.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "errand.h"
#include "eyes.h"
#include "habits.h"
#include "inbox.h"
#include "inbox_ui.h"
#include "life.h"
#include "link.h"
#include "lvgl_port.h"
#include "menu.h"
#include "metrics_model.h"
#include "motion.h"
#include "net.h"
#include "playback.h"
#include "question.h"
#include "draft.h"
#include "remote.h"
#include "timers.h"
#include "voice.h"

/* From DualEye's board_tools.c: one screen, no faces, and the settings as
 * the table board_settings.c keeps. */

#define TEXT_MAX 160

/** A tool fills `text` with what it did, or why it refused (returning false). */
typedef bool (*tool_fn_t)(const cJSON *args, char *text, cJSON **structured);

/** A schema made at the time, for one that lists the board's own names. */
typedef cJSON *(*schema_fn_t)(void);

typedef struct {
    const char *name;
    const char *description;
    const char *schema;
    tool_fn_t fn;
    /* Instead of `schema`. */
    schema_fn_t schema_fn;
} tool_t;

static bool get_int(const cJSON *args, const char *key, int *out, char *text)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(args, key);
    if (!cJSON_IsNumber(v) || v->valuedouble != (double) (int) v->valuedouble) {
        snprintf(text, TEXT_MAX, "%s must be an integer", key);
        return false;
    }
    *out = (int) v->valuedouble;
    return true;
}

static bool tool_set_brightness(const cJSON *args, char *text, cJSON **structured)
{
    int pct = 0;
    if (!get_int(args, "percent", &pct, text)) {
        return false;
    }
    if (board_settings_set(SETTING_BRIGHTNESS, pct) != ESP_OK) {
        int32_t min, max;
        board_settings_range(SETTING_BRIGHTNESS, &min, &max);
        snprintf(text, TEXT_MAX, "percent must be %d to %d", (int) min, (int) max);
        return false;
    }
    snprintf(text, TEXT_MAX, "brightness %d%%", pct);
    return true;
}

static bool tool_set_mic(const cJSON *args, char *text, cJSON **structured)
{
    const cJSON *muted = cJSON_GetObjectItemCaseSensitive(args, "muted");
    const cJSON *sound = cJSON_GetObjectItemCaseSensitive(args, "wake_sound");
    if ((muted != NULL && !cJSON_IsBool(muted)) || (sound != NULL && !cJSON_IsBool(sound))
        || (muted == NULL && sound == NULL)) {
        snprintf(text, TEXT_MAX, "give muted and/or wake_sound, true or false");
        return false;
    }
    if (!voice_available()) {
        snprintf(text, TEXT_MAX, "voice is not available on this board");
        return false;
    }
    int n = 0;
    if (muted != NULL) {
        bool on = cJSON_IsTrue(muted);
        board_settings_set(SETTING_MIC_MUTED, on);
        playback_sound(on ? SOUND_MUTE : SOUND_UNMUTE);
        n += snprintf(text, TEXT_MAX, on ? "microphone muted: not listening for \"%s\"" : "listening for \"%s\"",
                      voice_wake_word());
    }
    if (sound != NULL) {
        bool on = cJSON_IsTrue(sound);
        board_settings_set(SETTING_WAKE_SOUND, on);
        snprintf(text + n, TEXT_MAX - n, "%s%s", n ? "; " : "",
                 on ? "a chime when the wake word is heard" : "no chime on the wake word");
    }
    return true;
}

static bool tool_set_wake_word(const cJSON *args, char *text, cJSON **structured)
{
    const cJSON *word = cJSON_GetObjectItemCaseSensitive(args, "word");
    if (!cJSON_IsString(word)) {
        snprintf(text, TEXT_MAX, "word must be a string");
        return false;
    }
    if (!voice_available()) {
        snprintf(text, TEXT_MAX, "voice is not available on this board");
        return false;
    }
    esp_err_t err = voice_set_wake_word(word->valuestring);
    if (err == ESP_ERR_NOT_FOUND) {
        snprintf(text, TEXT_MAX, "no model for \"%s\" on this board", word->valuestring);
        return false;
    }
    if (err != ESP_OK) {
        snprintf(text, TEXT_MAX, "switching failed, still listening for \"%s\"", voice_wake_word());
        return false;
    }
    board_settings_set_wake_word(voice_wake_word_id());
    snprintf(text, TEXT_MAX, "wake word is now \"%s\"", voice_wake_word());
    return true;
}

static cJSON *wake_word_schema(void)
{
    const char *ids[8];
    int n = voice_wake_words(ids, 8);
    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON *word = cJSON_AddObjectToObject(cJSON_AddObjectToObject(schema, "properties"), "word");
    cJSON_AddStringToObject(word, "type", "string");
    cJSON_AddItemToObject(word, "enum", cJSON_CreateStringArray(ids, n));
    cJSON_AddItemToArray(cJSON_AddArrayToObject(schema, "required"), cJSON_CreateString("word"));
    return schema;
}

static bool tool_set_volume(const cJSON *args, char *text, cJSON **structured)
{
    int pct = 0;
    if (!get_int(args, "percent", &pct, text)) {
        return false;
    }
    if (pct < 0 || pct > 100) {
        snprintf(text, TEXT_MAX, "percent must be 0 to 100");
        return false;
    }
    if (!playback_available() || board_settings_set(SETTING_VOLUME, pct) != ESP_OK) {
        snprintf(text, TEXT_MAX, "the speaker is not available on this board");
        return false;
    }
    // How loud it is now.
    playback_sound(SOUND_VOLUME);
    snprintf(text, TEXT_MAX, "speaker volume %d%%", pct);
    return true;
}

static bool tool_react(const cJSON *args, char *text, cJSON **structured)
{
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(args, "name");
    if (!cJSON_IsString(name)) {
        snprintf(text, TEXT_MAX, "name must be a string");
        return false;
    }
    if (voice_state() != VOICE_IDLE) {
        snprintf(text, TEXT_MAX, "not now: a conversation is on");
        return false;
    }
    lvgl_port_lock();
    bool ok = behaviour_play(name->valuestring);
    lvgl_port_unlock();
    if (!ok) {
        snprintf(text, TEXT_MAX, "unknown reaction \"%s\"", name->valuestring);
        return false;
    }
    snprintf(text, TEXT_MAX, "reacting: %s", name->valuestring);
    return true;
}

static cJSON *react_schema(void)
{
    const char *names[40];
    int n = behaviour_scenes(names, 40);
    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON *name = cJSON_AddObjectToObject(cJSON_AddObjectToObject(schema, "properties"), "name");
    cJSON_AddStringToObject(name, "type", "string");
    cJSON_AddItemToObject(name, "enum", cJSON_CreateStringArray(names, n));
    cJSON_AddItemToArray(cJSON_AddArrayToObject(schema, "required"), cJSON_CreateString("name"));
    return schema;
}

static cJSON *play_sound_schema(void)
{
    const char *names[SOUND_COUNT];
    for (int i = 0; i < SOUND_COUNT; i++) {
        names[i] = sound_name((sound_t) i);
    }
    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON *name = cJSON_AddObjectToObject(cJSON_AddObjectToObject(schema, "properties"), "name");
    cJSON_AddStringToObject(name, "type", "string");
    cJSON_AddItemToObject(name, "enum", cJSON_CreateStringArray(names, SOUND_COUNT));
    cJSON_AddItemToArray(cJSON_AddArrayToObject(schema, "required"), cJSON_CreateString("name"));
    return schema;
}

static bool tool_play_sound(const cJSON *args, char *text, cJSON **structured)
{
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(args, "name");
    if (!cJSON_IsString(name)) {
        snprintf(text, TEXT_MAX, "name must be a string");
        return false;
    }
    sound_t sound = sound_find(name->valuestring);
    if (sound == SOUND_COUNT) {
        snprintf(text, TEXT_MAX, "unknown sound \"%s\"", name->valuestring);
        return false;
    }
    if (!playback_available()) {
        snprintf(text, TEXT_MAX, "the speaker is not available on this board");
        return false;
    }
    if (sound >= SOUND_FIRST_PET && !playback_pet_sounds()) {
        snprintf(text, TEXT_MAX, "pet sounds are off");
        return false;
    }
    playback_sound(sound);
    snprintf(text, TEXT_MAX, "playing %s", name->valuestring);
    return true;
}

static double round2(float v)
{
    return (int) (v * 100.0f + (v < 0 ? -0.5f : 0.5f)) / 100.0;
}

static bool tool_get_settings(const cJSON *args, char *text, cJSON **structured)
{
    cJSON *all = cJSON_CreateObject();
    for (int i = 0; i < SETTING_COUNT; i++) {
        int32_t min, max;
        board_settings_range((setting_t) i, &min, &max);
        cJSON *s = cJSON_AddObjectToObject(all, board_settings_name((setting_t) i));
        cJSON_AddNumberToObject(s, "value", board_settings_get((setting_t) i));
        cJSON_AddNumberToObject(s, "min", min);
        cJSON_AddNumberToObject(s, "max", max);
    }
    char word[16];
    board_settings_wake_word(word, sizeof(word));
    cJSON_AddStringToObject(all, "wake_word", word);
    *structured = all;
    text[0] = '\0';
    return true;
}

static bool tool_set_setting(const cJSON *args, char *text, cJSON **structured)
{
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(args, "name");
    setting_t id;
    if (!cJSON_IsString(name) || !board_settings_from_name(name->valuestring, &id)) {
        snprintf(text, TEXT_MAX, "name must be one of get_settings' names");
        return false;
    }
    int value = 0;
    if (!get_int(args, "value", &value, text)) {
        return false;
    }
    esp_err_t err = board_settings_set(id, value);
    if (err == ESP_ERR_INVALID_ARG) {
        int32_t min, max;
        board_settings_range(id, &min, &max);
        snprintf(text, TEXT_MAX, "%s must be %d to %d", name->valuestring, (int) min, (int) max);
        return false;
    }
    if (err != ESP_OK) {
        snprintf(text, TEXT_MAX, "%s could not be applied: %s", name->valuestring, esp_err_to_name(err));
        return false;
    }
    snprintf(text, TEXT_MAX, "%s = %d", name->valuestring, value);
    return true;
}

static const char *metrics_state_name(metrics_ui_state_t state)
{
    switch (state) {
    case METRICS_UI_LIVE:
        return "live";
    case METRICS_UI_STALE:
        return "stale";
    case METRICS_UI_ERROR:
        return "error";
    default:
        return "waiting";
    }
}

static bool tool_get_state(const cJSON *args, char *text, cJSON **structured)
{
    metrics_snapshot_t snap;
    metrics_model_get(&snap);
    link_stats_t link;
    link_get_stats(&link);

    cJSON *st = cJSON_CreateObject();
    cJSON_AddStringToObject(st, "firmware", esp_app_get_description()->version);
    cJSON_AddNumberToObject(st, "uptime_s", (double) (esp_timer_get_time() / 1000000));
    cJSON_AddStringToObject(st, "metrics", metrics_state_name(snap.state));
    lvgl_port_lock();
    const char *eyes = eyes_mood_name(eyes_get_mood());
    lvgl_port_unlock();
    cJSON_AddStringToObject(st, "eyes", eyes);
    cJSON_AddNumberToObject(st, "brightness", board_settings_get(SETTING_BRIGHTNESS));
    cJSON *voice = cJSON_AddObjectToObject(st, "voice");
    cJSON_AddBoolToObject(voice, "available", voice_available());
    cJSON_AddBoolToObject(voice, "wake_sound", board_settings_on(SETTING_WAKE_SOUND));
    if (voice_available()) {
        cJSON_AddStringToObject(voice, "wake_word", voice_wake_word());
        cJSON_AddStringToObject(voice, "wake_word_id", voice_wake_word_id());
        cJSON_AddStringToObject(voice, "model", voice_wake_model());
        const char *ids[8];
        int n = voice_wake_words(ids, 8);
        cJSON_AddItemToObject(voice, "wake_words", cJSON_CreateStringArray(ids, n));
        cJSON_AddBoolToObject(voice, "muted", voice_muted());
        cJSON_AddStringToObject(voice, "state", voice_state_name(voice_state()));
    }
    cJSON_AddStringToObject(st, "activity", life_activity());
    cJSON *audio = cJSON_AddObjectToObject(st, "audio");
    cJSON_AddBoolToObject(audio, "speaker", playback_available());
    cJSON_AddNumberToObject(audio, "volume", board_settings_get(SETTING_VOLUME));
    cJSON_AddBoolToObject(audio, "playing", playback_active());
    lvgl_port_stats_t ui;
    lvgl_port_get_stats(&ui);
    cJSON *u = cJSON_AddObjectToObject(st, "ui");
    cJSON_AddNumberToObject(u, "busy_pct", (int) (ui.busy_pct * 10.0f + 0.5f) / 10.0);
    cJSON_AddNumberToObject(u, "max_frame_ms", (int) (ui.max_us / 100) / 10.0);
    cJSON *mem = cJSON_AddObjectToObject(st, "memory");
    cJSON_AddNumberToObject(mem, "internal_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(mem, "internal_min", heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(mem, "psram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON *l = cJSON_AddObjectToObject(st, "link");
    cJSON_AddNumberToObject(l, "rx_frames", link.rx_frames);
    cJSON_AddNumberToObject(l, "rx_bad", link.rx_bad);
    cJSON_AddNumberToObject(l, "tx_frames", link.tx_frames);
    cJSON_AddNumberToObject(l, "tx_dropped", link.tx_dropped);
    cJSON_AddNumberToObject(l, "rx_ignored", link.rx_ignored);
    cJSON_AddStringToObject(l, "transport", link_transport_name(link_active()));
    cJSON_AddItemToObject(st, "wifi", net_status());
    board_pmu_state_t bat;
    board_pmu_get(&bat);
    cJSON *b = cJSON_AddObjectToObject(st, "battery");
    cJSON_AddBoolToObject(b, "present", bat.battery);
    cJSON_AddNumberToObject(b, "percent", bat.percent);
    cJSON_AddNumberToObject(b, "mv", bat.mv);
    cJSON_AddBoolToObject(b, "usb", bat.usb);
    cJSON_AddBoolToObject(b, "charging", bat.charging);
    if (motion_present()) {
        // Gravity and turn rate on the screen's axes, for checking them.
        float g[3], w[3], lx, ly;
        motion_raw(g, w);
        motion_lean(&lx, &ly);
        cJSON *mo = cJSON_AddObjectToObject(st, "motion");
        cJSON_AddItemToObject(mo, "g", cJSON_CreateFloatArray(g, 3));
        cJSON_AddItemToObject(mo, "dps", cJSON_CreateFloatArray(w, 3));
        cJSON_AddNumberToObject(mo, "lean_x", round2(lx));
        cJSON_AddNumberToObject(mo, "lean_y", round2(ly));
        cJSON_AddNumberToObject(mo, "upright", motion_upright());
    }
    agents_summary_t ag;
    agents_summary(&ag);
    cJSON *a = cJSON_AddObjectToObject(st, "agents");
    cJSON_AddNumberToObject(a, "sessions", ag.n);
    cJSON_AddNumberToObject(a, "working", ag.working);
    cJSON_AddNumberToObject(a, "waiting", ag.waiting);
    cJSON_AddNumberToObject(a, "quiet_min", ag.quiet_min);
    lvgl_port_lock();
    cJSON_AddBoolToObject(a, "view", agents_ui_is_open());
    cJSON_AddBoolToObject(a, "question", question_is_open());
    cJSON_AddBoolToObject(a, "approval", approval_is_open());
    cJSON *in = cJSON_AddObjectToObject(st, "inbox");
    cJSON_AddNumberToObject(in, "count", inbox_count());
    cJSON_AddNumberToObject(in, "unread", inbox_unread(NULL));
    cJSON_AddBoolToObject(in, "view", inbox_ui_is_open());
    cJSON_AddNumberToObject(st, "timers", timers_count());
    cJSON *hb = cJSON_AddObjectToObject(st, "habits");
    cJSON_AddNumberToObject(hb, "count", habits_count());
    cJSON_AddNumberToObject(hb, "done", habits_done());
    cJSON_AddBoolToObject(st, "errand", errand_busy());
    lvgl_port_unlock();
    *structured = st;
    // The text is filled in by the caller from `structured`.
    text[0] = '\0';
    return true;
}

static bool tool_show_menu(const cJSON *args, char *text, cJSON **structured)
{
    const cJSON *open = cJSON_GetObjectItemCaseSensitive(args, "open");
    const cJSON *page = cJSON_GetObjectItemCaseSensitive(args, "page");
    bool ok = true;
    lvgl_port_lock();
    if ((approval_is_open() || question_is_open()) && !cJSON_IsFalse(open)) {
        lvgl_port_unlock();
        snprintf(text, TEXT_MAX, "a permission request is on the screen");
        return false;
    }
    if (cJSON_IsFalse(open)) {
        menu_close();
    } else if (cJSON_IsString(page)) {
        ok = menu_open_page(page->valuestring);
    } else {
        menu_open();
    }
    lvgl_port_unlock();
    snprintf(text, TEXT_MAX, ok ? "done" : "no such page");
    return ok;
}

static bool tool_show_approval(const cJSON *args, char *text, cJSON **structured)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(args, "id");
    const cJSON *tool = cJSON_GetObjectItemCaseSensitive(args, "tool");
    const cJSON *what = cJSON_GetObjectItemCaseSensitive(args, "text");
    const cJSON *project = cJSON_GetObjectItemCaseSensitive(args, "project");
    int seconds = 0;
    if (!cJSON_IsString(id) || !cJSON_IsString(tool) || !cJSON_IsString(what)
        || (project != NULL && !cJSON_IsNull(project) && !cJSON_IsString(project))) {
        snprintf(text, TEXT_MAX, "id, tool and text must be strings, project a string if given");
        return false;
    }
    if (id->valuestring[0] == '\0' || strlen(id->valuestring) > APPROVAL_ID_MAX) {
        snprintf(text, TEXT_MAX, "id must be 1 to %d bytes", APPROVAL_ID_MAX);
        return false;
    }
    if (!get_int(args, "seconds", &seconds, text)) {
        return false;
    }
    if (seconds < 1) {
        snprintf(text, TEXT_MAX, "seconds must be 1 or more");
        return false;
    }
    // Hosts from before 0.7.0 send neither: no Always, the whole text.
    const cJSON *always = cJSON_GetObjectItemCaseSensitive(args, "always");
    const cJSON *full = cJSON_GetObjectItemCaseSensitive(args, "full");
    lvgl_port_lock();
    approval_show(id->valuestring, tool->valuestring, what->valuestring,
                  cJSON_IsString(project) ? project->valuestring : NULL, seconds, cJSON_IsTrue(always),
                  !cJSON_IsFalse(full));
    lvgl_port_unlock();
    snprintf(text, TEXT_MAX, "showing %s", id->valuestring);
    return true;
}

static bool tool_hide_approval(const cJSON *args, char *text, cJSON **structured)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(args, "id");
    if (!cJSON_IsString(id)) {
        snprintf(text, TEXT_MAX, "id must be a string");
        return false;
    }
    lvgl_port_lock();
    const bool shown = approval_hide(id->valuestring);
    lvgl_port_unlock();
    // Not shown is fine: answered here already, timed out, or replaced.
    snprintf(text, TEXT_MAX, shown ? "hidden" : "not shown");
    return true;
}

static bool tool_set_agents(const cJSON *args, char *text, cJSON **structured)
{
    if (!agents_set(args, text, TEXT_MAX)) {
        return false;
    }
    agents_summary_t sum;
    agents_summary(&sum);
    snprintf(text, TEXT_MAX, "%d session(s), %d waiting", sum.n, sum.waiting);
    return true;
}

static bool tool_show_toast(const cJSON *args, char *text, cJSON **structured)
{
    static const char *const TONES[] = {"info", "done", "wait", "error"};
    const cJSON *what = cJSON_GetObjectItemCaseSensitive(args, "text");
    const cJSON *tone = cJSON_GetObjectItemCaseSensitive(args, "tone");
    int seconds = 0;
    if (!cJSON_IsString(what) || what->valuestring[0] == '\0') {
        snprintf(text, TEXT_MAX, "text must be a string");
        return false;
    }
    if (!get_int(args, "seconds", &seconds, text)) {
        return false;
    }
    toast_tone_t t = TOAST_INFO;
    if (cJSON_IsString(tone)) {
        bool known = false;
        for (int i = 0; i < 4; i++) {
            if (strcmp(tone->valuestring, TONES[i]) == 0) {
                t = (toast_tone_t) i;
                known = true;
            }
        }
        if (!known) {
            snprintf(text, TEXT_MAX, "tone must be info, done, wait or error");
            return false;
        }
    }
    lvgl_port_lock();
    agents_ui_toast(what->valuestring, seconds, t);
    lvgl_port_unlock();
    snprintf(text, TEXT_MAX, "shown");
    return true;
}

/* One question of `show_question`, checked against the protocol's limits. */
static bool parse_question(const cJSON *j, question_t *q, char *text)
{
    const cJSON *question = cJSON_GetObjectItemCaseSensitive(j, "question");
    const cJSON *header = cJSON_GetObjectItemCaseSensitive(j, "header");
    const cJSON *options = cJSON_GetObjectItemCaseSensitive(j, "options");
    if (!cJSON_IsObject(j) || !cJSON_IsString(question) || !cJSON_IsArray(options)) {
        snprintf(text, TEXT_MAX, "each question needs question and options");
        return false;
    }
    q->n = cJSON_GetArraySize(options);
    if (q->n < 2 || q->n > QUESTION_OPTIONS_MAX) {
        snprintf(text, TEXT_MAX, "a question has 2 to %d options", QUESTION_OPTIONS_MAX);
        return false;
    }
    card_ascii(question->valuestring, q->text, QUESTION_TEXT_CHARS);
    card_ascii(cJSON_IsString(header) ? header->valuestring : "", q->header, QUESTION_HEADER_CHARS);
    q->multi = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "multi"));
    for (int i = 0; i < q->n; i++) {
        const cJSON *o = cJSON_GetArrayItem(options, i);
        if (!cJSON_IsString(o)) {
            snprintf(text, TEXT_MAX, "options must be strings");
            return false;
        }
        card_ascii(o->valuestring, q->options[i], QUESTION_OPTION_CHARS);
    }
    return true;
}

static bool tool_show_question(const cJSON *args, char *text, cJSON **structured)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(args, "id");
    const cJSON *project = cJSON_GetObjectItemCaseSensitive(args, "project");
    const cJSON *questions = cJSON_GetObjectItemCaseSensitive(args, "questions");
    int seconds = 0;
    if (!cJSON_IsString(id) || id->valuestring[0] == '\0' || strlen(id->valuestring) > QUESTION_ID_MAX) {
        snprintf(text, TEXT_MAX, "id must be a string of 1 to %d bytes", QUESTION_ID_MAX);
        return false;
    }
    if (!get_int(args, "seconds", &seconds, text)) {
        return false;
    }
    const int n = cJSON_IsArray(questions) ? cJSON_GetArraySize(questions) : 0;
    if (n < 1 || n > QUESTION_MAX) {
        snprintf(text, TEXT_MAX, "questions must be an array of 1 to %d", QUESTION_MAX);
        return false;
    }
    question_t *qs = heap_caps_calloc(QUESTION_MAX, sizeof(question_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (qs == NULL) {
        snprintf(text, TEXT_MAX, "out of memory");
        return false;
    }
    bool ok = true;
    for (int i = 0; i < n && ok; i++) {
        ok = parse_question(cJSON_GetArrayItem(questions, i), &qs[i], text);
    }
    if (ok) {
        lvgl_port_lock();
        question_show(id->valuestring, cJSON_IsString(project) ? project->valuestring : "", seconds < 1 ? 1 : seconds,
                      qs, n);
        lvgl_port_unlock();
        snprintf(text, TEXT_MAX, "showing %s", id->valuestring);
    }
    free(qs);
    return ok;
}

static bool tool_hide_question(const cJSON *args, char *text, cJSON **structured)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(args, "id");
    if (!cJSON_IsString(id)) {
        snprintf(text, TEXT_MAX, "id must be a string");
        return false;
    }
    lvgl_port_lock();
    const bool shown = question_hide(id->valuestring);
    lvgl_port_unlock();
    snprintf(text, TEXT_MAX, shown ? "hidden" : "not shown");
    return true;
}

static bool tool_show_draft(const cJSON *args, char *text, cJSON **structured)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(args, "id");
    const cJSON *words = cJSON_GetObjectItemCaseSensitive(args, "text");
    const cJSON *to = cJSON_GetObjectItemCaseSensitive(args, "to");
    int seconds = 0;
    if (!cJSON_IsString(id) || id->valuestring[0] == '\0' || strlen(id->valuestring) > DRAFT_ID_MAX) {
        snprintf(text, TEXT_MAX, "id must be a string of 1 to %d bytes", DRAFT_ID_MAX);
        return false;
    }
    if (!cJSON_IsString(words) || !cJSON_IsString(to)) {
        snprintf(text, TEXT_MAX, "text and to must be strings");
        return false;
    }
    if (!get_int(args, "seconds", &seconds, text)) {
        return false;
    }
    lvgl_port_lock();
    draft_show(id->valuestring, words->valuestring, to->valuestring, seconds);
    lvgl_port_unlock();
    snprintf(text, TEXT_MAX, "showing %s", id->valuestring);
    return true;
}

static bool tool_hide_draft(const cJSON *args, char *text, cJSON **structured)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(args, "id");
    if (!cJSON_IsString(id)) {
        snprintf(text, TEXT_MAX, "id must be a string");
        return false;
    }
    lvgl_port_lock();
    const bool shown = draft_hide(id->valuestring);
    lvgl_port_unlock();
    snprintf(text, TEXT_MAX, shown ? "hidden" : "not shown");
    return true;
}

static bool tool_set_chat(const cJSON *args, char *text, cJSON **structured)
{
    if (!remote_set_chat(args, text, TEXT_MAX)) {
        return false;
    }
    snprintf(text, TEXT_MAX, "chat set");
    return true;
}

static bool tool_set_projects(const cJSON *args, char *text, cJSON **structured)
{
    if (!remote_set_projects(args, text, TEXT_MAX)) {
        return false;
    }
    snprintf(text, TEXT_MAX, "projects set");
    return true;
}

/* A string argument within `max` bytes (`required`: present and not empty). */
static bool get_str(const cJSON *args, const char *key, bool required, size_t max, const char **out, char *text)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(args, key);
    if (v == NULL || cJSON_IsNull(v)) {
        *out = "";
        if (required) {
            snprintf(text, TEXT_MAX, "%s is required", key);
        }
        return !required;
    }
    if (!cJSON_IsString(v) || (required && v->valuestring[0] == '\0') || (max > 0 && strlen(v->valuestring) > max)) {
        snprintf(text, TEXT_MAX, "%s must be a string%s", key, required ? ", not empty" : "");
        if (max > 0) {
            snprintf(text + strlen(text), TEXT_MAX - strlen(text), ", at most %u bytes", (unsigned) max);
        }
        return false;
    }
    *out = v->valuestring;
    return true;
}

static bool tool_show_notification(const cJSON *args, char *text, cJSON **structured)
{
    const char *id, *source, *title, *words;
    if (!get_str(args, "id", true, INBOX_ID_MAX, &id, text) || !get_str(args, "source", false, 0, &source, text)
        || !get_str(args, "title", true, 0, &title, text) || !get_str(args, "text", false, 0, &words, text)) {
        return false;
    }
    inbox_urgency_t urgency = INBOX_NORMAL;
    const cJSON *u = cJSON_GetObjectItemCaseSensitive(args, "urgency");
    if (u != NULL && (!cJSON_IsString(u) || !inbox_urgency_from_name(u->valuestring, &urgency))) {
        snprintf(text, TEXT_MAX, "urgency must be low, normal, high or urgent");
        return false;
    }
    const cJSON *acts = cJSON_GetObjectItemCaseSensitive(args, "actions");
    const char *labels[INBOX_ACTIONS_MAX];
    int nacts = 0;
    if (acts != NULL && !cJSON_IsNull(acts)) {
        if (!cJSON_IsArray(acts) || cJSON_GetArraySize(acts) > INBOX_ACTIONS_MAX) {
            snprintf(text, TEXT_MAX, "actions must be an array of at most %d strings", INBOX_ACTIONS_MAX);
            return false;
        }
        const cJSON *a;
        cJSON_ArrayForEach(a, acts)
        {
            if (!cJSON_IsString(a) || a->valuestring[0] == '\0') {
                snprintf(text, TEXT_MAX, "each action must be a short label");
                return false;
            }
            labels[nacts++] = a->valuestring;
        }
    }
    lvgl_port_lock();
    const inbox_urgency_t loud = inbox_add(id, source, title, words, labels, nacts, urgency);
    inbox_ui_banner(loud);
    const int n = inbox_count(), unread = inbox_unread(NULL);
    lvgl_port_unlock();
    static const char *const LOUD[] = {"low", "normal", "high", "urgent"};
    snprintf(text, TEXT_MAX, "%s %s; %d in the inbox, %d unread", loud == INBOX_NONE ? "unchanged" : "shown",
             loud == INBOX_NONE ? "" : LOUD[loud], n, unread);
    return true;
}

static bool tool_hide_notification(const cJSON *args, char *text, cJSON **structured)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(args, "id");
    if (!cJSON_IsString(id)) {
        snprintf(text, TEXT_MAX, "id must be a string");
        return false;
    }
    lvgl_port_lock();
    const bool had = inbox_remove(id->valuestring);
    lvgl_port_unlock();
    // Not there is fine: dismissed here already, or pushed out.
    snprintf(text, TEXT_MAX, had ? "removed" : "not in the inbox");
    return true;
}

static bool tool_clear_notifications(const cJSON *args, char *text, cJSON **structured)
{
    lvgl_port_lock();
    inbox_clear();
    lvgl_port_unlock();
    snprintf(text, TEXT_MAX, "inbox empty");
    return true;
}

static bool tool_set_timers(const cJSON *args, char *text, cJSON **structured)
{
    lvgl_port_lock();
    const bool ok = timers_set(args, text, TEXT_MAX);
    const int n = timers_count();
    lvgl_port_unlock();
    if (ok) {
        snprintf(text, TEXT_MAX, "%d timers", n);
    }
    return ok;
}

static bool tool_set_habits(const cJSON *args, char *text, cJSON **structured)
{
    lvgl_port_lock();
    const bool ok = habits_set(args, text, TEXT_MAX);
    const int n = habits_count(), done = habits_done();
    lvgl_port_unlock();
    if (ok) {
        snprintf(text, TEXT_MAX, "%d habits, %d done", n, done);
    }
    return ok;
}

static bool tool_show_fetch(const cJSON *args, char *text, cJSON **structured)
{
    const char *id, *source, *state;
    if (!get_str(args, "id", false, 23, &id, text) || !get_str(args, "source", false, 24, &source, text)
        || !get_str(args, "state", true, 0, &state, text)) {
        return false;
    }
    if (strcmp(state, "start") != 0 && strcmp(state, "done") != 0 && strcmp(state, "failed") != 0) {
        snprintf(text, TEXT_MAX, "state must be start, done or failed");
        return false;
    }
    lvgl_port_lock();
    const bool out = behaviour_fetch(id, source, state);
    lvgl_port_unlock();
    snprintf(text, TEXT_MAX, out ? "Yoru is on the errand" : "not shown");
    return true;
}

static const tool_t TOOLS[] = {
    {
        .name = "show_menu",
        .description = "Open or close the menu on Yoru's screen: on its home (the apps around Yoru), on an app "
                       "(inbox, timers, habits, settings) or straight on a page of Settings (agents, display, sound, "
                       "behaviour, voice, connection, battery, about).",
        .schema = "{\"type\":\"object\",\"properties\":{\"open\":{\"type\":\"boolean\"},"
                  "\"page\":{\"type\":\"string\"}}}",
        .fn = tool_show_menu,
    },
    {
        .name = "show_approval",
        .description = "Show an agent's permission request on Yoru's screen with Allow and Deny (and Always when "
                       "`always`; only Deny when `full` is false), counting down `seconds`; a tap sends the "
                       "`approval` notification. A second call replaces it. Companion Hub sends these itself.",
        .schema = "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"},"
                  "\"tool\":{\"type\":\"string\"},\"text\":{\"type\":\"string\",\"maxLength\":72},"
                  "\"project\":{\"type\":\"string\"},\"seconds\":{\"type\":\"integer\",\"minimum\":1},"
                  "\"always\":{\"type\":\"boolean\"},\"full\":{\"type\":\"boolean\"}},"
                  "\"required\":[\"id\",\"tool\",\"text\",\"seconds\"]}",
        .fn = tool_show_approval,
    },
    {
        .name = "hide_approval",
        .description = "Take a permission request off Yoru's screen, if that id is shown.",
        .schema = "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},"
                  "\"required\":[\"id\"]}",
        .fn = tool_hide_approval,
    },
    {
        .name = "set_agents",
        .description = "Tell Yoru what every coding agent session is doing (docs/protocol.md, Agents): a dot per "
                       "session on the rim, the step ticker and the agents view. Companion Hub sends it itself.",
        .schema = "{\"type\":\"object\",\"properties\":{\"sessions\":{\"type\":\"array\",\"maxItems\":6,"
                  "\"items\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"},"
                  "\"agent\":{\"type\":\"string\"},\"project\":{\"type\":\"string\"},"
                  "\"state\":{\"type\":\"string\",\"enum\":[\"idle\",\"work\",\"wait\",\"done\",\"error\"]},"
                  "\"task\":{\"type\":\"string\"},\"since_s\":{\"type\":\"integer\"},"
                  "\"steps\":{\"type\":\"array\"}},\"required\":[\"id\",\"state\"]}},"
                  "\"quiet_min\":{\"type\":\"integer\"},\"focus\":{\"type\":\"boolean\"}},"
                  "\"required\":[\"sessions\"]}",
        .fn = tool_set_agents,
    },
    {
        .name = "show_toast",
        .description = "Show one short line over the bottom of Yoru's screen for a few seconds: an agent finished, "
                       "needs you, the plan's limits. A second call replaces it.",
        .schema = "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\",\"maxLength\":72},"
                  "\"seconds\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":30},"
                  "\"tone\":{\"type\":\"string\",\"enum\":[\"info\",\"done\",\"wait\",\"error\"]}},"
                  "\"required\":[\"text\",\"seconds\"]}",
        .fn = tool_show_toast,
    },
    {
        .name = "show_question",
        .description = "Show an agent's question with its choices on Yoru (1-4 questions, 2-4 options each); the "
                       "picks come back with the `answer` notification. A second call replaces it. Companion Hub "
                       "sends these itself.",
        .schema = "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"},"
                  "\"project\":{\"type\":\"string\"},\"seconds\":{\"type\":\"integer\",\"minimum\":1},"
                  "\"questions\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":4,\"items\":{"
                  "\"type\":\"object\",\"properties\":{\"header\":{\"type\":\"string\"},"
                  "\"question\":{\"type\":\"string\"},\"multi\":{\"type\":\"boolean\"},"
                  "\"options\":{\"type\":\"array\",\"minItems\":2,\"maxItems\":4,"
                  "\"items\":{\"type\":\"string\"}}},\"required\":[\"question\",\"options\"]}}},"
                  "\"required\":[\"id\",\"seconds\",\"questions\"]}",
        .fn = tool_show_question,
    },
    {
        .name = "hide_question",
        .description = "Take an agent's question off Yoru's screen, if that id is shown.",
        .schema = "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},"
                  "\"required\":[\"id\"]}",
        .fn = tool_hide_question,
    },
    {
        .name = "show_draft",
        .description = "Show what was said to an agent on Yoru, with Send and Cancel, before it goes; the answer "
                       "comes back with the `draft` notification. The Yoru app sends these itself.",
        .schema = "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"},"
                  "\"text\":{\"type\":\"string\",\"maxLength\":400},\"to\":{\"type\":\"string\",\"maxLength\":40},"
                  "\"seconds\":{\"type\":\"integer\",\"minimum\":1}},\"required\":[\"id\",\"text\",\"to\",\"seconds\"]}",
        .fn = tool_show_draft,
    },
    {
        .name = "hide_draft",
        .description = "Take a draft off Yoru's screen, if that id is shown.",
        .schema = "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},"
                  "\"required\":[\"id\"]}",
        .fn = tool_hide_draft,
    },
    {
        .name = "set_chat",
        .description = "What the chat open on Yoru shows for a session: the prompt and the agent's last reply. "
                       "The Yoru app sends it itself.",
        .schema = "{\"type\":\"object\",\"properties\":{\"session\":{\"type\":\"string\"},"
                  "\"prompt\":{\"type\":\"string\",\"maxLength\":200},"
                  "\"reply\":{\"type\":\"string\",\"maxLength\":1200}},\"required\":[\"session\"]}",
        .fn = tool_set_chat,
    },
    {
        .name = "set_projects",
        .description = "Where a new agent session can start from Yoru: the agents installed and the recent "
                       "projects. The Yoru app sends it itself.",
        .schema = "{\"type\":\"object\",\"properties\":{\"agents\":{\"type\":\"array\",\"maxItems\":4,"
                  "\"items\":{\"type\":\"string\"}},\"projects\":{\"type\":\"array\",\"maxItems\":8,"
                  "\"items\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"},"
                  "\"name\":{\"type\":\"string\"}},\"required\":[\"id\",\"name\"]}}},"
                  "\"required\":[\"agents\",\"projects\"]}",
        .fn = tool_set_projects,
    },
    {
        .name = "show_notification",
        .description = "Put a notification in Yoru's inbox (newest first, at most 30), replacing one with the same "
                       "id. `low` only marks it unread; `normal` also shows a banner with a soft sound; `high` also "
                       "makes Yoru knock on the glass; `urgent` also lights a dark screen. Do not disturb and quiet "
                       "turn all but urgent into low. What the user does comes back with `notification_action`. "
                       "The host sends these itself.",
        .schema = "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\",\"maxLength\":47},"
                  "\"source\":{\"type\":\"string\",\"maxLength\":24},\"title\":{\"type\":\"string\",\"maxLength\":60},"
                  "\"text\":{\"type\":\"string\",\"maxLength\":200},"
                  "\"urgency\":{\"type\":\"string\",\"enum\":[\"low\",\"normal\",\"high\",\"urgent\"]},"
                  "\"actions\":{\"type\":\"array\",\"maxItems\":2,\"items\":{\"type\":\"string\",\"maxLength\":16}}},"
                  "\"required\":[\"id\",\"title\"]}",
        .fn = tool_show_notification,
    },
    {
        .name = "hide_notification",
        .description = "Take a notification out of Yoru's inbox, if that id is there.",
        .schema = "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},"
                  "\"required\":[\"id\"]}",
        .fn = tool_hide_notification,
    },
    {
        .name = "clear_notifications",
        .description = "Empty Yoru's inbox.",
        .schema = "{\"type\":\"object\",\"properties\":{}}",
        .fn = tool_clear_notifications,
    },
    {
        .name = "set_timers",
        .description = "Every timer and reminder the host keeps, for the Timers app on Yoru (at most 8). Send the "
                       "whole list whenever it changes; the board counts them down. What the user does comes back "
                       "with `timer_action`. The host sends it itself.",
        .schema = "{\"type\":\"object\",\"properties\":{\"timers\":{\"type\":\"array\",\"maxItems\":8,"
                  "\"items\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":[\"integer\",\"string\"]},"
                  "\"kind\":{\"type\":\"string\",\"enum\":[\"timer\",\"work\",\"break\",\"reminder\"]},"
                  "\"state\":{\"type\":\"string\",\"enum\":[\"run\",\"pause\",\"ring\"]},"
                  "\"left_s\":{\"type\":\"number\",\"minimum\":0},\"total_s\":{\"type\":\"number\"},"
                  "\"label\":{\"type\":\"string\"},\"at\":{\"type\":\"string\"}},"
                  "\"required\":[\"id\",\"state\",\"left_s\",\"total_s\"]}}},\"required\":[\"timers\"]}",
        .fn = tool_set_timers,
    },
    {
        .name = "set_habits",
        .description = "Today's habits, for the Habits app on Yoru (at most 8): how many times each is done of its "
                       "goal and its streak. A tap on the board ticks one and comes back with `habit_check`. The "
                       "host sends it itself.",
        .schema = "{\"type\":\"object\",\"properties\":{\"habits\":{\"type\":\"array\",\"maxItems\":8,"
                  "\"items\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"},"
                  "\"name\":{\"type\":\"string\"},\"done\":{\"type\":\"integer\"},"
                  "\"goal\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":99},"
                  "\"streak\":{\"type\":\"integer\"},\"color\":{\"type\":\"string\"}},"
                  "\"required\":[\"id\",\"name\"]}}},\"required\":[\"habits\"]}",
        .fn = tool_set_habits,
    },
    {
        .name = "show_fetch",
        .description = "The host's assistant calls one of its connectors: on `start` Yoru dives into a portal in "
                       "the connector's colour, and on `done` (or `failed`) comes back with a parcel (or empty "
                       "handed). `id` pairs a start with its end. The host sends it itself.",
        .schema = "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\",\"maxLength\":23},"
                  "\"source\":{\"type\":\"string\",\"maxLength\":24},"
                  "\"state\":{\"type\":\"string\",\"enum\":[\"start\",\"done\",\"failed\"]}},"
                  "\"required\":[\"state\"]}",
        .fn = tool_show_fetch,
    },
    {
        .name = "set_brightness",
        .description = "Set how bright Yoru's eyes glow while it's awake. Asleep it dims by itself. "
                       "The board remembers it.",
        .schema = "{\"type\":\"object\",\"properties\":{"
                  "\"percent\":{\"type\":\"integer\",\"minimum\":5,\"maximum\":100}},\"required\":[\"percent\"]}",
        .fn = tool_set_brightness,
    },
    {
        .name = "set_mic",
        .description = "Mute or unmute the microphone (muted, Yoru doesn't listen for its wake word or to the "
                       "room); and whether a short chime plays when it hears the wake word (wake_sound, on by "
                       "default). The board remembers both.",
        .schema = "{\"type\":\"object\",\"properties\":{\"muted\":{\"type\":\"boolean\"},"
                  "\"wake_sound\":{\"type\":\"boolean\"}},\"minProperties\":1}",
        .fn = tool_set_mic,
    },
    {
        .name = "set_wake_word",
        .description = "Choose the word Yoru listens for before a voice command. The board remembers it.",
        .schema_fn = wake_word_schema,
        .fn = tool_set_wake_word,
    },
    {
        .name = "set_volume",
        .description = "Set the speaker volume Yoru talks with; 0 is silent. The board remembers it.",
        .schema = "{\"type\":\"object\",\"properties\":{"
                  "\"percent\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100}},\"required\":[\"percent\"]}",
        .fn = tool_set_volume,
    },
    {
        .name = "react",
        .description = "Make Yoru's eyes react now for a few seconds: happy, curious, surprised, sad, love... "
                       "Refused during a voice conversation.",
        .schema_fn = react_schema,
        .fn = tool_react,
    },
    {
        .name = "play_sound",
        .description = "Play one of Yoru's own little sounds on its speaker: a chirp, a giggle, a purr, a yawn, "
                       "a sigh, a tune...",
        .schema_fn = play_sound_schema,
        .fn = tool_play_sound,
    },
    {
        .name = "get_settings",
        .description = "Read all of the board's settings, each with its value and range, as the menu on the "
                       "board shows them: brightness, night hours, volume, sleep times, focus mode, reactions, "
                       "eye colour, language...",
        .schema = "{\"type\":\"object\",\"properties\":{}}",
        .fn = tool_get_settings,
    },
    {
        .name = "set_setting",
        .description = "Change one setting by its get_settings name, within its range (switches are 0 or 1). "
                       "It applies at once and the board remembers it.",
        .schema = "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},"
                  "\"value\":{\"type\":\"integer\"}},\"required\":[\"name\",\"value\"]}",
        .fn = tool_set_setting,
    },
    {
        .name = "get_state",
        .description = "Read the board's state: firmware, uptime, whether metrics are live, the eyes' mood, the "
                       "voice (wake word, muted, listening), the speaker, what Yoru is doing, how busy the UI is, "
                       "memory, the link and the WiFi.",
        .schema = "{\"type\":\"object\",\"properties\":{}}",
        .fn = tool_get_state,
    },
};

#define TOOL_COUNT (sizeof(TOOLS) / sizeof(TOOLS[0]))

cJSON *board_tools_list(const cJSON *params, size_t budget)
{
    // The cursor is the index of the first tool of the page, as a string.
    size_t first = 0;
    const cJSON *cursor = cJSON_GetObjectItemCaseSensitive(params, "cursor");
    if (cJSON_IsString(cursor)) {
        first = (size_t) strtoul(cursor->valuestring, NULL, 10);
    }
    cJSON *result = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(result, "tools");
    size_t used = 0;
    for (size_t i = first; i < TOOL_COUNT; i++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", TOOLS[i].name);
        cJSON_AddStringToObject(t, "description", TOOLS[i].description);
        cJSON_AddItemToObject(t, "inputSchema",
                              TOOLS[i].schema_fn != NULL ? TOOLS[i].schema_fn() : cJSON_Parse(TOOLS[i].schema));
        char *text = cJSON_PrintUnformatted(t);
        size_t size = text != NULL ? strlen(text) + 1 : budget;
        cJSON_free(text);
        // A page holds one tool at least, so a long one still goes.
        if (i > first && used + size > budget) {
            cJSON_Delete(t);
            char next[12];
            snprintf(next, sizeof(next), "%u", (unsigned) i);
            cJSON_AddStringToObject(result, "nextCursor", next);
            break;
        }
        used += size;
        cJSON_AddItemToArray(list, t);
    }
    return result;
}

cJSON *board_tools_call(const char *name, const cJSON *args)
{
    const tool_t *tool = NULL;
    for (size_t i = 0; i < TOOL_COUNT && tool == NULL; i++) {
        if (strcmp(TOOLS[i].name, name) == 0) {
            tool = &TOOLS[i];
        }
    }
    if (tool == NULL) {
        return NULL;
    }
    char text[TEXT_MAX];
    cJSON *structured = NULL;
    bool ok = tool->fn(args, text, &structured);

    cJSON *result = cJSON_CreateObject();
    cJSON *content = cJSON_AddArrayToObject(result, "content");
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "text");
    if (structured != NULL && text[0] == '\0') {
        char *json = cJSON_PrintUnformatted(structured);
        cJSON_AddStringToObject(item, "text", json != NULL ? json : "");
        cJSON_free(json);
    } else {
        cJSON_AddStringToObject(item, "text", text);
    }
    cJSON_AddItemToArray(content, item);
    if (structured != NULL) {
        cJSON_AddItemToObject(result, "structuredContent", structured);
    }
    cJSON_AddBoolToObject(result, "isError", !ok);
    return result;
}
