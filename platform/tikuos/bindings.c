/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * bindings.c - TikuOS PSRAM reservation, clock and timer-process binding.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "runtime.h"
#include "boot.h"
#if SURFACE_NATIVE_DESKTOP
#include "../../desktop/local_programs.h"
#endif
#include <kernel/memory/tiku_mem.h>
#include <kernel/process/tiku_process.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/timers/tiku_timer.h>
#include <kernel/threads/tiku_thread.h>
#include <arch/esp32s31/tiku_runtime_arch.h>

static tiku_arena_t arena;
static struct tiku_timer wake;
TIKU_PROCESS(surface_process, "TikuSurface");

void *surface_os_reserve(size_t bytes)
{
    if (arena.active || !surface_os_in_owner() || bytes != SURFACE_RUNTIME_BYTES) return NULL;
    if (tiku_tier_arena_create_span(&arena, TIKU_MEM_PSRAM, 0, bytes, 64, 0x53) != TIKU_MEM_OK) return NULL;
    return arena.buf;
}

#ifdef SURFACE_FRAMES_BYTES
static tiku_arena_t frames;

void *surface_os_reserve_frames(size_t bytes)
{
    if (frames.active || !surface_os_in_owner() || bytes != SURFACE_FRAMES_BYTES) return NULL;
    if (tiku_tier_arena_create_span(&frames, TIKU_MEM_PSRAM, 0, bytes, 64, 0x46) != TIKU_MEM_OK)
        return NULL;
    return frames.buf;
}
#endif

int surface_os_release(void *base)
{
    if (!arena.active || arena.buf != base || !surface_os_in_owner()) return 0;
    return tiku_arena_destroy(&arena) == TIKU_MEM_OK;
}

int surface_os_in_owner(void)
{ return tiku_thread_in_kernel() && !tiku_s31_in_trap; }

int surface_os_free_bytes(uint32_t *bytes)
{
    tiku_mem_space_t space;
    if (!bytes || !surface_os_in_owner() || tiku_tier_span_space(TIKU_MEM_PSRAM, 0, &space) != TIKU_MEM_OK)
        return 0;
    *bytes = (uint32_t)space.free_bytes;
    return 1;
}

uint64_t surface_os_time(void)
{ return tiku_clock_usec(); }

void surface_os_poll_isr(void)
{ tiku_process_poll(&surface_process); }

/** @brief Dispatch bounded owner turns with a timer fallback and queued LCD continuations. */
TIKU_PROCESS_THREAD(surface_process, ev, data)
{
    TIKU_PROCESS_BEGIN();
    for (;;) {
        tiku_timer_set_event(&wake, 1);
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_POLL || ev == TIKU_EVENT_EXIT ||
            (ev == TIKU_EVENT_TIMER && data == &wake && tiku_timer_expired(&wake)));
        if (ev == TIKU_EVENT_EXIT) surface_runtime_stop();
#if SURFACE_NATIVE_DESKTOP
        surface_program_pump();
        surface_basic_window_poll();
#endif
        if (!surface_runtime_poll()) break;
#if SURFACE_LCD_ENABLE
        if (surface_runtime_continue()) tiku_process_poll(&surface_process);
#endif
    }
    tiku_timer_stop(&wake);
    TIKU_PROCESS_END();
}

#if SURFACE_NATIVE_DESKTOP
#include "tiku_gui.h"
#include "tiku_draw_program.h"
extern struct tiku_process gui_demo_process;

static void form_exited(const tiku_process_observation_t *r, void *unused)
{
    (void)unused;
    if (r->transition != TIKU_PROCESS_EXITING) return;
    if (r->process == &gui_demo_process) tiku_gui_remove_owner(r->process);
    tiku_draw_program_exited(r->process);
}
#endif

int tiku_application_start(void)
{
    if (!surface_os_in_owner()) return 0;
    int started = surface_runtime_start();
    surface_runtime_stats_t state;
    surface_runtime_stats(&state);
    if (state.running && !tiku_process_is_running(&surface_process)) {
        tiku_process_start(&surface_process, NULL);
#if SURFACE_NATIVE_DESKTOP
        if (tiku_process_observer_add(form_exited, NULL))
            tiku_process_start(&gui_demo_process, NULL);
#endif
#if SURFACE_LCD_ENABLE
        tiku_process_poll(&surface_process);
#endif
    }
    return started;
}

void tiku_application_stop(void)
{ if (surface_os_in_owner()) { surface_runtime_stop(); tiku_process_poll(&surface_process); } }
