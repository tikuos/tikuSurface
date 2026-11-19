/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_touch.c - contact identity, calibration and cancellation fixtures.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "touch.h"
#include <assert.h>
#include <stdio.h>
static tiku_input_event_t events[64];
static unsigned count;
static int refuse;
/** @brief Capture posted events without dereferencing the shell marker. */
int surface_post(surface_t *s, const tiku_input_event_t *e)
{
    assert(s && count < 64);
    events[count++] = *e;
    return !refuse;
}
/** @brief Exercise primary identity, mapping, loss, expiration and refused queues. */
int main(void)
{
    surface_touch_t t;
    surface_t *s = (surface_t *)&t;
    surface_touch_map_t map = {100, 1100, 200, 2200, 0, 0, 0};
    assert(surface_touch_init(&t, &map));
    assert(!surface_touch_init(&t, &(surface_touch_map_t){0}));
    surface_touch_report_t r = {.count = 2, .points = {{1100,2200,7},{100,200,3}}};
    surface_touch_report(&t,s,&r,10);
    assert(count == 1 && events[0].type == TIKU_EVENT_POINTER_DOWN && !events[0].x && !events[0].y && t.id == 3);
    r.points[0].id = 1;
    r.points[1].x = 600; r.points[1].y = 1200;
    surface_touch_report(&t,s,&r,20);
    assert(count == 2 && events[1].type == TIKU_EVENT_POINTER_MOVE && events[1].x == 400 && events[1].y == 240 && t.id == 3);
    r.count = 1;
    surface_touch_report(&t,s,&r,30);
    assert(events[2].type == TIKU_EVENT_DEACTIVATED && t.blocked && !t.active);
    surface_touch_report(&t,s,&r,40); assert(count == 3);
    r.count = 0; surface_touch_report(&t,s,&r,50); assert(!t.blocked);
    r.count = 1; surface_touch_report(&t,s,&r,60); assert(events[3].x == 799 && events[3].y == 479);
    r.count = 0; surface_touch_report(&t,s,&r,70);
    assert(events[4].type == TIKU_EVENT_POINTER_UP && !t.active);
    map.swap = map.invert_x = 1;
    assert(surface_touch_init(&t,&map));
    r.count = 1; r.points[0].x = 100; r.points[0].y = 2200;
    surface_touch_report(&t,s,&r,80); assert(events[5].x == 0 && events[5].y == 0);
    surface_touch_expire(&t,s,100079); assert(t.active);
    surface_touch_expire(&t,s,100080); assert(!t.active && t.blocked && events[6].type == TIKU_EVENT_DEACTIVATED);
    r.count = 0; surface_touch_report(&t,s,&r,100081);
    r.count = 1; surface_touch_report(&t,s,&r,100082);
    surface_touch_report(&t,s,&r,1); assert(!t.active && t.blocked);
    assert(surface_touch_init(&t,&map));
    r.count = 2; r.points[1] = r.points[0];
    surface_touch_report(&t,s,&r,1); assert(t.blocked && !t.active);
    assert(surface_touch_init(&t,&map)); r.count = 1; r.points[0].x = 99;
    surface_touch_report(&t,s,&r,1); assert(t.blocked && !t.active);
    assert(surface_touch_init(&t,&map)); r.points[0].x = 100; refuse = 1;
    surface_touch_report(&t,s,&r,1); assert(t.blocked && !t.active);
    refuse = 0; assert(surface_touch_init(&t,&map)); r.cancel = 1;
    surface_touch_report(&t,s,&r,1); assert(t.blocked && !t.active);
    puts("touch: mapping, identity, cancellation and expiry pass");
    return 0;
}
