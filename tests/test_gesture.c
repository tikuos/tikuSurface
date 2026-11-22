/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_gesture.c - touch slop, release point and double-tap landing.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gesture.h"
#include <assert.h>
#include <stdio.h>

static surface_gesture_t g;
static int x, y;

/** @brief A contact report at (@p cx, @p cy) at @p now; return the event. */
static int at(int cx, int cy, uint64_t now) { return surface_gesture_contact(&g, cx, cy, now, &x, &y); }
/** @brief A lift at @p now; return the event. */
static int lift(uint64_t now) { return surface_gesture_lift(&g, now, &x, &y); }

int main(void)
{
    /* A finger that wanders inside the slop is a tap at its press point. */
    assert(lift(1) == SURFACE_GESTURE_NONE);
    assert(at(100, 100, 1000) == SURFACE_GESTURE_DOWN && x == 100 && y == 100);
    assert(at(104, 97, 2000) == SURFACE_GESTURE_NONE);
    assert(at(110, 90, 3000) == SURFACE_GESTURE_NONE);
    assert(lift(4000) == SURFACE_GESTURE_UP && x == 100 && y == 100);

    /* A press soon after a tap and near it lands on the tap's point. */
    assert(at(118, 80, 4000 + SURFACE_TOUCH_TAP_US) == SURFACE_GESTURE_DOWN && x == 100 && y == 100);
    assert(lift(500000) == SURFACE_GESTURE_UP && x == 100 && y == 100);

    /* Leaving the slop moves the pointer from there on, and the release
     * lands where the pointer is; a press after a drag lands where it is. */
    assert(at(200, 200, 600000) == SURFACE_GESTURE_DOWN && x == 200 && y == 200);
    assert(at(211, 200, 610000) == SURFACE_GESTURE_MOVE && x == 211 && y == 200);
    assert(at(211, 200, 620000) == SURFACE_GESTURE_NONE);
    assert(at(203, 202, 630000) == SURFACE_GESTURE_MOVE && x == 203 && y == 202);
    assert(lift(640000) == SURFACE_GESTURE_UP && x == 203 && y == 202);
    assert(at(205, 204, 650000) == SURFACE_GESTURE_DOWN && x == 205 && y == 204);
    assert(lift(660000) == SURFACE_GESTURE_UP);

    /* Too late or too far from the last tap, a press lands where it is. */
    assert(at(200, 200, 660000 + SURFACE_TOUCH_TAP_US + 1) == SURFACE_GESTURE_DOWN && x == 200 && y == 200);
    assert(lift(1100000) == SURFACE_GESTURE_UP);
    assert(at(225, 200, 1200000) == SURFACE_GESTURE_DOWN && x == 225 && y == 200);
    assert(lift(1300000) == SURFACE_GESTURE_UP);
    puts("Gesture: touch slop, release point and double-tap landing passed");
    return 0;
}
