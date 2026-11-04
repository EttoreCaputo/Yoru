#include "friends.h"

#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "link_crypto.h"

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

#define MAGIC "YORU"
#define VERSION 1
#define MSG_HELLO 1

/* On the air: small, fixed, and versioned, so a later firmware can add
 * messages without confusing this one. */
typedef struct __attribute__((packed)) {
    char magic[4];
    uint8_t version;
    uint8_t type;
    uint8_t flags; // FLAG_*
    uint8_t reserved;
    char firmware[16];
} hello_t;

#define FLAG_AWAKE 0x01

typedef struct {
    uint8_t mac[6];
    bool used;
    bool awake;
    bool present; // heard within GONE_MS
    int8_t rssi;
    int64_t seen_us;
    int64_t met_us; // when it was last greeted, 0 never
    char firmware[16];
} friend_t;

static const uint8_t BROADCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static friend_t s_friends[MAX_FRIENDS];
static bool s_on;
static volatile bool s_awake = true;
static esp_timer_handle_t s_timer;

static void send_hello(void *arg)
{
    hello_t h = {.version = VERSION, .type = MSG_HELLO, .flags = s_awake ? FLAG_AWAKE : 0};
    memcpy(h.magic, MAGIC, sizeof(h.magic));
    strlcpy(h.firmware, esp_app_get_description()->version, sizeof(h.firmware));
    // Lost hellos don't matter: the next one is a few seconds away.
    esp_now_send(BROADCAST, (const uint8_t *) &h, sizeof(h));
}

/* The WiFi task: short, and only the table. */
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < (int) sizeof(hello_t) || memcmp(data, MAGIC, 4) != 0) {
        return;
    }
    hello_t h;
    memcpy(&h, data, sizeof(h));
    if (h.version != VERSION || h.type != MSG_HELLO) {
        return;
    }
    h.firmware[sizeof(h.firmware) - 1] = '\0';
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_mux);
    friend_t *f = NULL, *free_slot = NULL, *oldest = NULL;
    for (int i = 0; i < MAX_FRIENDS; i++) {
        friend_t *c = &s_friends[i];
        if (c->used && memcmp(c->mac, info->src_addr, 6) == 0) {
            f = c;
            break;
        }
        if (!c->used && free_slot == NULL) {
            free_slot = c;
        }
        if (c->used && (oldest == NULL || c->seen_us < oldest->seen_us)) {
            oldest = c;
        }
    }
    if (f == NULL) {
        // A full table forgets the friend not heard from the longest.
        f = free_slot != NULL ? free_slot : oldest;
        memset(f, 0, sizeof(*f));
        memcpy(f->mac, info->src_addr, 6);
        f->used = true;
    }
    f->awake = (h.flags & FLAG_AWAKE) != 0;
    f->rssi = info->rx_ctrl != NULL ? info->rx_ctrl->rssi : 0;
    f->seen_us = now;
    memcpy(f->firmware, h.firmware, sizeof(f->firmware));
    portEXIT_CRITICAL(&s_mux);
}

void friends_radio(bool on)
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
        esp_timer_start_periodic(s_timer, (uint64_t) HELLO_MS * 1000);
        s_on = true;
        send_hello(NULL);
        ESP_LOGI(TAG, "looking for friends");
    } else {
        esp_timer_stop(s_timer);
        esp_now_deinit();
        s_on = false;
    }
}

bool friends_tick(bool awake, bool free)
{
    s_awake = awake;
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

int friends_count(void)
{
    const int64_t now = esp_timer_get_time();
    int n = 0;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < MAX_FRIENDS; i++) {
        n += s_friends[i].used && now - s_friends[i].seen_us < (int64_t) GONE_MS * 1000;
    }
    portEXIT_CRITICAL(&s_mux);
    return n;
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
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}
