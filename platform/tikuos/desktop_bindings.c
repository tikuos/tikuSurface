/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * desktop_bindings.c - TikuOS calls for the desktop thread and its runtime.
 *
 * The desktop runs on one preemptible TikuOS worker whose stack lies in the
 * runtime's PSRAM span.  It blocks on one wait queue, which the kernel
 * runtime wakes when touch input arrives or a frame reaches the panel.  The
 * desktop's zero-initialized statics live in a PSRAM section the linker
 * script places at the top of PSRAM.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../desktop/bridge_os.h"
#include <hal/tiku_cpu.h>
#include <kernel/cpu/tiku_rtc.h>
#include <kernel/process/tiku_process.h>
#include <kernel/threads/tiku_thread.h>
#include <kernel/timers/tiku_clock.h>
#include <services/console/tiku_console.h>
#include <arch/esp32s31/tiku_trng_arch.h>
#include <arch/esp32s31/tiku_runtime_arch.h>
#include <services/storage/tiku_media_card.h>
#include <string.h>

extern struct tiku_process surface_process;
extern char __surface_psram_bss_start[] __attribute__((weak));
extern char __surface_psram_bss_end[] __attribute__((weak));

static tiku_thread_t desktop_worker;
static tiku_waitq_t desktop_queue;

int surface_os_desktop_start(void (*entry)(void *), void *arg, void *stack, size_t bytes)
{
    if (!tiku_thread_in_kernel() || stack == NULL || ((uintptr_t)stack & 15u) ||
        bytes < TIKU_THREAD_STACK_MIN) {
        return -1;
    }
    memset(&desktop_worker, 0, sizeof desktop_worker);
    desktop_worker.stack_base = stack;
    desktop_worker.stack_size = bytes;
    desktop_worker.name = "desktop";
    return tiku_thread_start(&desktop_worker, entry, arg);
}

void surface_os_desktop_wait(int (*ready)(void *), void *context, uint64_t timeout_us)
{
    unsigned long ticks = 0;

    if (timeout_us != 0u) {
        uint64_t t = (timeout_us * TIKU_CLOCK_SECOND + 999999u) / 1000000u;

        ticks = t == 0u ? 1u : t > 0x7fffffffu ? 0x7fffffffu : (unsigned long)t;
    }
    tiku_atomic_enter();
    if (ready == NULL || !ready(context)) {
        (void)tiku_thread_wait(&desktop_queue, ticks);
    }
    tiku_atomic_exit();
}

void surface_os_desktop_wake(void)
{
    tiku_thread_wake_all(&desktop_queue);
}

void surface_os_desktop_kick(void)
{
    tiku_process_poll(&surface_process);
}

void surface_os_desktop_exit(void)
{
    tiku_thread_exit();
}

void surface_os_desktop_print(const void *bytes, size_t length)
{
    if (tiku_thread_in_kernel()) tiku_console_write(bytes, length);
}

void surface_os_desktop_statics(void **base, size_t *bytes)
{
    uintptr_t start = (uintptr_t)__surface_psram_bss_start;
    uintptr_t end = (uintptr_t)__surface_psram_bss_end;

    *base = (void *)start;
    *bytes = (start != 0u && end > start) ? (size_t)(end - start) : 0u;
}

void surface_os_desktop_lock(void) { tiku_atomic_enter(); }
void surface_os_desktop_unlock(void) { tiku_atomic_exit(); }

int surface_os_desktop_random(void *bytes, size_t length)
{
    uint8_t *out = bytes;

    while (length > 0u) {
        size_t n = length > 4096u ? 4096u : length;

        if (tiku_trng_arch_read_bytes(out, n) != TIKU_TRNG_OK) return -1;
        out += n;
        length -= n;
    }
    return 0;
}

uintptr_t surface_os_desktop_sample(void)
{
    /* The kernel is on the CPU, so a READY desktop thread was preempted and
     * its saved frame holds the interrupted address. */
    if (desktop_worker.state != TIKU_THREAD_READY || desktop_worker.sp == NULL) return 0;
    return desktop_worker.sp[S31_FRAME_PC / 4u];
}

int surface_os_desktop_context(uintptr_t *pc, uintptr_t *ra, const uint32_t **sp)
{
    const uint32_t *f = desktop_worker.sp;

    if (desktop_worker.state != TIKU_THREAD_READY || f == NULL) return 0;
    *pc = f[S31_FRAME_PC / 4u];
    *ra = f[1];
    *sp = (const uint32_t *)(uintptr_t)f[2];
    return 1;
}

int surface_os_in_desktop(void)
{
    return tiku_thread_self() == &desktop_worker;
}

/* The SD clock the card is mounted at; the media service accepts up to
 * 20 MHz. */
#ifndef SURFACE_CARD_HZ
#define SURFACE_CARD_HZ 20000000u
#endif

int surface_os_card_mount(void)
{
    /* Mounting discovers the volume's UUID and writes nothing. */
    return tiku_media_card_mount(NULL, NULL, SURFACE_CARD_HZ);
}

int surface_os_clock_state(void)
{
    switch (tiku_rtc_state()) {
    case TIKU_RTC_SET:
        return SURFACE_CLOCK_SET;
    case TIKU_RTC_RESUMED:
        return SURFACE_CLOCK_RESUMED;
    default:
        return SURFACE_CLOCK_UNSET;
    }
}
