/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * surface_panel.h - native desktop panel dimensions and touch orientation.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_PANEL_H
#define SURFACE_PANEL_H
#include <stdint.h>
#if defined(TIKU_BOARD_ESP32_P4X_FUNCTION_EV_BOARD)
#define SURFACE_DESKTOP_W 1024
#define SURFACE_DESKTOP_H 600
#define SURFACE_TOUCH_ROTATED_180 1
#else
#define SURFACE_DESKTOP_W 800
#define SURFACE_DESKTOP_H 480
#define SURFACE_TOUCH_ROTATED_180 0
#endif
#define SURFACE_SCANOUT_BYTES (SURFACE_DESKTOP_W * SURFACE_DESKTOP_H * sizeof(uint16_t))
#endif
