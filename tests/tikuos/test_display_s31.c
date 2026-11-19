/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_display_s31.c - S31 handoff decisions against simulated MMIO registers.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t registers[256];
static uintptr_t addresses[256];
static unsigned register_count;
/** @brief Return one independently writable simulated hardware register. */
static volatile uint32_t *reg(uintptr_t address)
{
    for (unsigned i = 0; i < register_count; ++i)
        if (addresses[i] == address) return &registers[i];
    assert(register_count < 256);
    addresses[register_count] = address;
    return &registers[register_count++];
}
#define TIKU_S31_DISPLAY_REG(a) (*reg(a))
#define TIKU_S31_DISPLAY_FENCE() ((void)0)
#define TIKU_S31_PSRAM_MHZ 80
void tiku_clock_arch_delay(unsigned int ms);
#include <arch/esp32s31/tiku_display_arch.c>

volatile uint8_t tiku_s31_in_trap;
static int in_kernel = 1, sync_error;
static uint64_t ticks;
static unsigned synced, sync_bytes;
int tiku_thread_in_kernel(void) { return in_kernel; }
uint32_t tiku_s31_psram_size(void) { return 16 * 1024 * 1024; }
uint64_t tiku_s31_time(void) { return ticks; }
int tiku_s31_psram_sync(void *address, uint32_t bytes, uint8_t invalidate)
{
    assert(address == (void *)(uintptr_t)(0x50100000u + 1600 + 64));
    assert(bytes == 64 && !invalidate);
    ++synced; sync_bytes += bytes;
    return sync_error;
}
int tiku_s31_dma_fill(void *destination, uint32_t value, uint32_t bytes)
{ (void)destination; (void)value; (void)bytes; assert(0); return -1; }
void tiku_s31_fatal(const char *message) { (void)message; abort(); }
int tiku_gpio_release(uint8_t port, uint8_t pin, const char *who)
{ (void)port; (void)pin; (void)who; return 0; }
tiku_mem_err_t tiku_arena_destroy(tiku_arena_t *arena)
{ (void)arena; return TIKU_MEM_OK; }

/** @brief Establish an already paired hardware fixture without invoking initialization. */
static void fixture(tiku_display_t *d)
{
    register_count = synced = sync_bytes = 0;
    for (unsigned i = 0; i < 256; ++i) registers[i] = 0;
    in_kernel = 1; sync_error = 0; ticks = 0; tiku_s31_in_trap = 0;
    *d = (tiku_display_t){.front = (void *)(uintptr_t)0x50000000,
        .fb = (void *)(uintptr_t)0x50100000, .w = 800, .h = 480,
        .stride = 1600, .fmt = TIKU_DISPLAY_FMT_RGB565};
    active = d; hardware = paired = 1; shown_ring = faulted = claimed = usb_changed = 0;
    shown_buffer = d->front; link_switched = 0;
    counters = (tiku_s31_display_stats_t){0};
    submission = (tiku_display_async_t){0};
}

/** @brief A switch event alone cannot release a ring still referenced by DMA. */
static void handoff(void)
{
    tiku_display_t d; fixture(&d);
    in_kernel = 0;
    assert(tiku_display_submit(&d, 1, 35, 1, 1, 1) == TIKU_DISPLAY_ERR_STATE);
    in_kernel = 1; tiku_s31_in_trap = 1;
    assert(tiku_display_submit(&d, 1, 35, 1, 1, 1) == TIKU_DISPLAY_ERR_STATE);
    tiku_s31_in_trap = 0;
    assert(tiku_display_submit(&d, 1, 35, 1, 1, 1) == TIKU_DISPLAY_PENDING);
    in_kernel = 0;
    assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_ERR_STATE && submission.phase);
    in_kernel = 1;
    assert(tiku_display_arch_fill_rect(&d, 0, 0, 1, 1, 0) == TIKU_DISPLAY_ERR_STATE);
    assert(tiku_display_arch_set_scanout(&d, d.fb) == TIKU_DISPLAY_ERR_STATE);
    assert(tiku_display_poll(&d, 2) == TIKU_DISPLAY_ERR_TOKEN);
    assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_PENDING && synced == 1 && sync_bytes == 64);
    assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_PENDING);
    assert(descriptors[0][COUNT - 1].next == (uint32_t)(uintptr_t)descriptors[1]);
    REG(DMA + 0x138) = 1u << 10;
    REG(DMA + 0x16c) = (uint32_t)(uintptr_t)descriptors[0];
    assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_PENDING && shown_ring == 0);
    REG(DMA + 0x138) = 0;
    REG(DMA + 0x16c) = (uint32_t)(uintptr_t)&descriptors[1][3];
    assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_OK && shown_ring == 1);
    assert(d.front == (void *)(uintptr_t)0x50100000 && d.fb == (void *)(uintptr_t)0x50000000);
    assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_ERR_TOKEN);
}

/** @brief A publication error retains both buffers until both stopped indications arrive. */
static void retirement(void)
{
    tiku_display_t d; fixture(&d); sync_error = 1;
    assert(tiku_display_submit(&d, 3, 35, 1, 1, 1) == TIKU_DISPLAY_PENDING);
    assert(tiku_display_poll(&d, 3) == TIKU_DISPLAY_PENDING && faulted);
    assert(tiku_display_poll(&d, 3) == TIKU_DISPLAY_PENDING && submission.phase);
    REG(DMA + 0x158) |= 8u;
    assert(tiku_display_poll(&d, 3) == TIKU_DISPLAY_PENDING && submission.phase);
    REG(DMA + 0x298) = 1u;
    assert(tiku_display_poll(&d, 3) == TIKU_DISPLAY_ERR_RETIRED && !submission.phase);
    assert(d.front == (void *)(uintptr_t)0x50000000);
    assert(tiku_display_submit(&d, 4, 35, 1, 1, 1) == TIKU_DISPLAY_ERR_STATE);
    assert(!tiku_s31_display_close(&d) && !active && submission.last_token == 3);
}

int main(void)
{
    descriptor_window();
    assert(REG(DMA + 0x27c) == (uint32_t)(uintptr_t)descriptors);
    assert(REG(DMA + 0x280) == (uint32_t)(uintptr_t)descriptors + sizeof descriptors - 1u);
    handoff(); retirement();
    puts("S31 display: context, DMA ring proof, write refusal and stopped ownership passed");
    return 0;
}
