/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * touch_map.h - map calibrated sensor coordinates to panel pixels.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_TOUCH_MAP_H
#define SURFACE_TOUCH_MAP_H
#include "touch_report.h"
#include "../system/product/surface_panel.h"

/** @brief Initial calibration for the selected panel before loading the saved map. */
static inline surface_touch_map_t surface_touch_map_default(void)
{
    return (surface_touch_map_t){0, SURFACE_DESKTOP_W - 1, 0, SURFACE_DESKTOP_H - 1,
        0, SURFACE_TOUCH_ROTATED_180, SURFACE_TOUCH_ROTATED_180};
}

/** @brief Map a raw point to the panel using validated bounds, swap and inversion. */
static inline void surface_touch_map_point(const surface_touch_map_t *m,
    unsigned rx, unsigned ry, int *x, int *y)
{
    uint32_t dx = m->x_max - m->x_min, dy = m->y_max - m->y_min;
    uint32_t px = rx < m->x_min ? 0u : rx > m->x_max ? dx : rx - m->x_min;
    uint32_t py = ry < m->y_min ? 0u : ry > m->y_max ? dy : ry - m->y_min;

    if (m->swap) {
        uint32_t t = px; px = py; py = t;
        t = dx; dx = dy; dy = t;
    }
    *x = (int)((px * (SURFACE_DESKTOP_W - 1) + dx / 2u) / dx);
    *y = (int)((py * (SURFACE_DESKTOP_H - 1) + dy / 2u) / dy);
    if (m->invert_x) *x = (SURFACE_DESKTOP_W - 1) - *x;
    if (m->invert_y) *y = (SURFACE_DESKTOP_H - 1) - *y;
}

#endif
