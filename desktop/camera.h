/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * camera.h - Camera, the desktop's window onto the board's camera.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_DESKTOP_CAMERA_H
#define SURFACE_DESKTOP_CAMERA_H
#include "tiku_window.h"
#include <stdint.h>
/** @brief Open Camera's window within @p area, or raise it; @p review shows
 *  a saved photo's file.  @return the window, or NULL. */
tiku_window_t *surface_camera_open(tiku_workspace_t *workspace, tiku_rect_t area,
                                   int *dirty, void (*review)(const char *path));
/** @brief Each desktop turn: answer the camera's last request and ask for
 *  the next. */
void surface_camera_tick(int64_t now);
#endif
