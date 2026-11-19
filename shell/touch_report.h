/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * touch_report.h - raw contact records at the firmware boundary.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_TOUCH_REPORT_H
#define SURFACE_TOUCH_REPORT_H
#include <stdint.h>
#define SURFACE_TOUCH_CONTACTS 10u
/** @brief Raw contacts copied by the firmware adapter; cancel invalidates a gesture. */
typedef struct {
    unsigned count, cancel;
    struct { uint16_t x, y; uint8_t id; } points[SURFACE_TOUCH_CONTACTS];
} surface_touch_report_t;
/** @brief Inclusive raw bounds followed by axis swap and display-axis inversion. */
typedef struct {
    uint16_t x_min, x_max, y_min, y_max;
    uint8_t swap, invert_x, invert_y;
} surface_touch_map_t;
#endif
