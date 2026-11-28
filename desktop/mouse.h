/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * mouse.h - mouse movement and the desktop's tracking preferences.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_DESKTOP_MOUSE_H
#define SURFACE_DESKTOP_MOUSE_H
#include <stdint.h>
enum { SURFACE_MOUSE_MIN = 50, SURFACE_MOUSE_NORMAL = 100, SURFACE_MOUSE_MAX = 400,
       SURFACE_MOUSE_STEP = 25, SURFACE_CURSOR_NORMAL = 16, SURFACE_CURSOR_SIZES = 5 };
typedef struct { uint16_t speed; uint8_t size; } surface_mouse_settings_t;

static inline int surface_cursor_size(unsigned index)
{
    static const uint8_t pixels[SURFACE_CURSOR_SIZES] = {12, 16, 20, 24, 32};
    return index < SURFACE_CURSOR_SIZES ? pixels[index] : SURFACE_CURSOR_NORMAL;
}

/** @brief RGB565 ink at cursor-local coordinates, or -1 for transparent pixels. */
int surface_cursor_pixel(int shape, int size, int x, int y);
typedef struct { int x, y, frac_x, frac_y; } surface_mouse_position_t;
int surface_mouse_move(surface_mouse_position_t *position, surface_mouse_settings_t settings,
                       int dx, int dy);
void surface_mouse_load(void);
surface_mouse_settings_t surface_mouse_settings(void);
int surface_mouse_save(surface_mouse_settings_t settings);
#endif
