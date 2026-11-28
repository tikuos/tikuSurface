/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * mouse_motion.c - scale relative counts into panel coordinates.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../system/product/surface_panel.h"
#include "mouse.h"


static int advance(int pos, int delta, int gain, int limit, int *fraction)
{
    int hundredths = delta * gain + *fraction;
    pos += hundredths / 100;
    *fraction = hundredths % 100;
    if (pos < 0 || (pos == 0 && *fraction < 0)) {
        *fraction = 0;
        return 0;
    }
    if (pos > limit || (pos == limit && *fraction > 0)) {
        *fraction = 0;
        return limit;
    }
    return pos;
}

int surface_mouse_move(surface_mouse_position_t *p, surface_mouse_settings_t prefs, int dx, int dy)
{
    int gain = prefs.speed;
    if (gain < SURFACE_MOUSE_MIN || gain > SURFACE_MOUSE_MAX) gain = SURFACE_MOUSE_NORMAL;
    int x = p->x, y = p->y;
    p->x = advance(x, dx, gain, (SURFACE_DESKTOP_W - 1), &p->frac_x);
    p->y = advance(y, dy, gain, (SURFACE_DESKTOP_H - 1), &p->frac_y);
    return x != p->x || y != p->y;
}
