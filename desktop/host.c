/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * host.c - the desktop's display host on TikuOS: the S31 panel and touch.
 *
 * tiku_host_present() hands the damaged rectangle of the desktop's surface
 * to the kernel runtime and waits until that rectangle is on the panel, so
 * the desktop never draws into pixels the runtime is still converting.
 * tiku_host_poll() takes pointer events the runtime made from touch reports,
 * keeping only the latest of consecutive moves;
 * tiku_host_wait() blocks the desktop thread until one arrives.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_host.h"
#include "bridge.h"
#include <string.h>

struct tiku_host {
    int width, height;
    uint64_t frames, bytes, rectangles;
    int x, y;
};

static struct tiku_host panel;

tiku_host_t *tiku_host_open(int width, int height, const char *title)
{
    (void)title;
    if (width != SURFACE_DESKTOP_W || height != SURFACE_DESKTOP_H) return NULL;
    memset(&panel, 0, sizeof panel);
    panel.width = width;
    panel.height = height;
    return &panel;
}

void tiku_host_close(tiku_host_t *host) { (void)host; }
void tiku_host_cancel_composition(tiku_host_t *host) { (void)host; }
void tiku_host_text_rect(tiku_host_t *host, tiku_rect_t rect) { (void)host; (void)rect; }
int tiku_host_scale(const tiku_host_t *host) { (void)host; return 1; }
void tiku_host_set_title(tiku_host_t *host, const char *title) { (void)host; (void)title; }
void tiku_host_set_fullscreen(tiku_host_t *host, int on) { (void)host; (void)on; }
void tiku_host_set_cursor(tiku_host_t *host, int kind) { (void)host; (void)kind; }
void tiku_host_set_resize_cursor(tiku_host_t *host, int enabled) { (void)host; (void)enabled; }

/** @brief Whether the panel shows the frame handed over last. */
static int presented(void *context)
{
    (void)context;
    return surface_desktop_bridge.done_token == surface_desktop_bridge.frame_token;
}

int tiku_host_present_in(tiku_host_t *host, const tiku_surface_t *surface,
                         const tiku_rect_t *bounds)
{
    surface_desktop_bridge_t *b = &surface_desktop_bridge;
    tiku_rect_t r = {0, 0, SURFACE_DESKTOP_W, SURFACE_DESKTOP_H};

    if (host == NULL || surface == NULL || surface->w != host->width ||
        surface->h != host->height) {
        return -1;
    }
    if (bounds != NULL) {
        int x0 = bounds->x < 0 ? 0 : bounds->x, y0 = bounds->y < 0 ? 0 : bounds->y;
        int x1 = bounds->x + bounds->w, y1 = bounds->y + bounds->h;

        if (x1 > SURFACE_DESKTOP_W) x1 = SURFACE_DESKTOP_W;
        if (y1 > SURFACE_DESKTOP_H) y1 = SURFACE_DESKTOP_H;
        if (x1 <= x0 || y1 <= y0) return 0;
        r = (tiku_rect_t){x0, y0, x1 - x0, y1 - y0};
    }
    b->frame_damage = r;
    b->frame = surface;
    b->frame_token++;
    surface_os_desktop_kick();
    surface_os_desktop_wait(presented, NULL, 0);
    host->frames++;
    host->rectangles++;
    host->bytes += (uint64_t)r.w * (uint64_t)r.h * 4u;
    return b->frame_result;
}

int tiku_host_present(tiku_host_t *host, const tiku_surface_t *surface)
{
    return tiku_host_present_in(host, surface, NULL);
}

/** @brief Whether a pointer event waits in the ring. */
static int input_waiting(void *context)
{
    (void)context;
    return surface_desktop_bridge.event_head != surface_desktop_bridge.event_tail;
}

int tiku_host_events_queued(tiku_host_t *host)
{
    (void)host;
    return input_waiting(NULL);
}

void tiku_host_wait(tiku_host_t *host, int64_t timeout_us)
{
    (void)host;
    surface_desktop_hash_poll();
    surface_desktop_bridge.waits++;
    if (timeout_us <= 0) timeout_us = 1;
    if (timeout_us > 60000000) timeout_us = 60000000;
    surface_os_desktop_wait(input_waiting, NULL, (uint64_t)timeout_us);
}

int tiku_host_poll(tiku_host_t *host, tiku_event_t *event)
{
    surface_desktop_bridge_t *b = &surface_desktop_bridge;
    unsigned tail = b->event_tail;

    if (host == NULL || event == NULL || tail == b->event_head) return 0;
    *event = b->events[tail % SURFACE_DESKTOP_EVENTS];
    tail++;
    /* A move with another move queued behind it is dropped for the later
     * one: the desktop sees where the contact is now, not where it was
     * while a frame was drawn. */
    while (event->type == TIKU_EVENT_POINTER_MOVE && tail != b->event_head &&
           b->events[tail % SURFACE_DESKTOP_EVENTS].type == TIKU_EVENT_POINTER_MOVE) {
        *event = b->events[tail % SURFACE_DESKTOP_EVENTS];
        tail++;
    }
    b->event_tail = tail;
    if (event->type == TIKU_EVENT_POINTER_DOWN || event->type == TIKU_EVENT_POINTER_UP ||
        event->type == TIKU_EVENT_POINTER_MOVE) {
        host->x = event->x;
        host->y = event->y;
    }
    return 1;
}

int tiku_host_pointer(tiku_host_t *host, int *x, int *y, unsigned *modifiers)
{
    if (host == NULL) return 0;
    if (x != NULL) *x = host->x;
    if (y != NULL) *y = host->y;
    if (modifiers != NULL) *modifiers = 0;
    return 1;
}

void tiku_host_present_stats(tiku_host_t *host, uint64_t *frames, uint64_t *bytes,
                             uint64_t *rectangles)
{
    if (frames != NULL) *frames = host ? host->frames : 0;
    if (bytes != NULL) *bytes = host ? host->bytes : 0;
    if (rectangles != NULL) *rectangles = host ? host->rectangles : 0;
}

int tiku_host_show(const tiku_surface_t *s, const char *title)
{
    (void)s; (void)title;
    return -1;
}

int tiku_host_access(tiku_host_t *host, uint32_t window, tiku_access_tree_t *tree, int x,
                     int y, int focused, tiku_access_action_fn action, void *context)
{
    (void)host; (void)window; (void)tree; (void)x; (void)y; (void)focused;
    (void)action; (void)context;
    return -1;
}

int tiku_host_print(tiku_host_t *host, uint64_t request, const void *pdf, size_t bytes,
                    const char *title, tiku_print_done_fn done, void *context)
{
    (void)host; (void)request; (void)pdf; (void)bytes; (void)title; (void)done;
    (void)context;
    return -1;
}

void tiku_host_print_cancel(tiku_host_t *host, uint64_t request) { (void)host; (void)request; }

void tiku_host_observe(const tiku_host_t *host, tiku_host_observation_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof *out);
    strcpy(out->backend, "tikuos");
    if (host == NULL) return;
    out->available = 1;
    out->scale = 1;
    out->logical_width = out->native_width = host->width;
    out->logical_height = out->native_height = host->height;
    out->fullscreen = out->mapped = 1;
}

size_t tiku_host_print_observe(tiku_host_t *host, tiku_print_job_observation_t *out,
                               size_t capacity)
{
    (void)host; (void)out; (void)capacity;
    return 0;
}
