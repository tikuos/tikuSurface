/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * touch.h - primary contact tracking and explicit coordinate mapping.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_TOUCH_H
#define SURFACE_TOUCH_H
#include "surface.h"
#include "touch_report.h"
/** @brief Caller-owned tracking state; cancellation requires an empty report to rearm. */
typedef struct {
    surface_touch_map_t map;
    uint64_t last_report;
    int x, y;
    unsigned id, active, blocked;
} surface_touch_t;
/** @brief Validate mapping bounds and boolean flags. */
int surface_touch_map_valid(const surface_touch_map_t *);
/** @brief Initialize tracking; invalid mapping refuses initialization. */
int surface_touch_init(surface_touch_t *, const surface_touch_map_t *);
/** @brief Cancel without activation and wait for complete contact release. */
void surface_touch_cancel(surface_touch_t *, surface_t *);
/** @brief Deliver one complete report; malformed coordinates/IDs cancel the gesture. */
void surface_touch_report(surface_touch_t *, surface_t *, const surface_touch_report_t *, uint64_t);
/** @brief Cancel an active contact after 100 ms without a report or clock rollback. */
void surface_touch_expire(surface_touch_t *, surface_t *, uint64_t);
#endif
