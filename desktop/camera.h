/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * camera.h - Camera, the desktop's window onto the board's camera.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_DESKTOP_CAMERA_H
#define SURFACE_DESKTOP_CAMERA_H
#include "tiku_window.h"
#include <stdint.h>
/** @brief Open Camera's window within @p area, or raise it; @p damage draws
 *  a part of the window again, @p dirty the whole screen, and @p review
 *  shows a saved photo's file.  @return the window, or NULL. */
tiku_window_t *surface_camera_open(tiku_workspace_t *workspace, tiku_rect_t area, int *dirty,
                                   void (*damage)(tiku_window_t *window, tiku_rect_t rect),
                                   void (*review)(const char *path));
/** @brief Each desktop turn: answer the camera's last request, ask for the
 *  next, and draw a picture that arrived. */
void surface_camera_tick(int64_t now);
#endif
