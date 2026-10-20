#include "lvgl_port.h"

#include <string.h>
#include <sys/lock.h>

#include "board.h"
#include "board_touch.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* Against tearing, a frame reaches the panel all at once, right after it
 * starts scanning out (the TE pin):
 * - LVGL draws the whole frame first, into a screen-sized buffer in PSRAM
 *   (direct mode, 424 KB), instead of 20-line strips sent while the next
 *   ones are still being drawn, over several of the panel's refreshes.
 * - On the frame's last area flush_cb waits for TE and sends every changed
 *   area, top to bottom, through two small bounce buffers in internal DMA
 *   RAM (18 KB each, no larger than the panel's biggest transfer,
 *   board_display.c): swapped to big-endian and turned on the way.
 * Started at TE, the writes run ahead of the scan for Yoru-sized areas and
 * behind it for full-width ones, so either way each area shows whole. */
#define LVGL_BOUNCE_LINES 20
#define LVGL_MAX_AREAS 32
/* The panel refreshes at about 60 Hz: past this, TE is missing. */
#define LVGL_TE_TIMEOUT_MS 25
#define LVGL_TICK_PERIOD_MS 2
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define LVGL_TASK_PRIORITY 5

static const char *TAG = "lvgl_port";
static _lock_t s_lvgl_lock;
static lv_display_t *s_disp;
static lv_indev_t *s_touch;
static uint16_t *s_fb;
static uint16_t *s_bounce[2];
static size_t s_bounce_px;
static int s_next_bounce;
/* A bounce buffer free to fill: given back when its transfer is done. */
static SemaphoreHandle_t s_bounce_free;
static SemaphoreHandle_t s_te;
static bool s_te_seen;
/* The frame's changed areas, until its last one is drawn. */
static lv_area_t s_areas[LVGL_MAX_AREAS];
static int s_nareas;
/* Time spent waiting for TE this window: idle, not work. */
static int64_t s_te_wait_us;
static volatile int64_t s_te_last_us;
static volatile int32_t s_te_period_us;

static int64_t s_window_start_us;
static int64_t s_window_busy_us;
static uint32_t s_window_max_us;
static lvgl_port_stats_t s_stats;
static portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;

static void account(int64_t start_us, int64_t end_us)
{
    uint32_t took = (uint32_t) (end_us - start_us);
    s_window_busy_us += took;
    if (took > s_window_max_us) {
        s_window_max_us = took;
    }
    int64_t span = end_us - s_window_start_us;
    if (span >= LVGL_PORT_STATS_WINDOW_MS * 1000) {
        portENTER_CRITICAL(&s_stats_mux);
        s_stats.busy_pct = 100.0f * (float) s_window_busy_us / (float) span;
        s_stats.max_us = s_window_max_us;
        portEXIT_CRITICAL(&s_stats_mux);
        static bool s_logged;
        if (!s_logged) {
            s_logged = true;
            const int32_t te = lvgl_port_te_period_us();
            ESP_LOGI(TAG, "panel refresh %.1f Hz, LVGL busy %.0f%%, longest run %lu us",
                     te > 0 ? 1e6f / (float) te : 0.0f, s_stats.busy_pct, (unsigned long) s_stats.max_us);
        }
        s_window_start_us = end_us;
        s_window_busy_us = 0;
        s_window_max_us = 0;
    }
}

void lvgl_port_get_stats(lvgl_port_stats_t *out)
{
    portENTER_CRITICAL(&s_stats_mux);
    *out = s_stats;
    portEXIT_CRITICAL(&s_stats_mux);
}

static bool bounce_done(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_bounce_free, &woken);
    return woken == pdTRUE;
}

static void IRAM_ATTR te_isr(void *arg)
{
    const int64_t now = esp_timer_get_time();
    s_te_period_us = (int32_t) (now - s_te_last_us);
    s_te_last_us = now;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_te, &woken);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/* Until the panel starts a new refresh. Without TE (not wired, or not
 * pulsing) it gives up after a frame and the writes go out at once. */
static void wait_te(void)
{
    if (s_te == NULL) {
        return;
    }
    const int64_t start = esp_timer_get_time();
    xSemaphoreTake(s_te, 0); // a pulse from earlier is stale
    const bool got = xSemaphoreTake(s_te, pdMS_TO_TICKS(LVGL_TE_TIMEOUT_MS)) == pdTRUE;
    s_te_wait_us += esp_timer_get_time() - start;
    if (got && !s_te_seen) {
        s_te_seen = true;
    } else if (!got && s_te_seen) {
        s_te_seen = false;
        ESP_LOGW(TAG, "no TE from the panel: frames go out unsynchronised");
    }
}

/* Copy `w` x `h` pixels from the frame buffer at (x, y) to `dst`, swapped
 * to big-endian. Rows start even and are an even number of pixels wide
 * (the rounder), so they go two pixels at a time. */
static void copy_swapped(uint16_t *dst, int32_t x, int32_t y, int32_t w, int32_t h)
{
    uint32_t *d = (uint32_t *) dst;
    for (int32_t row = 0; row < h; row++) {
        const uint32_t *src = (const uint32_t *) (s_fb + (y + row) * BOARD_LCD_H_RES + x);
        for (int32_t i = 0; i < w / 2; i++) {
            const uint32_t v = src[i];
            *d++ = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
        }
    }
}

/* One band of a changed area (logical coordinates) to the panel: turned
 * if the screen is, through the next bounce buffer. */
static void send_band(lv_display_t *disp, const lv_area_t *band)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t) lv_display_get_user_data(disp);
    const lv_display_rotation_t rot = lv_display_get_rotation(disp);
    const int32_t w = lv_area_get_width(band), h = lv_area_get_height(band);
    uint16_t *buf = s_bounce[s_next_bounce];
    s_next_bounce ^= 1;
    xSemaphoreTake(s_bounce_free, portMAX_DELAY);

    lv_area_t out = *band;
    if (rot == LV_DISPLAY_ROTATION_0) {
        copy_swapped(buf, band->x1, band->y1, w, h);
    } else {
        const uint32_t px = sizeof(uint16_t);
        const bool swap = rot == LV_DISPLAY_ROTATION_90 || rot == LV_DISPLAY_ROTATION_270;
        const uint16_t *src = s_fb + band->y1 * BOARD_LCD_H_RES + band->x1;
        lv_draw_sw_rotate(src, buf, w, h, BOARD_LCD_H_RES * px, (swap ? h : w) * px, rot, LV_COLOR_FORMAT_RGB565);
        lv_draw_sw_rgb565_swap(buf, (uint32_t) (w * h));
        lv_display_rotate_area(disp, &out);
    }
    if (esp_lcd_panel_draw_bitmap(panel, out.x1, out.y1, out.x2 + 1, out.y2 + 1, buf) != ESP_OK) {
        xSemaphoreGive(s_bounce_free); // no transfer, no callback
    }
}

/* An area cut into bands that are rows on the panel, sent top to bottom
 * there, so the writes go the way the panel scans. */
static void send_area(lv_display_t *disp, const lv_area_t *a)
{
    const lv_display_rotation_t rot = lv_display_get_rotation(disp);
    // Turned a quarter, the panel's rows are the drawing's columns.
    const bool cols = rot == LV_DISPLAY_ROTATION_90 || rot == LV_DISPLAY_ROTATION_270;
    const int32_t len = cols ? lv_area_get_height(a) : lv_area_get_width(a);
    const int32_t span = cols ? lv_area_get_width(a) : lv_area_get_height(a);
    const int32_t step = LV_MAX(2, (int32_t) (s_bounce_px / (size_t) len) & ~1);
    const int32_t n = (span + step - 1) / step;

    // Which end of the area is the panel's top.
    lv_area_t first, last;
    if (cols) {
        first = (lv_area_t) {a->x1, a->y1, a->x1, a->y2};
        last = (lv_area_t) {a->x2, a->y1, a->x2, a->y2};
    } else {
        first = (lv_area_t) {a->x1, a->y1, a->x2, a->y1};
        last = (lv_area_t) {a->x1, a->y2, a->x2, a->y2};
    }
    lv_display_rotate_area(disp, &first);
    lv_display_rotate_area(disp, &last);
    const bool reverse = first.y1 > last.y1;

    for (int32_t k = 0; k < n; k++) {
        const int32_t i = reverse ? n - 1 - k : k;
        lv_area_t band = *a;
        if (cols) {
            band.x1 = a->x1 + i * step;
            band.x2 = LV_MIN(a->x2, band.x1 + step - 1);
        } else {
            band.y1 = a->y1 + i * step;
            band.y2 = LV_MIN(a->y2, band.y1 + step - 1);
        }
        send_band(disp, &band);
    }
}

/* The panel's top edge of a logical area. */
static int32_t panel_top(lv_display_t *disp, const lv_area_t *a)
{
    lv_area_t out = *a;
    lv_display_rotate_area(disp, &out);
    return out.y1;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    // Direct mode: LVGL has drawn `area` into s_fb, and draws the frame's
    // other areas next. Only the last one sends them all.
    if (s_nareas < LVGL_MAX_AREAS) {
        s_areas[s_nareas++] = *area;
    } else {
        // Out of room: the last one grows to cover this one too.
        lv_area_t *last = &s_areas[LVGL_MAX_AREAS - 1];
        last->x1 = LV_MIN(last->x1, area->x1);
        last->y1 = LV_MIN(last->y1, area->y1);
        last->x2 = LV_MAX(last->x2, area->x2);
        last->y2 = LV_MAX(last->y2, area->y2);
    }
    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }
    // Top to bottom on the panel, as it scans.
    for (int i = 1; i < s_nareas; i++) {
        const lv_area_t a = s_areas[i];
        const int32_t top = panel_top(disp, &a);
        int j = i - 1;
        while (j >= 0 && panel_top(disp, &s_areas[j]) > top) {
            s_areas[j + 1] = s_areas[j];
            j--;
        }
        s_areas[j + 1] = a;
    }
    wait_te();
    for (int i = 0; i < s_nareas; i++) {
        send_area(disp, &s_areas[i]);
    }
    s_nareas = 0;
    // The bounce buffers may still be on the wire; s_fb is free to draw in.
    lv_display_flush_ready(disp);
}

/* The CO5300 only takes areas that start on an even pixel and are an even
 * number of pixels wide and tall. */
static void round_area_cb(lv_event_t *e)
{
    lv_area_t *area = lv_event_get_invalidated_area(e);
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x;
    uint16_t y;
    if (board_touch_read(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* TE pulses at the start of each of the panel's refreshes (0x35 in
 * board_display.c). Not fatal: without it the frames just aren't synced. */
static void te_init(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_LCD_TE_GPIO,
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    s_te = xSemaphoreCreateBinary();
    esp_err_t err = s_te != NULL ? gpio_config(&io) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        // The touch screen has usually installed the ISR service already.
        err = gpio_isr_handler_add(BOARD_LCD_TE_GPIO, te_isr, NULL);
        if (err == ESP_ERR_INVALID_STATE && gpio_install_isr_service(0) == ESP_OK) {
            err = gpio_isr_handler_add(BOARD_LCD_TE_GPIO, te_isr, NULL);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no TE (%s): frames go out unsynchronised", esp_err_to_name(err));
        if (s_te != NULL) {
            vSemaphoreDelete(s_te);
            s_te = NULL;
        }
    }
}

int32_t lvgl_port_te_period_us(void)
{
    return esp_timer_get_time() - s_te_last_us < 100000 ? s_te_period_us : 0;
}

static void tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_task(void *arg)
{
    const uint32_t min_delay_ms = 1000 / configTICK_RATE_HZ;
    s_window_start_us = esp_timer_get_time();
    while (true) {
        _lock_acquire(&s_lvgl_lock);
        int64_t start = esp_timer_get_time();
        s_te_wait_us = 0;
        uint32_t delay_ms = lv_timer_handler();
        account(start, esp_timer_get_time() - s_te_wait_us);
        _lock_release(&s_lvgl_lock);
        if (delay_ms < min_delay_ms) {
            delay_ms = min_delay_ms;
        } else if (delay_ms > 500) {
            delay_ms = 500;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void lvgl_port_set_rotation(int quarter)
{
    quarter &= 3;
    if ((int) lv_display_get_rotation(s_disp) != quarter) {
        ESP_LOGI(TAG, "screen turned %d degrees", quarter * 90);
        lv_display_set_rotation(s_disp, (lv_display_rotation_t) quarter);
        lv_obj_invalidate(lv_display_get_screen_active(s_disp));
    }
}

int lvgl_port_rotation(void)
{
    return (int) lv_display_get_rotation(s_disp);
}

void lvgl_port_lock(void)
{
    _lock_acquire(&s_lvgl_lock);
}

void lvgl_port_unlock(void)
{
    _lock_release(&s_lvgl_lock);
}

esp_err_t lvgl_port_init(const board_lcd_t *lcd, lv_display_t **out_display)
{
    lv_init();

    lv_display_t *disp = lv_display_create(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    const size_t fb_size = BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(lv_color16_t);
    s_fb = heap_caps_aligned_alloc(64, fb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_fb, ESP_ERR_NO_MEM, TAG, "frame buffer");
    memset(s_fb, 0, fb_size);
    s_bounce_px = BOARD_LCD_H_RES * LVGL_BOUNCE_LINES;
    for (int i = 0; i < 2; i++) {
        s_bounce[i] = spi_bus_dma_memory_alloc(BOARD_LCD_SPI_HOST, s_bounce_px * sizeof(uint16_t), 0);
        ESP_RETURN_ON_FALSE(s_bounce[i], ESP_ERR_NO_MEM, TAG, "bounce buffers");
    }
    s_bounce_free = xSemaphoreCreateCounting(2, 2);
    ESP_RETURN_ON_FALSE(s_bounce_free, ESP_ERR_NO_MEM, TAG, "bounce semaphore");
    s_disp = disp;

    lv_display_set_buffers(disp, s_fb, NULL, fb_size, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_user_data(disp, lcd->panel);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_add_event_cb(disp, round_area_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = bounce_done,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(lcd->io, &cbs, disp), TAG, "io callbacks");
    te_init();

    lv_indev_t *touch = lv_indev_create();
    s_touch = touch;
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read_cb);
    lv_indev_set_display(touch, disp);

    const esp_timer_create_args_t tick_args = {
        .callback = &tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &tick_timer), TAG, "tick timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000), TAG, "tick start");

    // Core 0: core 1 is kept for the audio front end and the wake word (M3).
    BaseType_t ok = xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_TASK_STACK_SIZE, NULL,
                                            LVGL_TASK_PRIORITY, NULL, 0);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create");

    *out_display = disp;
    return ESP_OK;
}

void lvgl_port_set_touch(bool on)
{
    if (s_touch != NULL) {
        lv_indev_enable(s_touch, on);
    }
}
