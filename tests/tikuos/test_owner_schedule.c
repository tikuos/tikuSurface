/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_owner_schedule.c - owner continuations through production process and timer queues.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "runtime.h"
#include "boot.h"
#include <kernel/process/tiku_process.h>
#include <kernel/timers/tiku_timer.h>
#include <kernel/memory/tiku_mem.h>
#include <assert.h>
#include <stdio.h>

extern struct tiku_process surface_process;
volatile uint8_t tiku_s31_in_trap;
volatile uint8_t tiku_crit_held;
static tiku_clock_time_t ticks;
static unsigned turns, peers, peer_first_turn, fill_queue;
static int active, stopping, fast = 1;
static unsigned kernel_wakes;
int tiku_thread_in_kernel(void) { return 1; }
void tiku_thread_kernel_wake(void) { ++kernel_wakes; }
void tiku_atomic_enter(void) {}
void tiku_atomic_exit(void) {}
tiku_clock_time_t tiku_clock_time(void) { return ticks; }
uint8_t tiku_hang_is_culprit(const struct tiku_process *p) { (void)p; return 0; }
void tiku_vfs_unwatch_all(struct tiku_process *p) { (void)p; }
void tiku_sched_notify(void) {}
tiku_mem_err_t tiku_arena_stats(const tiku_arena_t *a, tiku_mem_stats_t *s)
{ (void)a; (void)s; return TIKU_MEM_ERR_INVALID; }

/** @brief Observe another process's progress between owner continuations. */
static char receive(struct pt *pt, tiku_event_t event, tiku_event_data_t data)
{
    (void)pt; (void)data;
    if (event == TIKU_EVENT_CONTINUE) {
        if (!peers) peer_first_turn = turns;
        ++peers;
    }
    return PT_YIELDED;
}
static struct tiku_process peer = {.name = "peer", .thread = receive, .pid = -1};

int surface_runtime_start(void)
{ if (active) return 0; active = 1; stopping = 0; return 1; }
void surface_runtime_stop(void) { stopping = 1; }
void surface_runtime_stats(surface_runtime_stats_t *s)
{ *s = (surface_runtime_stats_t){.running = (uint32_t)active}; }
int surface_runtime_continue(void) { return fast && turns % 4; }
int surface_runtime_poll(void)
{
    assert(TIKU_THIS() == &surface_process);
    if (stopping) { active = 0; return 0; }
    ++turns;
    if (fill_queue) {
        fill_queue = 0;
        while (tiku_process_post(&peer, TIKU_EVENT_CONTINUE, NULL)) {}
    }
    return 1;
}

/** @brief Drain a finite queue and reject unbounded continuation chains. */
static void drain(void)
{
    unsigned i = 0;
    while (tiku_process_run()) assert(++i < 1000);
}
/** @brief Advance the real timer subsystem by one tick. */
static void tick(void)
{ ++ticks; tiku_timer_request_poll(); drain(); }

int main(void)
{
    tiku_process_init(); tiku_timer_init(); tiku_process_start(&peer, NULL); drain();
    assert(tiku_application_start());
    assert(tiku_process_post(&peer, TIKU_EVENT_CONTINUE, NULL));
    drain();
    assert(turns == 4 && peers == 1 && peer_first_turn == 1);
    unsigned previous = turns;
    int foreign;
    assert(tiku_process_post(&surface_process, TIKU_EVENT_TIMER, &foreign));
    struct tiku_timer *armed = tiku_timer_get(0);
    assert(armed && armed->p == &surface_process && !tiku_timer_expired(armed));
    assert(tiku_process_post(&surface_process, TIKU_EVENT_TIMER, armed));
    assert(tiku_process_post(&surface_process, TIKU_EVENT_CONTINUE, NULL));
    drain(); assert(turns == previous);
    tick(); assert(turns == 8);
    fast = 0; tick(); assert(turns == 9); tick(); assert(turns == 10);
    tiku_application_stop(); drain();
    assert(!active && !tiku_process_is_running(&surface_process));
    tick(); assert(turns == 10);
    turns = peers = 0; fast = 1; fill_queue = 1;
    assert(tiku_application_start()); drain();
    assert(turns == 1 && peers && tiku_process_is_running(&surface_process));
    tick(); assert(turns == 4);
    tiku_application_stop(); drain(); assert(!active);
#if TIKU_THREADS_ENABLE
    assert(kernel_wakes);
#endif
    puts("Owner scheduling: FIFO peer progress, bounded chains, timer fallback, foreign events and stop passed");
    return 0;
}
