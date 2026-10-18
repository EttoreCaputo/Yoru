#pragma once

/* The latest snapshot of the computer, as the host sends it (M4). Yoru reads
 * the host's clock and idle time (night brightness, sleep) and the Claude
 * plan's limits (agents_ui.c); the rest isn't shown. From
 * DualEye's metrics_model.h, without the faces and the two screens. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define METRICS_FAN_MAX 3
#define METRICS_CPU_TEMP_MAX_DEFAULT 100.0f
#define METRICS_GPU_TEMP_MAX_DEFAULT 100.0f
#define METRICS_FAN_RPM_MAX_DEFAULT 3000
#define METRICS_STALE_MS_DEFAULT 3000

typedef enum {
    METRICS_UI_WAITING = 0,
    METRICS_UI_LIVE,
    METRICS_UI_STALE,
    METRICS_UI_ERROR,
} metrics_ui_state_t;

/** What Claude Code is doing. Names on the wire: sleep, work, idle. */
typedef enum {
    METRICS_CLAUDE_SLEEP = 0,
    METRICS_CLAUDE_WORK,
    METRICS_CLAUDE_IDLE,
} metrics_claude_state_t;

/* Claude Code usage for the pet; the has_* flags mark the
 * optional fields the host sent. */
typedef struct {
    bool valid;
    float tokens;
    float today;
    bool has_left;
    int left_min;
    bool has_session;
    float session_pct;
    bool has_week;
    float week_pct;
    metrics_claude_state_t state;
    char model[16];
} metrics_claude_t;

typedef struct {
    bool valid;
    float temp_c;
    float usage_pct;
    float clock_ghz;
    float power_w;
    /* System RAM for the CPU, VRAM for the GPU. */
    bool mem_valid;
    float mem_used_mb;
    float mem_total_mb;
} metrics_temp_t;

typedef struct {
    char id[12];
    int rpm;
    bool valid;
} metrics_fan_t;

/* Network throughput, all interfaces but loopback, bytes per second. */
typedef struct {
    bool valid;
    float rx_bps;
    float tx_bps;
} metrics_net_t;

/* The system disk: space and throughput (bytes per second, when the host
 * can tell). */
typedef struct {
    bool valid;
    float used_gb;
    float total_gb;
    bool has_io;
    float read_bps;
    float write_bps;
} metrics_disk_t;

/* The laptop's battery; not valid on a desktop. */
typedef struct {
    bool valid;
    float pct;
    bool charging;
    /* On mains power: charging, or full. */
    bool plugged;
    /* Minutes to empty (on battery) or to full (charging). */
    bool has_mins;
    int mins;
} metrics_battery_t;

/** What a timer counts down to. Names on the wire: timer, work, break,
 * reminder (work and break are a pomodoro's). */
typedef enum {
    METRICS_TIMER_PLAIN = 0,
    METRICS_TIMER_WORK,
    METRICS_TIMER_BREAK,
    METRICS_TIMER_REMINDER,
} metrics_timer_kind_t;

/* The host's timer that ends first (or is ringing). The
 * host keeps the timers; the board counts down from `left_s` between
 * snapshots and rings while `ringing`. */
typedef struct {
    bool valid;
    metrics_timer_kind_t kind;
    bool paused;
    bool ringing;
    float left_s;
    float total_s;
    /* Other timers running besides this one. */
    int more;
    /* A pomodoro's round and how many there are; 0 for other timers. */
    int round;
    int rounds;
    char label[28];
} metrics_timer_t;

/* What's playing on the host. The host sends the cover
 * art apart (music/art, see art.h) and names it here by `art`; the board
 * counts the position on from `pos_s` between snapshots while it plays. */
typedef struct {
    bool valid;
    bool playing;
    bool has_pos;
    float pos_s;
    float dur_s;
    /* The cover the host sent for this track; 0 for none. */
    uint32_t art;
    char title[64];
    char artist[48];
} metrics_music_t;

/* What the host can tell of the person at the computer (behaviour.c): the
 * local time and how long since they last touched the mouse or the
 * keyboard. */
typedef struct {
    bool valid;
    /* Minutes since local midnight, 0..1439. */
    int minute;
    /* Seconds without input. */
    int idle_s;
} metrics_host_t;

typedef struct {
    uint32_t ts;
    uint32_t updated_ms;
    metrics_temp_t cpu;
    metrics_temp_t gpu;
    metrics_fan_t fans[METRICS_FAN_MAX];
    size_t fan_count;
    metrics_net_t net;
    metrics_disk_t disk;
    metrics_battery_t battery;
    metrics_timer_t timer;
    metrics_music_t music;
    metrics_host_t host;
    metrics_claude_t claude;
    metrics_ui_state_t state;
} metrics_snapshot_t;

void metrics_model_init(void);
void metrics_model_get(metrics_snapshot_t *out);
void metrics_model_set(const metrics_snapshot_t *in);

#ifdef __cplusplus
}
#endif
