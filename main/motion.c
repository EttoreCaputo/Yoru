#include "motion.h"

#include <math.h>
#include <string.h>

#include "board.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "motion";

/* QMI8658 registers (QST's datasheet). */
#define IMU_ADDR 0x6B
#define IMU_WHO_AM_I 0x00
#define IMU_CTRL1 0x02 // bit 6: address auto-increment
#define IMU_CTRL2 0x03 // accelerometer: range, rate
#define IMU_CTRL3 0x04 // gyroscope: range, rate
#define IMU_CTRL7 0x08 // bit 0 accelerometer, bit 1 gyroscope on
#define IMU_AX_L 0x35  // ax ay az gx gy gz, 16 bits each, little-endian
#define IMU_RESET 0x60
#define QMI8658_ID 0x05
/* ±8 g and ±1024 °/s at 250 Hz: knocks and shakes reach a few g, a spin
 * a few hundred degrees a second. */
#define ACC_CFG 0x25
#define GYR_CFG 0x65
#define ACC_LSB_G 4096.0f
#define GYR_LSB_DPS 32.0f

#define PERIOD_MS 10

/* How the chip lies under the glass: the screen's x and y from its axes.
 * Check on a new board with get_state's motion.g: lying flat it reads
 * z ≈ +1; tilted to the right x > 0; with the top edge raised y > 0. */
#define SCREEN_X(v) (v[0])
#define SCREEN_Y(v) (-(v)[1])

#define NVS_NAMESPACE "yoru"
#define ZSIGN_KEY "imu_z"

/* Thresholds, in g, °/s and ms. Tuned by hand; in M6 they become data. */
#define ACTIVE_G 0.12f
#define ACTIVE_DPS 25.0f
#define STILL_G 0.03f
#define STILL_DPS 4.0f
#define LIFT_REST_MS 2000
#define LIFT_MOVE_MS 250
#define PUT_DOWN_MS 1000
#define KNOCK_G 0.30f
#define KNOCK_QUIET_MS 150
#define KNOCK_CHECK_MS 120
#define KNOCK_MAX_ACTIVE_MS 80
#define KNOCK_TOUCH_MS 400
#define DOUBLE_KNOCK_MS 800
#define SHAKE_G 1.0f
#define SHAKE_RESET_G 0.6f
#define SHAKE_PEAKS 4
#define SHAKE_WINDOW_MS 1200
#define SHAKE_COOL_MS 1500
#define SHAKE_LONG_COUNT 3
#define SHAKE_LONG_WINDOW_MS 6000
#define FALL_G 0.35f
#define FALL_MS 50
#define SPIN_DPS 200.0f
#define SPIN_MS 400
#define SPIN_COOL_MS 3000
#define FACE_DOWN_G (-0.8f)
#define FACE_DOWN_MS 1500
#define FACE_UP_G (-0.4f)
#define FACE_UP_MS 500
#define ROCK_G 0.07f
#define ROCK_MIN_MS 250
#define ROCK_MAX_MS 1500
#define ROCK_CROSSINGS 6
#define ROCK_COOL_MS 8000
#define LEAN_DEAD_G 0.10f
#define LEAN_FULL_G 0.40f
#define UPRIGHT_FLAT_G 0.55f
#define UPRIGHT_MS 800
/* After a landing the case still rings: nothing else for a moment. */
#define LAND_QUIET_MS 1000

static const char *const NAMES[MOTION_COUNT] = {
    "lifted", "put_down", "shaken", "shaken_long", "knock", "double_knock",
    "rocked", "face_down", "face_up", "dropped", "landed", "spun",
};

typedef struct {
    float mid;   // where it rocks around
    int sign;    // the side it was last seen on, beyond the hysteresis
    int64_t last_ms;
    int count;
} rock_axis_t;

static i2c_master_dev_handle_t s_dev;
static motion_cb_t s_cb;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
/* What other tasks read, under s_mux. */
static float s_lean[2];
static int s_upright = -1;
static float s_g_out[3], s_w_out[3];
static int64_t s_touched_ms = -100000;
/* +1: the chip's z points out of the glass; -1: the other way. Learnt from
 * touches; until then the face-down gesture stays off. */
static volatile int s_zsign = 1;
static volatile bool s_zsign_known;
static volatile bool s_zsign_dirty;
static float s_raw_z;

static struct {
    int64_t now;
    float g[3], rest[3];
    int64_t active_ms, gap_ms, still_ms, still_before;
    bool held, burst_after_rest;
    /* Knocks. */
    bool knock_pending;
    int64_t knock_at, last_knock, max_active;
    /* Shakes. */
    bool shake_high;
    int peaks;
    int64_t first_peak, shake_cool, first_shake;
    int shakes;
    /* Falling. */
    int fall_ms;
    bool falling;
    int64_t quiet_until;
    /* Spin, face down, rocking, upright. */
    int spin_ms;
    int64_t spin_cool;
    int face_ms;
    bool face_down;
    rock_axis_t rock[2];
    int64_t rock_cool;
    int up_candidate, up_ms;
} m;

static void emit(motion_event_t ev)
{
    ESP_LOGI(TAG, "%s", NAMES[ev]);
    if (s_cb != NULL) {
        s_cb(ev);
    }
}

static float norm3(const float v[3])
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    const uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_dev, b, sizeof(b), 50);
}

static bool read_sample(float a[3], float w[3])
{
    uint8_t reg = IMU_AX_L;
    uint8_t d[12];
    if (i2c_master_transmit_receive(s_dev, &reg, 1, d, sizeof(d), 20) != ESP_OK) {
        return false;
    }
    float ca[3], cw[3];
    for (int i = 0; i < 3; i++) {
        ca[i] = (float) (int16_t) (d[2 * i] | (d[2 * i + 1] << 8)) / ACC_LSB_G;
        cw[i] = (float) (int16_t) (d[6 + 2 * i] | (d[7 + 2 * i] << 8)) / GYR_LSB_DPS;
    }
    s_raw_z = ca[2];
    const float zs = (float) s_zsign;
    a[0] = SCREEN_X(ca);
    a[1] = SCREEN_Y(ca);
    a[2] = ca[2] * zs;
    w[0] = SCREEN_X(cw);
    w[1] = SCREEN_Y(cw);
    w[2] = cw[2] * zs;
    return true;
}

static void detect_knock(float dyn, int64_t prev_still)
{
    if (!m.knock_pending && dyn > KNOCK_G && prev_still >= KNOCK_QUIET_MS) {
        m.knock_pending = true;
        m.knock_at = m.now;
        m.max_active = 0;
    }
    if (!m.knock_pending) {
        return;
    }
    if (m.active_ms > m.max_active) {
        m.max_active = m.active_ms;
    }
    if (m.now - m.knock_at < KNOCK_CHECK_MS) {
        return;
    }
    m.knock_pending = false;
    portENTER_CRITICAL(&s_mux);
    const int64_t touched = s_touched_ms;
    portEXIT_CRITICAL(&s_mux);
    // Brief, and not a finger on the glass.
    if (m.max_active > KNOCK_MAX_ACTIVE_MS || m.now - touched < KNOCK_TOUCH_MS || m.held) {
        return;
    }
    if (m.last_knock != 0 && m.now - m.last_knock < DOUBLE_KNOCK_MS) {
        m.last_knock = 0;
        emit(MOTION_DOUBLE_KNOCK);
    } else {
        m.last_knock = m.now;
        emit(MOTION_KNOCK);
    }
}

static void detect_shake(float dyn)
{
    if (!m.shake_high && dyn > SHAKE_G) {
        m.shake_high = true;
        if (m.now - m.first_peak > SHAKE_WINDOW_MS) {
            m.first_peak = m.now;
            m.peaks = 0;
        }
        if (++m.peaks >= SHAKE_PEAKS && m.now >= m.shake_cool) {
            m.peaks = 0;
            m.shake_cool = m.now + SHAKE_COOL_MS;
            if (m.now - m.first_shake > SHAKE_LONG_WINDOW_MS) {
                m.first_shake = m.now;
                m.shakes = 0;
            }
            if (++m.shakes >= SHAKE_LONG_COUNT) {
                m.shakes = 0;
                emit(MOTION_SHAKEN_LONG);
            } else {
                emit(MOTION_SHAKEN);
            }
        }
    } else if (m.shake_high && dyn < SHAKE_RESET_G) {
        m.shake_high = false;
    }
}

/* Slow swings either side of a middle that follows them: three full ones
 * at a lullaby's pace. A shake swings too fast and resets the count. */
static void detect_rock(void)
{
    for (int i = 0; i < 2; i++) {
        rock_axis_t *r = &m.rock[i];
        r->mid += (m.g[i] - r->mid) * 0.0066f;
        const float d = m.g[i] - r->mid;
        const int side = d > ROCK_G ? 1 : (d < -ROCK_G ? -1 : 0);
        if (side == 0 || side == r->sign) {
            continue;
        }
        const int64_t gap = m.now - r->last_ms;
        r->count = (r->sign != 0 && gap >= ROCK_MIN_MS && gap <= ROCK_MAX_MS) ? r->count + 1 : 1;
        r->sign = side;
        r->last_ms = m.now;
        if (r->count >= ROCK_CROSSINGS && m.now >= m.rock_cool) {
            r->count = 0;
            m.rock_cool = m.now + ROCK_COOL_MS;
            emit(MOTION_ROCKED);
        }
    }
}

static void detect_upright(void)
{
    const float flat = fabsf(m.g[2]);
    const float plane = hypotf(m.g[0], m.g[1]);
    int q = -1;
    if (flat < UPRIGHT_FLAT_G && plane > 0.7f) {
        // The edge gravity pulls towards: down 0, right 1, up 2, left 3,
        // within 35° of it.
        const float ang = atan2f(m.g[0], m.g[1]) * 180.0f / (float) M_PI; // 0 = +y
        const float a = ang < 0 ? ang + 360 : ang;
        const int nearest = ((int) lroundf(a / 90.0f)) % 4;
        const float off = fabsf(a - (float) nearest * 90.0f);
        if (off < 35 || off > 325) {
            // atan2(x, y) grows towards +x, which is the right edge: turn 1.
            q = nearest;
        } else {
            return; // in between: keep what it was
        }
    }
    if (q != m.up_candidate) {
        m.up_candidate = q;
        m.up_ms = 0;
        return;
    }
    m.up_ms += PERIOD_MS;
    if (m.up_ms == UPRIGHT_MS) {
        portENTER_CRITICAL(&s_mux);
        s_upright = q;
        portEXIT_CRITICAL(&s_mux);
    }
}

static float lean_axis(float d)
{
    const float a = fabsf(d);
    if (a < LEAN_DEAD_G) {
        return 0;
    }
    const float v = fminf(1, (a - LEAN_DEAD_G) / (LEAN_FULL_G - LEAN_DEAD_G));
    return d < 0 ? -v : v;
}

static void step(const float a[3], const float w[3])
{
    m.now += PERIOD_MS;
    const float mag = norm3(a);
    for (int i = 0; i < 3; i++) {
        m.g[i] += (a[i] - m.g[i]) * 0.08f;
        m.rest[i] += (m.g[i] - m.rest[i]) * 0.0008f;
    }
    const float dv[3] = {a[0] - m.g[0], a[1] - m.g[1], a[2] - m.g[2]};
    const float dyn = norm3(dv);
    const float wmag = norm3(w);

    // Free fall reads nearly 0 g; the landing is the next hard reading.
    if (mag < FALL_G) {
        m.fall_ms += PERIOD_MS;
        if (m.fall_ms == FALL_MS && !m.falling) {
            m.falling = true;
            emit(MOTION_DROPPED);
        }
    } else {
        m.fall_ms = 0;
        if (m.falling && mag > 0.7f) {
            m.falling = false;
            m.quiet_until = m.now + LAND_QUIET_MS;
            m.knock_pending = false;
            emit(MOTION_LANDED);
        }
    }

    // Moving or resting. A short gap doesn't end a movement.
    const bool active = dyn > ACTIVE_G || wmag > ACTIVE_DPS;
    if (active) {
        if (m.active_ms == 0) {
            m.still_before = m.still_ms;
        }
        m.active_ms += PERIOD_MS;
        m.gap_ms = 0;
    } else if ((m.gap_ms += PERIOD_MS) > 60) {
        m.active_ms = 0;
    }
    const int64_t prev_still = m.still_ms;
    m.still_ms = (dyn < STILL_G && wmag < STILL_DPS) ? m.still_ms + PERIOD_MS : 0;

    if (m.now < m.quiet_until || m.falling) {
        return;
    }
    if (m.active_ms == 100) {
        m.burst_after_rest = m.still_before >= LIFT_REST_MS;
    }
    if (!m.held && m.burst_after_rest && m.active_ms >= LIFT_MOVE_MS) {
        m.held = true;
        m.burst_after_rest = false;
        emit(MOTION_LIFTED);
    }
    if (m.held && m.still_ms >= PUT_DOWN_MS) {
        m.held = false;
        emit(MOTION_PUT_DOWN);
    }

    detect_knock(dyn, prev_still);
    detect_shake(dyn);

    // Spun on the desk: fast around the axis through the glass.
    if (fabsf(w[2]) > SPIN_DPS) {
        m.spin_ms += PERIOD_MS;
        if (m.spin_ms >= SPIN_MS && m.now >= m.spin_cool) {
            m.spin_cool = m.now + SPIN_COOL_MS;
            emit(MOTION_SPUN);
        }
    } else {
        m.spin_ms = 0;
    }

    if (s_zsign_known) {
        const bool want_down = !m.face_down && m.g[2] < FACE_DOWN_G;
        const bool want_up = m.face_down && m.g[2] > FACE_UP_G;
        m.face_ms = (want_down || want_up) ? m.face_ms + PERIOD_MS : 0;
        if (want_down && m.face_ms >= FACE_DOWN_MS) {
            m.face_down = true;
            m.face_ms = 0;
            emit(MOTION_FACE_DOWN);
        } else if (want_up && m.face_ms >= FACE_UP_MS) {
            m.face_down = false;
            m.face_ms = 0;
            emit(MOTION_FACE_UP);
        }
    }

    detect_rock();
    detect_upright();

    portENTER_CRITICAL(&s_mux);
    s_lean[0] = lean_axis(m.g[0] - m.rest[0]);
    s_lean[1] = lean_axis(m.g[1] - m.rest[1]);
    memcpy(s_g_out, m.g, sizeof(s_g_out));
    memcpy(s_w_out, w, sizeof(s_w_out));
    portEXIT_CRITICAL(&s_mux);
}

static void save_zsign(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_i8(nvs, ZSIGN_KEY, (int8_t) s_zsign);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void load_zsign(void)
{
    nvs_handle_t nvs;
    int8_t v;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        if (nvs_get_i8(nvs, ZSIGN_KEY, &v) == ESP_OK && (v == 1 || v == -1)) {
            s_zsign = v;
            s_zsign_known = true;
        }
        nvs_close(nvs);
    }
}

static void motion_task(void *arg)
{
    float a[3], w[3];
    // Start from a real reading, so it doesn't look like a fall: for a moment
    // after the reset the chip reads all zeros.
    vTaskDelay(pdMS_TO_TICKS(200));
    while (!read_sample(a, w) || norm3(a) < 0.5f) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    memcpy(m.g, a, sizeof(m.g));
    memcpy(m.rest, a, sizeof(m.rest));
    m.up_candidate = -2;
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(PERIOD_MS));
        if (s_zsign_dirty) {
            // The flash is written here, not from the touch's task. Its
            // stack is internal for that (0.3.1).
            s_zsign_dirty = false;
            save_zsign();
            // The axes flipped: start the averages again.
            m.g[2] = -m.g[2];
            m.rest[2] = -m.rest[2];
        }
        if (read_sample(a, w)) {
            step(a, w);
        }
    }
}

esp_err_t motion_init(motion_cb_t cb)
{
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(board_i2c_bus(&bus), TAG, "i2c");
    if (i2c_master_probe(bus, IMU_ADDR, 50) != ESP_OK) {
        ESP_LOGW(TAG, "no QMI8658 at 0x%02x", IMU_ADDR);
        return ESP_ERR_NOT_FOUND;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = IMU_ADDR,
        .scl_speed_hz = 400 * 1000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "dev");
    uint8_t reg = IMU_WHO_AM_I, id = 0;
    i2c_master_transmit_receive(s_dev, &reg, 1, &id, 1, 50);
    if (id != QMI8658_ID) {
        ESP_LOGW(TAG, "who-am-i 0x%02x, expected 0x%02x; trying anyway", id, QMI8658_ID);
    }
    wr(IMU_RESET, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(wr(IMU_CTRL1, 0x40), TAG, "ctrl1");
    ESP_RETURN_ON_ERROR(wr(IMU_CTRL2, ACC_CFG), TAG, "ctrl2");
    ESP_RETURN_ON_ERROR(wr(IMU_CTRL3, GYR_CFG), TAG, "ctrl3");
    ESP_RETURN_ON_ERROR(wr(IMU_CTRL7, 0x03), TAG, "ctrl7");
    vTaskDelay(pdMS_TO_TICKS(30));
    load_zsign();
    s_cb = cb;
    // It writes the flash once in a while (save_zsign): internal stack.
    if (xTaskCreatePinnedToCore(motion_task, "motion", 3072, NULL, 4, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "QMI8658 ready, glass side %s", s_zsign_known ? (s_zsign > 0 ? "+z" : "-z") : "not known yet");
    return ESP_OK;
}

bool motion_present(void)
{
    return s_dev != NULL;
}

void motion_lean(float *x, float *y)
{
    portENTER_CRITICAL(&s_mux);
    *x = s_lean[0];
    *y = s_lean[1];
    portEXIT_CRITICAL(&s_mux);
}

int motion_upright(void)
{
    portENTER_CRITICAL(&s_mux);
    int q = s_upright;
    portEXIT_CRITICAL(&s_mux);
    return q;
}

void motion_touched(void)
{
    // On the motion task's clock, which knocks are timed by.
    portENTER_CRITICAL(&s_mux);
    s_touched_ms = m.now;
    portEXIT_CRITICAL(&s_mux);
    // Lying flat while touched: the glass faces up.
    const float z = s_raw_z;
    if (fabsf(z) > 0.75f) {
        const int sign = z > 0 ? 1 : -1;
        if (!s_zsign_known || sign != s_zsign) {
            s_zsign = sign;
            s_zsign_known = true;
            s_zsign_dirty = true;
            ESP_LOGI(TAG, "the glass is on the chip's %s side", sign > 0 ? "+z" : "-z");
        }
    }
}

void motion_raw(float g[3], float w[3])
{
    portENTER_CRITICAL(&s_mux);
    memcpy(g, s_g_out, sizeof(s_g_out));
    memcpy(w, s_w_out, sizeof(s_w_out));
    portEXIT_CRITICAL(&s_mux);
}

const char *motion_event_name(motion_event_t event)
{
    return event < MOTION_COUNT ? NAMES[event] : "?";
}
