/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * gesture.h - a finger's contact as pointer events suited to touch.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_GESTURE_H
#define SURFACE_GESTURE_H
#include <stdint.h>

/** @brief Pixels a finger may wander from its press before the pointer moves. */
#define SURFACE_TOUCH_SLOP 10
/** @brief Pixels from the last tap within which a press lands on that tap. */
#define SURFACE_TOUCH_TAP_SLOP 24
/** @brief Microseconds after a tap's release within which the next press may land on it. */
#define SURFACE_TOUCH_TAP_US 400000u

/** @brief What a contact report or a lift asks the desktop for. */
enum { SURFACE_GESTURE_NONE, SURFACE_GESTURE_DOWN, SURFACE_GESTURE_MOVE, SURFACE_GESTURE_UP };

/**
 * @brief One finger's state; zero-initialized means no finger and no tap.
 * @note A press stays where it landed until the finger leaves the slop, so a
 * tap is not a short drag, and the release lands where the pointer is. A
 * press soon after a tap, and near it, lands on that tap's point, so a
 * double tap meets the desktop's double-click distance.
 */
typedef struct {
    int down, travelled;   /* a finger is on the panel; it has left the slop */
    int x0, y0, x, y;      /* where the press landed; where the pointer is */
    int tap_x, tap_y;      /* the last tap that did not travel */
    uint64_t tap_at;       /* when that tap was released, or 0 */
} surface_gesture_t;

/**
 * @brief A report with the finger at (@p x, @p y) at @p now.
 * @return DOWN, MOVE or NONE, with the pointer's position in @p px, @p py.
 */
int surface_gesture_contact(surface_gesture_t *g, int x, int y, uint64_t now,
    int *px, int *py);
/**
 * @brief The finger lifted, or its reports stopped, at @p now.
 * @return UP with the pointer's position in @p px, @p py, or NONE without a finger.
 */
int surface_gesture_lift(surface_gesture_t *g, uint64_t now, int *px, int *py);
#endif
