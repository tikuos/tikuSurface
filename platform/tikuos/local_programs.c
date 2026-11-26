/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * local_programs.c - kernel side of the native desktop window session.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../desktop/local_programs.h"
#include "../../desktop/bridge_os.h"
#include <kernel/threads/tiku_thread.h>
#include <services/link/tiku_link_local.h>
#include "tiku_draw.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static tiku_link_local_t up, down;
static int epoch;
static tiku_link_t offline;
static unsigned pending;
enum { WINDOW_EVENT = 16, WINDOW_CLOSED = 18, WELCOME = 20 };
static uint32_t deferred_close[TIKU_DRAW_WINDOWS];
static unsigned closing_count, owner_waits;
extern void surface_os_poll_isr(void);

static void published(void)
{
    __atomic_store_n(&pending, 1, __ATOMIC_RELEASE);
    surface_os_desktop_wake();
}

int surface_program_ready(void *ctx)
{
    (void)ctx;
    return __atomic_load_n(&pending, __ATOMIC_ACQUIRE);
}

static int attach(void *arg)
{
    surface_program_buffers_t *b = arg;
    if (!b || up.storage || epoch == INT_MAX) return -1;
    tiku_link_local_open(&up, b->up[0], SURFACE_PROGRAM_SLOTS, sizeof b->up[0]);
    tiku_link_local_open(&down, b->down[0], SURFACE_PROGRAM_SLOTS, sizeof b->down[0]);
    up.wake = published;
    down.wake = surface_os_poll_isr;
    tiku_draw_set_link(&up.link);
    closing_count = owner_waits = 0;
    return ++epoch;
}

int surface_program_attach(surface_program_buffers_t *buffers)
{
    return tiku_thread_call(attach, buffers);
}

static int detach(void *arg)
{
    if (*(int *)arg != epoch || !up.storage) return -1;
    tiku_draw_set_link(&offline);
    tiku_link_close(&up.link);
    tiku_link_close(&down.link);
    __atomic_store_n(&pending, 0, __ATOMIC_RELEASE);
    return 0;
}

void surface_program_detach(int generation)
{
    (void)tiku_thread_call(detach, &generation);
}

struct message {
    int epoch;
    void *out;
    const void *data;
    size_t size;
    uint32_t type;
};

static int read_message(void *arg)
{
    struct message *m = arg;
    if (m->epoch != epoch) return -1;
    int n = tiku_link_local_read(&up, m->out, m->size);
    __atomic_store_n(&pending, up.count != 0, __ATOMIC_RELEASE);
    return n;
}

int surface_program_read(int generation, void *buf, size_t size)
{
    struct message m = {.epoch = generation, .out = buf, .size = size};
    return tiku_thread_call(read_message, &m);
}

static int send_message(void *arg)
{
    struct message *m = arg;
    uint32_t head[] = {m->type, (uint32_t)m->size};
    if (m->epoch != epoch || !down.storage || !m->data) return -1;
    if (!((m->type == WINDOW_EVENT && m->size == 68) ||
          (m->type == WINDOW_CLOSED && m->size == 4) ||
          (m->type == WELCOME && m->size == 8))) return -1;
    if (!closing_count &&
        tiku_link_send2(&down.link, head, sizeof head, m->data, m->size) == 0) return 0;
    if (m->type != WINDOW_CLOSED) return -1;
    uint32_t id;
    memcpy(&id, m->data, sizeof id);
    for (unsigned i = 0; i < closing_count; i++)
        if (deferred_close[i] == id) return 0;
    if (closing_count == TIKU_DRAW_WINDOWS) return -1;
    deferred_close[closing_count++] = id;
    surface_os_poll_isr();
    return 0;
}

int surface_program_send(int generation, uint32_t type, const void *data, uint32_t size)
{
    struct message m = {.epoch = generation, .data = data, .size = size, .type = type};
    return tiku_thread_call(send_message, &m);
}

void surface_program_pump(void)
{
    unsigned char packet[80];
    tiku_draw_service();
    for (unsigned i = 0; i < SURFACE_PROGRAM_SLOTS && down.count; i++) {
        uint16_t length;
        const uint8_t *p = tiku_link_local_front(&down, &length);
        uint32_t id;
        if (!p || length < 12) break;
        memcpy(&id, p + 8, sizeof id);
        if (p[0] != WELCOME && (!tiku_draw_input_room(id) || !tiku_process_queue_space())) {
            ++owner_waits;
            break;
        }
        int n = tiku_link_local_read(&down, packet, sizeof packet);
        if (n <= 0) break;
        tiku_link_deliver(&up.link, packet, (size_t)n);
    }
    while (closing_count && down.count < down.slots) {
        uint32_t record[] = {WINDOW_CLOSED, 4, deferred_close[0]};
        if (tiku_link_send(&down.link, record, sizeof record)) break;
        --closing_count;
        memmove(deferred_close, deferred_close + 1, closing_count * sizeof *deferred_close);
    }
}

int surface_program_queue_status(char *out, size_t size)
{
    return snprintf(out, size, "up_full=%u down_full=%u owner_waits=%u pending_close=%u\n",
                    up.full, down.full, owner_waits, closing_count);
}
