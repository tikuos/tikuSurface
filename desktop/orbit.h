/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * orbit.h - the Orbit frame-rate demo in the Deskbar.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_ORBIT_H
#define SURFACE_ORBIT_H
#include "tiku_window.h"

tiku_window_t *surface_orbit_open(tiku_workspace_t *workspace, tiku_rect_t area,
                                  int *dirty, const char *path);
void surface_orbit_tick(int64_t now);

#endif
