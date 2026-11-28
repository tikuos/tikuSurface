/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * touch_preferences.h - the native desktop's touch settings.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_TOUCH_PREFERENCES_H
#define SURFACE_TOUCH_PREFERENCES_H
#include "tiku_window.h"

/** @brief Read the desktop thread's magnifier choice. Defaults to enabled. */
void surface_touch_preferences_load(void);
int surface_touch_magnifier(void);
/** @brief Start the shell's calibration page on the desktop thread. */
void surface_desktop_calibrate_touch(void);
tiku_window_t *surface_touch_preferences_open(tiku_workspace_t *workspace,
                                              tiku_rect_t area, int *dirty,
                                              const char *path);
#endif
