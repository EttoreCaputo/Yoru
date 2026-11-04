#include "friends.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "link_crypto.h"
#include "lvgl_port.h"
#include "nvs.h"

static const char *TAG = "friends";

/* Often enough that a friend shows up within seconds, rarely enough that the
 * radio doesn't notice. */
#define HELLO_MS 4000
/* Several hellos lost before a friend counts as gone: on battery the radio
 * sleeps between beacons and misses some. */
#define GONE_MS 30000
/* Met again after this long apart, it's greeted again. */
#define GREET_AGAIN_MS (30 * 60 * 1000)
#define MAX_FRIENDS 6
/* Brought together: the signal was this weak (apart) and, within
 * CLOSE_WITHIN_US, this strong (a few cm). Measured on two boards: -64 to
 * -86 dBm apart, -50 to -55 lying near each other, -37 to -45 brought
 * against each other. */
#define FAR_DBM (-58)
#define CLOSE_DBM (-46)
#define CLOSE_WITHIN_US (1500 * 1000)
/* One meeting, not several, while they stay close. */
#define CLOSE_AGAIN_US (5000 * 1000)
/* While either is being moved, hellos this often: the signal is followed
 * closely enough to see them come together. */
#define FAST_HELLO_MS 250
#define FAST_FOR_US (2000 * 1000)
/* Both heard the same wake word: their detections are this close. */
#define HEARD_WINDOW_US (700 * 1000)
/* How long after our wake word a louder friend's still counts. */
#define HEARD_LATE_US (2000 * 1000)
#define EVENTS 8
#define NVS_NAMESPACE "friends"

#define MAGIC "YORU"
#define VERSION 1
#define MSG_HELLO 1
#define MSG_EVENT 2 // a, b: as friend_ev_kind_t says
#define MSG_CLOSE 3 // we were brought together
#define MSG_SIDE 6  // a: where we found the receiver, degrees in our panel's axes
#define MSG_BALL 4  // a: where along the edge x1000, b: speed
#define MSG_HEARD 5 // a: the wake word's volume in dB x10

/* On the air: small, fixed, and versioned, so a later firmware can add
 * messages without confusing this one. Boards before 0.20.0 only know the
 * hello, and drop the rest for its size. */
typedef struct __attribute__((packed)) {
    char magic[4];
    uint8_t version;
    uint8_t type;
    uint8_t flags; // FLAG_*
    uint8_t reserved;
    char firmware[16];
} hello_t;

typedef struct __attribute__((packed)) {
    char magic[4];
    uint8_t version;
    uint8_t type;
    uint8_t kind;
    uint8_t reserved;
    int16_t a, b;
} msg_t;

#define FLAG_AWAKE 0x01
#define FLAG_PLAYING 0x02
#define FLAG_MOVING 0x04

typedef struct {
    uint8_t mac[6];
    bool used;
    bool awake, playing;
    bool present; // heard within GONE_MS
    int8_t rssi;
    int64_t seen_us;
    int64_t met_us; // when it was last greeted, 0 never
    char firmware[16];
    /* Which way it is, in the panel's axes, from a bump; looked up in NVS
     * once (side_looked), saved when a bump changes it (side_dirty). */
    bool sided, side_looked, side_dirty;
    float side_x, side_y;
    int64_t heard_us;
    /* Coming together: when its signal was last weak, when they last met
     * up close, when it was last being moved. */
    int64_t far_us, close_us, moving_us;
    int close_n; // hellos in a row at CLOSE_DBM or more
    float heard_db;
} friend_t;

typedef struct {
    friend_ev_kind_t kind;
    int16_t a, b;
    int who; // index in s_friends
} queued_t;

static const uint8_t BROADCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static friend_t s_friends[MAX_FRIENDS];
static queued_t s_events[EVENTS];
static int s_ev_head, s_ev_count;
static bool s_on;
static volatile bool s_awake = true;
static volatile bool s_playing;
static esp_timer_handle_t s_timer;
static uint32_t s_hello_ms = HELLO_MS;
static uint8_t s_mac[6];
/* Meetings up close, for get_state; when we last said a fast hello. */
static int s_meetings;
static int64_t s_fast_hello_us;
static volatile bool s_moving;
static volatile int64_t s_moved_us; // when we were last being moved
/* Our last wake word, under s_mux. */
static int64_t s_heard_us;
static float s_heard_db;
static bool s_heard_done;
/* A ball thrown to us, under s_mux. */
static bool s_ball;
static float s_ball_along, s_ball_speed;

static void send_hello(void *arg)
{
    hello_t h = {.version = VERSION,
                 .type = MSG_HELLO,
                 .flags = (s_awake ? FLAG_AWAKE : 0) | (s_playing ? FLAG_PLAYING : 0) | (s_moving ? FLAG_MOVING : 0)};
    memcpy(h.magic, MAGIC, sizeof(h.magic));
    strlcpy(h.firmware, esp_app_get_description()->version, sizeof(h.firmware));
    // Lost hellos don't matter: the next one is a few seconds away.
    esp_now_send(BROADCAST, (const uint8_t *) &h, sizeof(h));
}

static void send_msg(uint8_t type, uint8_t kind, int16_t a, int16_t b)
{
    if (!s_on) {
        return;
    }
    msg_t m = {.version = VERSION, .type = type, .kind = kind, .a = a, .b = b};
    memcpy(m.magic, MAGIC, sizeof(m.magic));
    esp_now_send(BROADCAST, (const uint8_t *) &m, sizeof(m));
}

/* With s_mux held. */
static void push(friend_ev_kind_t kind, int16_t a, int16_t b, int who)
{
    if (s_ev_count == EVENTS) {
        // Full: the oldest goes, it's stale by now.
        s_ev_head = (s_ev_head + 1) % EVENTS;
        s_ev_count--;
    }
    s_events[(s_ev_head + s_ev_count) % EVENTS] = (queued_t) {kind, a, b, who};
    s_ev_count++;
}

/* With s_mux held: brought together with friend `who` (seen here, or told
 * by it): once, until they've been apart a while. */
static void met_close(int who, int64_t now, bool tell)
{
    friend_t *f = &s_friends[who];
    if (f->close_us != 0 && now - f->close_us < CLOSE_AGAIN_US) {
        return;
    }
    f->close_us = now;
    s_meetings++;
    push(FRIEND_BUMP, tell ? 1 : 0, 0, who);
}

/* With s_mux held: the friend `mac` is, created on its hello only. */
static int find(const uint8_t *mac, bool create, int64_t now)
{
    int free_slot = -1, oldest = -1;
    for (int i = 0; i < MAX_FRIENDS; i++) {
        const friend_t *c = &s_friends[i];
        if (c->used && memcmp(c->mac, mac, 6) == 0) {
            return i;
        }
        if (!c->used && free_slot < 0) {
            free_slot = i;
        }
        if (c->used && (oldest < 0 || c->seen_us < s_friends[oldest].seen_us)) {
            oldest = i;
        }
    }
    if (!create) {
        return -1;
    }
    // A full table forgets the friend not heard from the longest.
    const int i = free_slot >= 0 ? free_slot : oldest;
    memset(&s_friends[i], 0, sizeof(s_friends[i]));
    memcpy(s_friends[i].mac, mac, 6);
    s_friends[i].used = true;
    s_friends[i].seen_us = now;
    return i;
}

/* The WiFi task: short, and only the table. */
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < (int) sizeof(msg_t) || memcmp(data, MAGIC, 4) != 0 || data[4] != VERSION) {
        return;
    }
    const uint8_t type = data[5];
    const int64_t now = esp_timer_get_time();
    if (type == MSG_HELLO) {
        if (len < (int) sizeof(hello_t)) {
            return;
        }
        hello_t h;
        memcpy(&h, data, sizeof(h));
        h.firmware[sizeof(h.firmware) - 1] = '\0';
        portENTER_CRITICAL(&s_mux);
        friend_t *f = &s_friends[find(info->src_addr, true, now)];
        f->awake = (h.flags & FLAG_AWAKE) != 0;
        f->playing = (h.flags & FLAG_PLAYING) != 0;
        if (h.flags & FLAG_MOVING) {
            f->moving_us = now;
        }
        f->rssi = info->rx_ctrl != NULL ? info->rx_ctrl->rssi : 0;
        f->seen_us = now;
        // Apart a moment ago, against each other now, twice in a row, and
        // one of the two being moved: brought together. Lying still, the
        // signal jumps now and then on its own.
        const bool moved = (s_moved_us != 0 && now - s_moved_us < CLOSE_WITHIN_US)
                           || (f->moving_us != 0 && now - f->moving_us < CLOSE_WITHIN_US);
        f->close_n = f->rssi >= CLOSE_DBM ? f->close_n + 1 : 0;
        if (f->rssi <= FAR_DBM) {
            f->far_us = now;
        } else if (f->close_n >= 2 && moved && f->far_us != 0 && now - f->far_us < CLOSE_WITHIN_US) {
            f->far_us = 0;
            met_close((int) (f - s_friends), now, true);
        }
        memcpy(f->firmware, h.firmware, sizeof(f->firmware));
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    msg_t m;
    memcpy(&m, data, sizeof(m));
    portENTER_CRITICAL(&s_mux);
    // Only from a friend whose hello we heard.
    const int who = find(info->src_addr, false, now);
    if (who >= 0) {
        friend_t *f = &s_friends[who];
        f->seen_us = now;
        switch (type) {
        case MSG_EVENT:
            if (m.kind < FRIEND_BUMP) {
                push((friend_ev_kind_t) m.kind, m.a, m.b, who);
            }
            break;
        case MSG_CLOSE:
            met_close(who, now, false);
            break;
        case MSG_SIDE:
            // It found us that way: it is the other way from us (both
            // lying the same way up).
            f->side_x = -cosf((float) m.a * (float) M_PI / 180.0f);
            f->side_y = -sinf((float) m.a * (float) M_PI / 180.0f);
            f->sided = f->side_looked = f->side_dirty = true;
            push(FRIEND_SIDED, 0, 0, who);
            break;
        case MSG_BALL:
            if (s_playing) {
                s_ball = true;
                s_ball_along = (float) m.a / 1000.0f;
                s_ball_speed = (float) m.b;
            }
            break;
        case MSG_HEARD:
            f->heard_us = now;
            f->heard_db = (float) m.a / 10.0f;
            break;
        default:
            break;
        }
    }
    portEXIT_CRITICAL(&s_mux);
}

/* On at boot again since 0.19.2, after a test on a phone hotspot showed the
 * hello harmless (docs/FRIENDS.md); `debug/friends` turns it off until the
 * next restart, should a router mind it. */
static bool s_enabled = true;
static bool s_wifi_up;
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;

static void radio(bool on)
{
    if (on == s_on) {
        return;
    }
    if (on) {
        esp_err_t err = esp_now_init();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ESP-NOW unavailable: %s", esp_err_to_name(err));
            return;
        }
        esp_read_mac(s_mac, ESP_MAC_WIFI_STA);
        esp_now_register_recv_cb(on_recv);
        // Channel 0: whatever the WiFi is on, the access point's.
        const esp_now_peer_info_t peer = {.ifidx = WIFI_IF_STA, .channel = 0, .encrypt = false};
        esp_now_peer_info_t p = peer;
        memcpy(p.peer_addr, BROADCAST, 6);
        esp_now_add_peer(&p);
        if (s_timer == NULL) {
            const esp_timer_create_args_t args = {.callback = send_hello, .name = "friends"};
            esp_timer_create(&args, &s_timer);
        }
        esp_timer_start_periodic(s_timer, (uint64_t) s_hello_ms * 1000);
        s_on = true;
        send_hello(NULL);
        ESP_LOGI(TAG, "looking for friends");
    } else {
        esp_timer_stop(s_timer);
        s_on = false;
        esp_now_deinit();
    }
}

/* The WiFi's event task and the link's task both get here. */
static void apply(bool *what, bool on)
{
    static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    }
    portEXIT_CRITICAL(&mux);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *what = on;
    radio(s_enabled && s_wifi_up);
    xSemaphoreGive(s_lock);
}

void friends_radio(bool on)
{
    apply(&s_wifi_up, on);
}

void friends_enable(bool on)
{
    ESP_LOGI(TAG, "hello %s", on ? "on" : "off");
    apply(&s_enabled, on);
}

bool friends_enabled(void)
{
    return s_enabled;
}

/* ---- Sides, kept across restarts -------------------------------------------- */

/* The LVGL task, outside s_mux: NVS takes a few ms. */
static void keep_sides(void)
{
    for (int i = 0; i < MAX_FRIENDS; i++) {
        portENTER_CRITICAL(&s_mux);
        friend_t f = s_friends[i];
        portEXIT_CRITICAL(&s_mux);
        if (!f.used || (f.side_looked && !f.side_dirty)) {
            continue;
        }
        char key[13];
        link_hex(f.mac, 6, key);
        nvs_handle_t nvs;
        if (nvs_open(NVS_NAMESPACE, f.side_dirty ? NVS_READWRITE : NVS_READONLY, &nvs) != ESP_OK) {
            portENTER_CRITICAL(&s_mux);
            s_friends[i].side_looked = true;
            portEXIT_CRITICAL(&s_mux);
            continue;
        }
        if (f.side_dirty) {
            const int16_t deg = (int16_t) lroundf(atan2f(f.side_y, f.side_x) * 180.0f / (float) M_PI);
            nvs_set_i16(nvs, key, deg);
            nvs_commit(nvs);
            ESP_LOGI(TAG, "%s is at %d degrees", key, deg);
        }
        int16_t deg = 0;
        const bool known = !f.side_dirty && nvs_get_i16(nvs, key, &deg) == ESP_OK;
        nvs_close(nvs);
        portENTER_CRITICAL(&s_mux);
        friend_t *g = &s_friends[i];
        if (g->used && memcmp(g->mac, f.mac, 6) == 0) {
            g->side_looked = true;
            g->side_dirty = false;
            if (known && !g->sided) {
                g->sided = true;
                g->side_x = cosf((float) deg * (float) M_PI / 180.0f);
                g->side_y = sinf((float) deg * (float) M_PI / 180.0f);
            }
        }
        portEXIT_CRITICAL(&s_mux);
    }
}

/* From the panel's axes to the picture's, the screen may be turned (as
 * behaviour.c's lean). */
static void to_picture(float x, float y, float *px, float *py)
{
    switch (lvgl_port_rotation()) {
    case 1:
        *px = -y;
        *py = x;
        break;
    case 2:
        *px = -x;
        *py = -y;
        break;
    case 3:
        *px = y;
        *py = -x;
        break;
    default:
        *px = x;
        *py = y;
        break;
    }
}

/* ---- Every tick -------------------------------------------------------------- */

bool friends_tick(bool awake, bool free, bool moving)
{
    const int64_t now0 = esp_timer_get_time();
    s_moving = moving;
    if (moving) {
        s_moved_us = now0;
    }
    if (s_awake != awake) {
        s_awake = awake;
        if (s_on) {
            send_hello(NULL); // the others know at once
        }
    }
    // While either is moved, hellos come fast, so coming together shows.
    bool fast = moving;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < MAX_FRIENDS && !fast; i++) {
        fast = s_friends[i].used && s_friends[i].moving_us != 0 && now0 - s_friends[i].moving_us < FAST_FOR_US;
    }
    portEXIT_CRITICAL(&s_mux);
    if (s_on && fast && now0 - s_fast_hello_us >= FAST_HELLO_MS * 1000) {
        s_fast_hello_us = now0;
        send_hello(NULL);
    }
    keep_sides();
    const int64_t now = esp_timer_get_time();
    bool greet = false;
    char met[13] = "";
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < MAX_FRIENDS; i++) {
        friend_t *f = &s_friends[i];
        if (!f->used) {
            continue;
        }
        const bool present = now - f->seen_us < (int64_t) GONE_MS * 1000;
        if (present && !f->present && !greet
            && (f->met_us == 0 || now - f->met_us >= (int64_t) GREET_AGAIN_MS * 1000)) {
            // Only while Yoru can show it; until then it waits.
            if (free) {
                f->met_us = now;
                greet = true;
                link_hex(f->mac, 6, met);
            } else {
                continue; // stays "not present" until it can be greeted
            }
        }
        f->present = present;
    }
    portEXIT_CRITICAL(&s_mux);
    if (greet) {
        ESP_LOGI(TAG, "met %s", met);
    }
    return greet;
}

static int count(bool awake_only)
{
    const int64_t now = esp_timer_get_time();
    int n = 0;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < MAX_FRIENDS; i++) {
        const friend_t *f = &s_friends[i];
        n += f->used && now - f->seen_us < (int64_t) GONE_MS * 1000 && (!awake_only || f->awake);
    }
    portEXIT_CRITICAL(&s_mux);
    return n;
}

int friends_count(void)
{
    return count(false);
}

int friends_awake(void)
{
    return count(true);
}

/* With s_mux held: the friend in range with the strongest signal, -1. */
static int nearest(int64_t now)
{
    int best = -1;
    for (int i = 0; i < MAX_FRIENDS; i++) {
        const friend_t *f = &s_friends[i];
        if (f->used && now - f->seen_us < (int64_t) GONE_MS * 1000 && (best < 0 || f->rssi > s_friends[best].rssi)) {
            best = i;
        }
    }
    return best;
}

cJSON *friends_status(void)
{
    friend_t copy[MAX_FRIENDS];
    portENTER_CRITICAL(&s_mux);
    memcpy(copy, s_friends, sizeof(copy));
    portEXIT_CRITICAL(&s_mux);
    const int64_t now = esp_timer_get_time();
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < MAX_FRIENDS; i++) {
        const friend_t *f = &copy[i];
        if (!f->used || now - f->seen_us >= (int64_t) GONE_MS * 1000) {
            continue;
        }
        char id[13];
        link_hex(f->mac, 6, id);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", id);
        cJSON_AddNumberToObject(o, "rssi", f->rssi);
        cJSON_AddNumberToObject(o, "seen_s", (int) ((now - f->seen_us) / 1000000));
        cJSON_AddBoolToObject(o, "awake", f->awake);
        cJSON_AddStringToObject(o, "firmware", f->firmware);
        if (f->sided) {
            const int deg = (int) lroundf(atan2f(f->side_y, f->side_x) * 180.0f / (float) M_PI);
            cJSON_AddNumberToObject(o, "side_deg", deg);
        }
        if (f->playing) {
            cJSON_AddBoolToObject(o, "playing", true);
        }
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

/* ---- What they tell each other ------------------------------------------ */

void friends_send(friend_ev_kind_t kind, int16_t a, int16_t b)
{
    if (kind < FRIEND_BUMP) {
        send_msg(MSG_EVENT, (uint8_t) kind, a, b);
    }
}

bool friends_next(friend_ev_t *ev)
{
    portENTER_CRITICAL(&s_mux);
    if (s_ev_count == 0) {
        portEXIT_CRITICAL(&s_mux);
        return false;
    }
    const queued_t q = s_events[s_ev_head];
    s_ev_head = (s_ev_head + 1) % EVENTS;
    s_ev_count--;
    const friend_t f = s_friends[q.who];
    portEXIT_CRITICAL(&s_mux);
    if (q.kind == FRIEND_BUMP && q.a == 1) {
        send_msg(MSG_CLOSE, 0, 0, 0); // it may not have seen it yet
    }
    *ev = (friend_ev_t) {.kind = q.kind, .a = q.a, .b = q.b, .sided = f.sided};
    if (f.sided) {
        to_picture(f.side_x, f.side_y, &ev->side_x, &ev->side_y);
    }
    return true;
}

bool friends_side(float *x, float *y)
{
    portENTER_CRITICAL(&s_mux);
    const int i = nearest(esp_timer_get_time());
    const bool sided = i >= 0 && s_friends[i].sided;
    const float sx = sided ? s_friends[i].side_x : 0, sy = sided ? s_friends[i].side_y : 0;
    portEXIT_CRITICAL(&s_mux);
    if (sided) {
        to_picture(sx, sy, x, y);
    }
    return sided;
}

/* From the picture's axes back to the panel's. */
static void from_picture(float px, float py, float *x, float *y)
{
    switch (lvgl_port_rotation()) {
    case 1:
        *x = py;
        *y = -px;
        break;
    case 2:
        *x = -px;
        *y = -py;
        break;
    case 3:
        *x = -py;
        *y = px;
        break;
    default:
        *x = px;
        *y = py;
        break;
    }
}

void friends_found_at(float x, float y)
{
    float px, py;
    from_picture(x, y, &px, &py);
    const float len = hypotf(px, py);
    if (len <= 0) {
        return;
    }
    portENTER_CRITICAL(&s_mux);
    const int i = nearest(esp_timer_get_time());
    if (i >= 0) {
        friend_t *f = &s_friends[i];
        f->side_x = px / len;
        f->side_y = py / len;
        f->sided = f->side_looked = f->side_dirty = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (i >= 0) {
        send_msg(MSG_SIDE, 0, (int16_t) lroundf(atan2f(py, px) * 180.0f / (float) M_PI), 0);
    }
}

static const char *const EV_NAMES[FRIEND_EV_COUNT] = {
    "yawn", "sneeze", "hiccup", "dance", "asleep", "woke", "stroked", "talk", "chat", "firefly", "bump", "sided",
};

bool friends_tell(const char *name)
{
    for (int i = 0; i < FRIEND_EV_COUNT; i++) {
        if (strcmp(name, EV_NAMES[i]) == 0 && i < FRIEND_BUMP) {
            // A chat as its last turn (the other giggles), a firefly mid-edge.
            friends_send((friend_ev_kind_t) i, i == FRIEND_CHAT ? 3 : 0, i == FRIEND_CHAT ? 3 : 0);
            return true;
        }
    }
    return false;
}

void friends_bump_stats(cJSON *out)
{
    cJSON_AddNumberToObject(out, "meetings", s_meetings);
}

void friends_hello_every(int ms)
{
    s_hello_ms = (uint32_t) (ms < 100 ? 100 : ms > 60000 ? 60000 : ms);
    if (s_on && s_timer != NULL) {
        esp_timer_stop(s_timer);
        esp_timer_start_periodic(s_timer, (uint64_t) s_hello_ms * 1000);
    }
}

bool friends_set_side(int deg)
{
    portENTER_CRITICAL(&s_mux);
    const int i = nearest(esp_timer_get_time());
    if (i >= 0) {
        friend_t *f = &s_friends[i];
        f->side_x = cosf((float) deg * (float) M_PI / 180.0f);
        f->side_y = sinf((float) deg * (float) M_PI / 180.0f);
        f->sided = f->side_looked = f->side_dirty = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return i >= 0;
}

/* ---- The ball -------------------------------------------------------------- */

void friends_playing(bool on)
{
    s_playing = on;
    if (!on) {
        portENTER_CRITICAL(&s_mux);
        s_ball = false;
        portEXIT_CRITICAL(&s_mux);
    }
    if (s_on) {
        send_hello(NULL);
    }
}

bool friends_partner_playing(void)
{
    portENTER_CRITICAL(&s_mux);
    const int i = nearest(esp_timer_get_time());
    const bool playing = i >= 0 && s_friends[i].playing;
    portEXIT_CRITICAL(&s_mux);
    return playing;
}

void friends_throw(float along, float speed)
{
    const float a = along < -1 ? -1 : along > 1 ? 1 : along;
    send_msg(MSG_BALL, 0, (int16_t) lroundf(a * 1000), (int16_t) fminf(30000, speed));
}

bool friends_catch(float *along, float *speed)
{
    portENTER_CRITICAL(&s_mux);
    const bool got = s_ball;
    s_ball = false;
    *along = s_ball_along;
    *speed = s_ball_speed;
    portEXIT_CRITICAL(&s_mux);
    return got;
}

/* ---- The wake word --------------------------------------------------------- */

void friends_heard(float volume_db)
{
    portENTER_CRITICAL(&s_mux);
    s_heard_us = esp_timer_get_time();
    s_heard_db = volume_db;
    s_heard_done = false;
    portEXIT_CRITICAL(&s_mux);
    send_msg(MSG_HEARD, 0, (int16_t) lroundf(volume_db * 10), 0);
}

bool friends_outheard(void)
{
    const int64_t now = esp_timer_get_time();
    bool yield = false;
    portENTER_CRITICAL(&s_mux);
    if (s_heard_us != 0 && !s_heard_done && now - s_heard_us < HEARD_LATE_US) {
        for (int i = 0; i < MAX_FRIENDS && !yield; i++) {
            const friend_t *f = &s_friends[i];
            if (!f->used || f->heard_us == 0 || llabs(f->heard_us - s_heard_us) > HEARD_WINDOW_US) {
                continue;
            }
            // Louder answers; on a tie, the same board on both sides.
            const float d = f->heard_db - s_heard_db;
            yield = d > 0.5f || (d >= -0.5f && memcmp(f->mac, s_mac, 6) > 0);
        }
        s_heard_done = yield;
    }
    portEXIT_CRITICAL(&s_mux);
    return yield;
}
