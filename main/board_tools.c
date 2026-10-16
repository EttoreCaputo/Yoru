#include "board_tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "approval.h"
#include "behaviour.h"
#include "board_pmu.h"
#include "board_settings.h"
#include "ears.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "eyes.h"
#include "link.h"
#include "lvgl_port.h"
#include "menu.h"
#include "metrics_model.h"
#include "motion.h"
#include "net.h"
#include "pet.h"
#include "playback.h"
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
    pet_event(PET_EVENT_PLAYED);
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
    pet_event(PET_EVENT_PLAYED);
    snprintf(text, TEXT_MAX, "playing %s", name->valuestring);
    return true;
}

static bool tool_set_pet(const cJSON *args, char *text, cJSON **structured)
{
    const cJSON *sounds = cJSON_GetObjectItemCaseSensitive(args, "sounds");
    const cJSON *react = cJSON_GetObjectItemCaseSensitive(args, "react");
    const cJSON *hear = cJSON_GetObjectItemCaseSensitive(args, "hear");
    if ((sounds != NULL && !cJSON_IsBool(sounds)) || (react != NULL && !cJSON_IsBool(react))
        || (hear != NULL && !cJSON_IsBool(hear)) || (sounds == NULL && react == NULL && hear == NULL)) {
        snprintf(text, TEXT_MAX, "give sounds, react and/or hear, true or false");
        return false;
    }
    int n = 0;
    if (sounds != NULL) {
        bool on = cJSON_IsTrue(sounds);
        board_settings_set(SETTING_PET_SOUNDS, on);
        if (on) {
            playback_sound(SOUND_HAPPY);
        }
        n += snprintf(text, TEXT_MAX, on ? "it makes its little sounds" : "it keeps quiet, but for chimes");
    }
    if (react != NULL) {
        bool on = cJSON_IsTrue(react);
        board_settings_set(SETTING_PET_REACTIONS, on);
        n += snprintf(text + n, TEXT_MAX - n, "%s%s", n ? "; " : "",
                      on ? "it reacts to the computer" : "no reactions to the computer");
    }
    if (hear != NULL && n < TEXT_MAX) {
        bool on = cJSON_IsTrue(hear);
        board_settings_set(SETTING_PET_HEARING, on);
        snprintf(text + n, TEXT_MAX - n, "%s%s", n ? "; " : "",
                 on ? "it listens to the room" : "it doesn't listen to the room");
    }
    return true;
}

static double round2(float v)
{
    return (int) (v * 100.0f + (v < 0 ? -0.5f : 0.5f)) / 100.0;
}

static bool tool_get_pet(const cJSON *args, char *text, cJSON **structured)
{
    pet_state_t pet;
    pet_get(&pet);
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "mood", pet_mood_name(pet.mood));
    cJSON_AddNumberToObject(p, "energy", round2(pet.energy));
    cJSON_AddNumberToObject(p, "happiness", round2(pet.happiness));
    cJSON_AddNumberToObject(p, "affection", round2(pet.affection));
    cJSON_AddBoolToObject(p, "away", pet.away);
    cJSON *now = cJSON_AddArrayToObject(p, "now");
    for (uint32_t bit = 1; bit != 0 && bit <= PET_NOW_LAST; bit <<= 1) {
        if (pet.now & bit) {
            cJSON_AddItemToArray(now, cJSON_CreateString(pet_now_name(bit)));
        }
    }
    if (pet.minute >= 0) {
        cJSON_AddNumberToObject(p, "minute", pet.minute);
    }
    if (pet.idle_s >= 0) {
        cJSON_AddNumberToObject(p, "idle_s", pet.idle_s);
    }
    cJSON_AddNumberToObject(p, "lonely_s", pet.lonely_s);
    cJSON_AddNumberToObject(p, "pitch", round2(pet.pitch));
    cJSON_AddNumberToObject(p, "pace", round2(pet.pace));
    cJSON *recent = cJSON_AddArrayToObject(p, "recent");
    for (int i = 0; i < pet.recent_count; i++) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "what", pet.recent[i].what);
        cJSON_AddStringToObject(r, "scene", pet.recent[i].scene);
        cJSON_AddNumberToObject(r, "ago_s", pet.recent[i].ago_s);
        cJSON_AddItemToArray(recent, r);
    }
    cJSON_AddBoolToObject(p, "sounds", board_settings_on(SETTING_PET_SOUNDS));
    cJSON_AddBoolToObject(p, "reactions", board_settings_on(SETTING_PET_REACTIONS));
    cJSON_AddBoolToObject(p, "hearing", board_settings_on(SETTING_PET_HEARING));
    ears_state_t ears;
    ears_get(&ears);
    if (ears.listening) {
        cJSON *room = cJSON_AddObjectToObject(p, "room");
        cJSON_AddNumberToObject(room, "floor_db", (int) ears.floor_db);
        cJSON_AddNumberToObject(room, "level_db", (int) ears.level_db);
        cJSON_AddNumberToObject(room, "quiet_s", ears.quiet_s);
        cJSON_AddBoolToObject(room, "chatter", ears.chatter);
    }
    *structured = p;
    text[0] = '\0';
    return true;
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
    pet_state_t pet;
    pet_get(&pet);
    cJSON *p = cJSON_AddObjectToObject(st, "pet");
    cJSON_AddStringToObject(p, "mood", pet_mood_name(pet.mood));
    cJSON_AddNumberToObject(p, "energy", round2(pet.energy));
    cJSON_AddNumberToObject(p, "happiness", round2(pet.happiness));
    cJSON_AddNumberToObject(p, "affection", round2(pet.affection));
    cJSON_AddBoolToObject(p, "away", pet.away);
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
    if (approval_is_open() && !cJSON_IsFalse(open)) {
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
    lvgl_port_lock();
    approval_show(id->valuestring, tool->valuestring, what->valuestring,
                  cJSON_IsString(project) ? project->valuestring : NULL, seconds);
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

static const tool_t TOOLS[] = {
    {
        .name = "show_menu",
        .description = "Open or close the settings menu on Yoru's screen, on the ring of icons or straight on a page "
                       "(display, sound, behaviour, voice, connection, battery, about).",
        .schema = "{\"type\":\"object\",\"properties\":{\"open\":{\"type\":\"boolean\"},"
                  "\"page\":{\"type\":\"string\"}}}",
        .fn = tool_show_menu,
    },
    {
        .name = "show_approval",
        .description = "Show a Claude Code permission request on Yoru's screen with Allow and Deny, counting "
                       "down `seconds`; a tap sends the `approval` notification. A second call replaces it. "
                       "Companion Hub sends these itself.",
        .schema = "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"},"
                  "\"tool\":{\"type\":\"string\"},\"text\":{\"type\":\"string\",\"maxLength\":72},"
                  "\"project\":{\"type\":\"string\"},\"seconds\":{\"type\":\"integer\",\"minimum\":1}},"
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
        .name = "set_pet",
        .description = "Whether Yoru makes its little sounds (sounds; chimes play either way), reacts to what "
                       "happens on the computer (react: a hot CPU, music, Claude finishing, the battery, bedtime, "
                       "you coming back) and listens to the room (hear: a bang, claps, people talking, silence). "
                       "All on by default; the board remembers them.",
        .schema = "{\"type\":\"object\",\"properties\":{\"sounds\":{\"type\":\"boolean\"},"
                  "\"react\":{\"type\":\"boolean\"},\"hear\":{\"type\":\"boolean\"}},\"minProperties\":1}",
        .fn = tool_set_pet,
    },
    {
        .name = "get_pet",
        .description = "Read Yoru's mood (content, happy, excited, loving, bored, grumpy, sad, sleepy, hot), its "
                       "energy, happiness and affection (0 to 1), what's affecting it now, what the room sounds "
                       "like and its last reactions.",
        .schema = "{\"type\":\"object\",\"properties\":{}}",
        .fn = tool_get_pet,
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
                       "voice (wake word, muted, listening), the speaker, the pet's mood, how busy the UI is, "
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
