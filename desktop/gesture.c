/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * gesture.c - a finger's contact as pointer events suited to touch.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gesture.h"

/** @brief Whether (@p x, @p y) lies within @p slop pixels of (@p ax, @p ay) on both axes. */
static int near(int x, int y, int ax, int ay, int slop)
{
    return x - ax <= slop && ax - x <= slop && y - ay <= slop && ay - y <= slop;
}

int surface_gesture_contact(surface_gesture_t *g, int x, int y, uint64_t now,
    int *px, int *py)
{
    if (!g->down) {
        g->down = 1;
        g->travelled = 0;
        if (g->tap_at && now - g->tap_at <= SURFACE_TOUCH_TAP_US &&
            near(x, y, g->tap_x, g->tap_y, SURFACE_TOUCH_TAP_SLOP)) {
            x = g->tap_x;
            y = g->tap_y;
        }
        g->tap_at = 0;
        g->x0 = g->x = x;
        g->y0 = g->y = y;
        *px = x; *py = y;
        return SURFACE_GESTURE_DOWN;
    }
    if (!g->travelled) {
        if (near(x, y, g->x0, g->y0, SURFACE_TOUCH_SLOP)) return SURFACE_GESTURE_NONE;
        g->travelled = 1;
    }
    if (x == g->x && y == g->y) return SURFACE_GESTURE_NONE;
    g->x = x; g->y = y;
    *px = x; *py = y;
    return SURFACE_GESTURE_MOVE;
}

int surface_gesture_lift(surface_gesture_t *g, uint64_t now, int *px, int *py)
{
    if (!g->down) return SURFACE_GESTURE_NONE;
    g->down = 0;
    if (!g->travelled) {
        g->tap_x = g->x0; g->tap_y = g->y0;
        g->tap_at = now ? now : 1u;
    }
    *px = g->x; *py = g->y;
    return SURFACE_GESTURE_UP;
}
