/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * terminal_io.c - serialize the desktop terminal's shell capture in the kernel.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../desktop/terminal.h"
#include "console_io.h"
#include <compat/posix/tiku_posix.h>
#include <kernel/process/tiku_process.h>
#include <kernel/threads/tiku_thread.h>
#include <limits.h>

TIKU_PROCESS(terminal_process, "Terminal");
static void *capture;
static int generation;

void surface_terminal_exited(const struct tiku_process *owner)
{
    if (owner == &terminal_process && capture) {
        (void)surface_os_console_close(capture);
        capture = NULL;
    }
}

TIKU_PROCESS_THREAD(terminal_process, ev, data)
{
    (void)data;
    TIKU_PROCESS_BEGIN();
    (void)tiku_process_register("terminal", &terminal_process);
    TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_EXIT);
    TIKU_PROCESS_END();
}

static int claim(void *storage)
{
    if (!storage || capture || generation == INT_MAX) return -1;
    surface_os_console_init(storage);
    if (!surface_os_console_open_cap(storage, tiku_posix_authority())) return -1;
    capture = storage;
    tiku_process_start(&terminal_process, NULL);
    if (!tiku_process_is_running(&terminal_process)) {
        surface_terminal_exited(&terminal_process);
        return -1;
    }
    return ++generation;
}

int surface_terminal_claim(void *storage)
{
    return tiku_thread_call(claim, storage);
}

struct transfer {
    int handle;
    const void *input;
    void *output;
    size_t size;
    unsigned *lost;
};

static int transfer(void *arg)
{
    struct transfer *t = arg;
    if (!capture || t->handle != generation) return -1;
    if (t->lost) return surface_os_console_read(capture, t->output, t->size, t->lost);
    return surface_os_console_write(capture, t->input, t->size);
}

int surface_terminal_read(int handle, void *buf, size_t size, unsigned *lost)
{
    struct transfer t = {.handle = handle, .output = buf, .size = size, .lost = lost};
    return tiku_thread_call(transfer, &t);
}

int surface_terminal_write(int handle, const void *buf, size_t size)
{
    struct transfer t = {.handle = handle, .input = buf, .size = size};
    return tiku_thread_call(transfer, &t);
}

static int interrupt(void *arg)
{
    return *(int *)arg == generation && capture && surface_os_console_interrupt(capture);
}

int surface_terminal_interrupt(int handle)
{
    return tiku_thread_call(interrupt, &handle);
}

static int release(void *arg)
{
    if (*(int *)arg != generation || !capture) return -1;
    tiku_process_exit(&terminal_process);
    return 0;
}

void surface_terminal_release(int handle)
{
    (void)tiku_thread_call(release, &handle);
}
