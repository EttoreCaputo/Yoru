#pragma once

/* The QMI8658 at 0x6B: the whole case as an input (docs/ROADMAP.md, M2,
 * "Motion"). A task reads the accelerometer and the gyroscope 100 times a
 * second and turns them into a few gestures, handed to a callback the way
 * ears.c hands its sounds to pet.c, plus a lean for the gaze and the way
 * the badge is held, for the automatic rotation. Nothing is recorded.
 *
 * Axes are the screen's, unrotated: x to the right, y down, z out of the
 * glass towards you. Which way the chip's z points is learnt from the
 * touches (the side you touch faces up); how its x and y lie on the board
 * is fixed in motion.c. */

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* Picked up after resting a while, and put down again. */
    MOTION_LIFTED,
    MOTION_PUT_DOWN,
    /* Shaken; and shaken again and again. */
    MOTION_SHAKEN,
    MOTION_SHAKEN_LONG,
    /* A knock on the case or the desk next to it, and two in a row. */
    MOTION_KNOCK,
    MOTION_DOUBLE_KNOCK,
    /* Rocked slowly back and forth, a lullaby. */
    MOTION_ROCKED,
    /* Turned over on the desk, and back. */
    MOTION_FACE_DOWN,
    MOTION_FACE_UP,
    /* Falling, and the landing after it. */
    MOTION_DROPPED,
    MOTION_LANDED,
    /* Turned fast on the desk. */
    MOTION_SPUN,
    MOTION_COUNT,
} motion_event_t;

/** From the motion task: keep it short (take a lock, set a flag). */
typedef void (*motion_cb_t)(motion_event_t event);

/** Find the IMU and start reading it. ESP_ERR_NOT_FOUND without one. */
esp_err_t motion_init(motion_cb_t cb);
bool motion_present(void);

/** How the case leans away from how it has been resting lately, -1..1 on
 * each axis (screen pixels' directions, unrotated), with a dead zone so a
 * sloped desk or a stand doesn't count. Safe from any task. */
void motion_lean(float *x, float *y);

/** Which quarter turn (0..3, clockwise) is "up" while the badge stands or is
 * held upright, or -1 while it lies flat. */
int motion_upright(void);

/** Someone is touching the screen: the side facing up is the glass, and a
 * knock felt now is the finger. From any task. */
void motion_touched(void);

/** Gravity in g and the last turn rate in degrees per second, screen axes,
 * for get_state and for checking the axes on a new board. */
void motion_raw(float g[3], float w[3]);

const char *motion_event_name(motion_event_t event);

#ifdef __cplusplus
}
#endif
