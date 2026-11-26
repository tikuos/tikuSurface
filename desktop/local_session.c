/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * local_session.c - deliver whole kernel messages to Stomme's window session.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "local_programs.h"
#include "terminal.h"
#include "bridge_os.h"
#include "tiku_remote.h"
#include <string.h>

static surface_program_buffers_t buffers;
static unsigned resetting, restart_requested;

void surface_program_reconnect(void)
{
    __atomic_store_n(&restart_requested, 1, __ATOMIC_RELEASE);
    surface_os_desktop_wake();
}

static int reply(void *ctx, uint32_t type, const void *data, uint32_t n)
{
    return surface_program_send((int)(uintptr_t)ctx, type, data, n) == 0;
}

int tiku_remote_listen(tiku_remote_listener_t *listener)
{
    uint32_t invite[] = {TIKU_REMOTE_VERSION, 0x80000000u | TIKU_FEAT_COMMAND_STREAM};
    memset(listener, 0, sizeof *listener);
    listener->fd = -1;
    int generation = surface_program_attach(&buffers);
    if (generation < 0) return -1;
    surface_terminal_bind(listener);
    void *token = (void *)(uintptr_t)generation;
    if (tiku_remote_attach(listener, reply, token) &&
        reply(token, TIKU_RMSG_WELCOME, invite, sizeof invite)) return 0;
    tiku_remote_shutdown(listener);
    return -1;
}

void tiku_remote_shutdown(tiku_remote_listener_t *listener)
{
    tiku_remote_session_t *s = &listener->session[0];
    surface_terminal_bind(NULL);
    surface_terminal_stop();
    if (s->used) surface_program_detach((int)(uintptr_t)s->packet_ctx);
    tiku_remote_detach(s);
}

int tiku_remote_wait(tiku_remote_listener_t *listener, int timeout_us)
{
    if (!listener->session[0].used) return 0;
    if (timeout_us > 0)
        surface_os_desktop_wait(surface_program_ready, NULL, (uint64_t)timeout_us);
    return surface_program_ready(NULL);
}

int tiku_remote_poll(tiku_remote_listener_t *listener)
{
    unsigned char packet[SURFACE_PROGRAM_MTU];
    tiku_remote_session_t *s = &listener->session[0];
    int changed = surface_terminal_poll();
    if (resetting) {
        tiku_remote_shutdown(listener);
        resetting = 0;
        (void)tiku_remote_listen(listener);
        return 1;
    }
    if (__atomic_exchange_n(&restart_requested, 0, __ATOMIC_ACQ_REL)) {
        for (int i = 0; i < TIKU_REMOTE_WINDOWS; i++) {
            if (!s->win[i].opened) continue;
            uint32_t close[] = {TIKU_RMSG_CLOSE, 4, s->win[i].win_id};
            (void)tiku_remote_receive(s, (unsigned char *)close, sizeof close);
        }
        resetting = 1;
        return 1;
    }
    int generation = (int)(uintptr_t)s->packet_ctx;
    for (unsigned i = 0; s->used && i < SURFACE_PROGRAM_SLOTS && surface_program_ready(NULL); i++) {
        int n = surface_program_read(generation, packet, sizeof packet);
        if (n <= 0) break;
        int r = tiku_remote_receive(s, packet, (size_t)n);
        if (r < 0) {
            tiku_remote_shutdown(listener);
            return 1;
        }
        changed |= r;
    }
    return changed;
}
