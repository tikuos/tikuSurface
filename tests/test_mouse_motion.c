/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_mouse_motion.c - fractional counts, screen edges and uniform scaling.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "mouse.h"
#include <assert.h>

int main(void)
{
    surface_mouse_position_t pos = {.x = 400, .y = 240};
    surface_mouse_settings_t prefs = {.speed = 50, .size = 16};
    assert(!surface_mouse_move(&pos, prefs, 1, -1));
    assert(surface_mouse_move(&pos, prefs, 1, -1));
    assert(pos.x == 401 && pos.y == 239);
    for (int speed = 50; speed <= 400; speed += 25) {
        surface_mouse_position_t small = {.x = 400, .y = 240}, single = small;
        prefs.speed = speed;
        for (int i = 0; i < 32; i++) surface_mouse_move(&small, prefs, 1, -1);
        surface_mouse_move(&single, prefs, 32, -32);
        assert(small.x == single.x && small.y == single.y);
        assert(small.x == 400 + 32 * speed / 100 && small.y == 240 - 32 * speed / 100);
    }
    for (int i = 0; i < 4; i++) surface_mouse_move(&pos, prefs, 127, -128);
    assert(pos.x == 799 && pos.y == 0);
    assert(surface_mouse_move(&pos, prefs, -1, 1) && pos.x == 795 && pos.y == 4);
    return 0;
}
