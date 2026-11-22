/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * touch.c - bounded primary contact events for the product shell.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "touch.h"
#include <string.h>

int surface_touch_map_valid(const surface_touch_map_t *m)
{
    return m && m->x_min < m->x_max && m->y_min < m->y_max &&
        m->swap <= 1 && m->invert_x <= 1 && m->invert_y <= 1;
}

int surface_touch_init(surface_touch_t *t, const surface_touch_map_t *m)
{
    if (!t || !surface_touch_map_valid(m)) return 0;
    *t = (surface_touch_t){.map = *m};
    return 1;
}

void surface_touch_cancel(surface_touch_t *t, surface_t *s)
{
    if (!t) return;
    (void)surface_post(s, &(tiku_input_event_t){.type = TIKU_EVENT_DEACTIVATED});
    t->active = 0;
    t->blocked = 1;
}

/** @brief Post a logical pointer event; queue refusal forces release before rearming. */
static void post(surface_touch_t *t, surface_t *s, tiku_event_type_t type, uint64_t now)
{
    tiku_input_event_t e = {.type = type, .button = 1, .x = t->x, .y = t->y,
        .time_us = now > INT64_MAX ? INT64_MAX : (int64_t)now};
    if (!surface_post(s, &e)) surface_touch_cancel(t, s);
}

void surface_touch_report(surface_touch_t *t, surface_t *s, const surface_touch_report_t *r, uint64_t now)
{
    if (!t || !s) return;
    if (!r || r->cancel || r->count > SURFACE_TOUCH_CONTACTS ||
        !surface_touch_map_valid(&t->map) || (t->active && now < t->last_report)) {
        surface_touch_cancel(t, s);
        return;
    }
    unsigned seen = 0, primary = r->count;
    for (unsigned i = 0; i < r->count; ++i) {
        unsigned id = r->points[i].id;
        if (id >= 16 || (seen & (1u << id)) ||
            r->points[i].x < t->map.x_min || r->points[i].x > t->map.x_max ||
            r->points[i].y < t->map.y_min || r->points[i].y > t->map.y_max) {
            surface_touch_cancel(t, s);
            return;
        }
        seen |= 1u << id;
        if (t->active && id == t->id) primary = i;
    }
    t->last_report = now;
    if (!r->count) {
        if (t->active) post(t, s, TIKU_EVENT_POINTER_UP, now);
        t->active = t->blocked = 0;
        return;
    }
    if (t->blocked) return;
    if (t->active && primary == r->count) {
        surface_touch_cancel(t, s);
        return;
    }
    int was_active = t->active;
    if (!was_active) {
        primary = 0;
        for (unsigned i = 1; i < r->count; ++i)
            if (r->points[i].id < r->points[primary].id) primary = i;
        t->id = r->points[primary].id;
    }
    uint32_t x = r->points[primary].x - t->map.x_min;
    uint32_t y = r->points[primary].y - t->map.y_min;
    uint32_t dx = t->map.x_max - t->map.x_min, dy = t->map.y_max - t->map.y_min;
    if (t->map.swap) { uint32_t a = x; x = y; y = a; a = dx; dx = dy; dy = a; }
    int mapped_x = (int)((x * 799u + dx / 2) / dx);
    int mapped_y = (int)((y * 479u + dy / 2) / dy);
    if (t->map.invert_x) mapped_x = 799 - mapped_x;
    if (t->map.invert_y) mapped_y = 479 - mapped_y;
    int moved = t->x != mapped_x || t->y != mapped_y;
    t->x = mapped_x; t->y = mapped_y; t->active = 1;
    if (!was_active || moved) post(t, s, was_active ? TIKU_EVENT_POINTER_MOVE : TIKU_EVENT_POINTER_DOWN, now);
}

void surface_touch_expire(surface_touch_t *t, surface_t *s, uint64_t now)
{
    if (!t || (now >= t->last_report && now - t->last_report < 100000)) return;
    if (t->active) surface_touch_cancel(t, s);
    else t->blocked = 0;
}
