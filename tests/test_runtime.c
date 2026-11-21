/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_runtime.c - firmware runtime reservation and retirement contracts.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static _Alignas(64) unsigned char region[SURFACE_RUNTIME_BYTES];
static int reserved, deny_reserve, deny_release;
static unsigned reservations, releases;
static uint64_t now, clock_step = 100;
int surface_host_allocating;

void *surface_os_reserve(size_t bytes)
{
    assert(bytes == sizeof region && !reserved);
    if (deny_reserve) return NULL;
    memset(region, 0xa5, sizeof region);
    reserved = 1;
    ++reservations;
    return region;
}

int surface_os_release(void *base)
{
    assert(reserved && base == region);
    if (deny_release) return 0;
    reserved = 0;
    ++releases;
    memset(region, 0xdd, sizeof region);
    return 1;
}

uint64_t surface_os_time(void)
{ now += clock_step; return now; }

/** @brief Read the runtime's copied diagnostic counters. */
static surface_runtime_stats_t stats(void)
{ surface_runtime_stats_t value; surface_runtime_stats(&value); return value; }

/** @brief Drain presentation and prove the reserved span has been returned. */
static void stop(void)
{
    surface_runtime_stop();
    assert(!surface_runtime_launch(1));
    for (unsigned i = 0; i < 8 && surface_runtime_poll(); ++i) {}
    surface_runtime_stats_t value = stats();
    assert(!value.running && !value.stopping && !value.live_bytes && !value.allocations);
    assert(!reserved && reservations == releases);
    assert(!surface_runtime_poll());
}

/** @brief Reject reservations and unwind each shell allocation failure. */
static void startup_failures(void)
{
    deny_reserve = 1;
    assert(!surface_runtime_start());
    assert(!reserved && !stats().running);
    deny_reserve = 0;
    for (unsigned i = 1; i <= 5; ++i) {
        surface_runtime_fail_at(i);
        assert(!surface_runtime_start());
        assert(!reserved && !stats().running && stats().allocation_failures == 1);
        assert(reservations == releases);
    }
    surface_runtime_fail_at(0);
}

/** @brief Retain a failed release, including a partially initialized shell. */
static void deferred_release(void)
{
    for (unsigned fail = 0; fail <= 1; ++fail) {
        deny_release = 1;
        surface_runtime_fail_at(fail);
        assert(surface_runtime_start() == !fail);
        surface_runtime_stop();
        assert(surface_runtime_poll());
        assert(reserved && stats().running && stats().stopping && !stats().live_bytes);
        assert(!surface_runtime_start());
        deny_release = 0;
        stop();
    }
    surface_runtime_fail_at(0);
}

/** @brief Repeated app switches reclaim their memory before returning the span. */
static void lifecycle(void)
{
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        assert(surface_runtime_start());
        assert(!surface_runtime_start());
        assert(surface_runtime_poll());
        assert(!stats().frames);
        for (unsigned turn = 1; turn < 480 && !stats().frames; ++turn)
            assert(surface_runtime_poll());
        surface_runtime_stats_t initial = stats();
        assert(initial.frames == 1 && initial.allocations == 5);
        assert(initial.live_bytes > 1536000 && initial.peak_bytes < sizeof region);
        assert(!surface_runtime_launch(99));
        for (unsigned id = 1; id <= 3 + SURFACE_ARITHMETIC; ++id) {
            assert(surface_runtime_launch(id));
            assert(surface_runtime_poll());
            assert(surface_runtime_poll());
            surface_runtime_stats_t opened = stats();
            assert(surface_runtime_launch(0));
            assert(surface_runtime_poll());
            assert(surface_runtime_poll());
            assert(stats().live_bytes == opened.live_bytes);
            assert(stats().allocations == opened.allocations);
            assert(surface_runtime_launch(id));
            assert(surface_runtime_poll());
            assert(stats().live_bytes == opened.live_bytes);
            assert(stats().allocations == opened.allocations);
        }
        clock_step = 6000;
        assert(surface_runtime_poll());
        assert(stats().overruns && stats().max_dispatch_us >= 6000);
        clock_step = 100;
        stop();
    }
}

/** @brief Browser initialization failures leave the shell allocation baseline intact. */
static void app_failures(void)
{
    assert(surface_runtime_start());unsigned initial=stats().allocations;
    assert(surface_runtime_launch(2));assert(surface_runtime_poll());
    unsigned allocations=stats().allocations-initial;stop();
    for (unsigned fail = 1; fail <= allocations; ++fail) {
        assert(surface_runtime_start());
        unsigned baseline = stats().live_bytes;
        surface_runtime_fail_at(fail);
        assert(surface_runtime_launch(2));
        assert(surface_runtime_poll());
        assert(surface_runtime_launch(0));
        assert(surface_runtime_poll());
        assert(surface_runtime_poll());
        assert(stats().allocation_failures && stats().live_bytes == baseline);
        stop();
        surface_runtime_fail_at(0);
    }
}

/** @brief Run the ownership contracts against the firmware runtime implementation. */
int main(void)
{
    startup_failures();
    deferred_release();
    lifecycle();
    app_failures();
    puts("Firmware runtime: startup failures, deferred release, 100 cycles, app failures passed");
    return 0;
}
