/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_local_programs.c - local session lifetime, capacity and delivery.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../desktop/local_programs.h"
#include <services/link/tiku_link.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static tiku_link_t *client;
static unsigned delivered;
static int room;
static unsigned char process_space = 1;
static unsigned closed;
uint8_t tiku_process_queue_space(void) { return process_space; }
int tiku_thread_call(int (*fn)(void *), void *arg) { return fn(arg); }
void surface_os_desktop_wake(void) {}
void surface_os_poll_isr(void) {}
void tiku_draw_service(void) {}
int tiku_draw_input_room(uint32_t id) { assert(id == 7); return room; }

static void receive(void *ctx, uint8_t *bytes, size_t size)
{
    (void)ctx;
    if (bytes[0] == 18) {
        assert(size == 12 && bytes[8] == 7 && delivered == SURFACE_PROGRAM_SLOTS);
        closed++;
        return;
    }
    assert(size == 76 && bytes[0] == 16 && bytes[8] == 7);
    delivered++;
}

void tiku_draw_set_link(tiku_link_t *new_link)
{
    client = new_link;
    if (new_link) tiku_link_on_recv(client, receive, NULL);
}

int main(void)
{
    static surface_program_buffers_t buffers;
    uint8_t payload[68] = {7}, output[SURFACE_PROGRAM_MTU] = {0};
    int a = surface_program_attach(&buffers);
    assert(a > 0 && surface_program_attach(&buffers) < 0);
    for (int i = 0; i < SURFACE_PROGRAM_SLOTS; ++i) {
        output[0] = (uint8_t)i;
        assert(tiku_link_send(client, output, sizeof output) == 0);
        assert(surface_program_send(a, 16, payload, sizeof payload) == 0);
    }
    assert(tiku_link_send(client, output, 1) < 0);
    assert(surface_program_send(a, 16, payload, sizeof payload) < 0);
    assert(surface_program_send(a, 18, payload, 4) == 0);
    assert(surface_program_read(a, output, 1) < 0);
    assert(tiku_link_send2(client, output, SIZE_MAX, NULL, 0) < 0);
    for (int i = 0; i < SURFACE_PROGRAM_SLOTS; ++i) {
        assert(surface_program_ready(NULL));
        assert(surface_program_read(a, output, sizeof output) == sizeof output);
        assert(output[0] == i);
    }
    assert(!surface_program_ready(NULL));
    surface_program_pump();
    assert(!delivered);
    room = 1;
    process_space = 0;
    surface_program_pump();
    assert(delivered == 0 && !closed);
    process_space = 1;
    surface_program_pump();
    assert(delivered == SURFACE_PROGRAM_SLOTS);
    surface_program_pump();
    assert(closed == 1);
    assert(surface_program_send(a, 16, payload, sizeof payload) == 0);
    assert(tiku_link_send(client, output, 2) == 0);
    surface_program_detach(a);
    int b = surface_program_attach(&buffers);
    assert(b > a);
    surface_program_detach(a);
    assert(surface_program_send(a, 16, payload, sizeof payload) < 0);
    assert(surface_program_send(b, 21, payload, sizeof payload) < 0);
    assert(surface_program_send(b, 23, payload, sizeof payload) < 0);
    assert(surface_program_read(a, output, sizeof output) < 0);
    assert(surface_program_read(b, output, sizeof output) == 0);
    surface_program_pump();
    assert(delivered == SURFACE_PROGRAM_SLOTS);
    assert(tiku_link_send(client, output, 2) == 0);
    surface_program_detach(b);
    puts("PASS both full queues, ordered retry, owner pressure, privilege filter and stale generation");
}
