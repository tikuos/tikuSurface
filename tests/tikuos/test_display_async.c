/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_display_async.c - production publication state with simulated DMA.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <interfaces/display/tiku_display_async.h>
#include <interfaces/display/tiku_display_arch.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>

static _Alignas(64) uint16_t buffers[2][800 * 480];
static tiku_display_async_t state;
static uint32_t now, offsets[2048], lengths[2048], count, total;
static int publish_error, switch_result, stopped, stop_requests, switches, async_cap;
static jmp_buf fatal;

/** @brief Record cache ranges and reject publication after requesting a switch. */
static int publish(void *buffer, uint32_t offset, uint32_t bytes)
{
    assert(buffer == state.buffer && !switches && count < 2048);
    assert(bytes && bytes <= 4096 && !(bytes & 63) && !(offset & 63));
    offsets[count] = offset; lengths[count++] = bytes; total += bytes;
    return publish_error;
}
/** @brief Retain the submitted buffer while a switch is pending. */
static void switch_start(void *buffer) { assert(buffer == state.buffer); ++switches; }
/** @brief Return independently controlled DMA switch progress. */
static int switch_poll(void *buffer) { assert(buffer == state.buffer); return switch_result; }
/** @brief Record stop requests without acknowledging that DMA has parked. */
static void stop_start(void) { ++stop_requests; }
/** @brief Return stopped ownership only after the fixture acknowledges it. */
static int stop_poll(void) { return stopped ? TIKU_DISPLAY_OK : TIKU_DISPLAY_PENDING; }
/** @brief Return a clock with controllable wrap and elapsed time. */
static uint32_t now_us(void) { return now; }
/** @brief Escape fail-stop without retiring a DMA-owned request. */
static void fail_stop(void) { longjmp(fatal, 1); }
static const tiku_display_async_ops_t ops = {
    publish, switch_start, switch_poll, stop_start, stop_poll, now_us, fail_stop
};

uint32_t tiku_display_arch_caps(void)
{ return TIKU_DISPLAY_CAP_FLIP | (async_cap ? TIKU_DISPLAY_CAP_ASYNC : 0); }
tiku_display_fmt_t tiku_display_arch_format(void) { return TIKU_DISPLAY_FMT_RGB565; }
void tiku_display_arch_geometry(uint16_t *w, uint16_t *h) { if (w) *w = 800; if (h) *h = 480; }
int tiku_display_arch_init(tiku_display_t *d) { (void)d; return 0; }
int tiku_display_arch_fill_rect(tiku_display_t *d, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t c)
{ (void)d; (void)x; (void)y; (void)w; (void)h; (void)c; return state.phase ? TIKU_DISPLAY_ERR_STATE : 0; }
int tiku_display_arch_fill_circle(tiku_display_t *d, int16_t x, int16_t y, uint16_t r, uint32_t c)
{ (void)d; (void)x; (void)y; (void)r; (void)c; return TIKU_DISPLAY_ERR_UNSUPPORTED; }
int tiku_display_arch_fill_rounded_rect(tiku_display_t *d, int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t r, uint32_t c)
{ (void)d; (void)x; (void)y; (void)w; (void)h; (void)r; (void)c; return TIKU_DISPLAY_ERR_UNSUPPORTED; }
int tiku_display_arch_set_scanout(tiku_display_t *d, void *fb)
{ (void)d; (void)fb; return state.phase ? TIKU_DISPLAY_ERR_STATE : 0; }
int tiku_display_arch_present(tiku_display_t *d, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{ (void)d; (void)x; (void)y; (void)w; (void)h; return state.phase ? TIKU_DISPLAY_ERR_STATE : 0; }
int tiku_display_arch_submit(tiku_display_t *d, uint64_t token, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{ return tiku_display_async_begin(&state, d, token, x, y, w, h); }
int tiku_display_arch_poll(tiku_display_t *d, uint64_t token)
{ return tiku_display_async_step(&state, d, token, &ops); }

/** @brief Reset simulated hardware and create a tight paired display. */
static tiku_display_t fixture(void)
{
    state = (tiku_display_async_t){0};
    now = count = total = publish_error = stopped = stop_requests = switches = 0;
    switch_result = TIKU_DISPLAY_PENDING; async_cap = 1;
    return (tiku_display_t){.fb = buffers[1], .front = buffers[0], .w = 800,
        .h = 480, .stride = 1600, .fmt = TIKU_DISPLAY_FMT_RGB565};
}

/** @brief Full publication yields between batches and does not release before DMA. */
static void full_frame(void)
{
    tiku_display_t d = fixture(), other = d;
    assert(tiku_display_submit(&d, 9, 0, 0, 800, 480) == TIKU_DISPLAY_PENDING);
    assert(!count && !switches);
    assert(tiku_display_submit(&d, 10, 0, 0, 800, 480) == TIKU_DISPLAY_ERR_STATE);
    assert(tiku_display_poll(&d, 8) == TIKU_DISPLAY_ERR_TOKEN);
    assert(tiku_display_poll(&other, 9) == TIKU_DISPLAY_ERR_TOKEN);
    for (unsigned i = 0; i < 188; ++i) {
        assert(tiku_display_poll(&d, 9) == TIKU_DISPLAY_PENDING);
        assert(count == i + 1 && !switches && d.front == buffers[0]);
        assert(offsets[i] == i * 4096);
    }
    assert(total == 768000 && lengths[187] == 2048);
    assert(tiku_display_poll(&d, 9) == TIKU_DISPLAY_PENDING && switches == 1);
    assert(tiku_display_poll(&d, 9) == TIKU_DISPLAY_PENDING && d.front == buffers[0]);
    switch_result = TIKU_DISPLAY_OK;
    assert(tiku_display_poll(&d, 9) == TIKU_DISPLAY_OK);
    assert(d.front == buffers[1] && d.fb == buffers[0]);
    assert(tiku_display_poll(&d, 9) == TIKU_DISPLAY_ERR_TOKEN);
    assert(tiku_display_submit(&d, 9, 0, 0, 1, 1) == TIKU_DISPLAY_ERR_TOKEN);
    tiku_display_async_reset(&state);
    assert(tiku_display_submit(&d, 8, 0, 0, 1, 1) == TIKU_DISPLAY_ERR_TOKEN);
}

/** @brief Narrow damage publishes each expanded row, including padded surfaces. */
static void damage_and_refusals(void)
{
    tiku_display_t d = fixture();
    assert(tiku_display_submit(&d, 1, 31, 7, 2, 3) == TIKU_DISPLAY_PENDING);
    for (unsigned i = 0; i < 3; ++i) {
        assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_PENDING);
        assert(count == i + 1 && offsets[i] == (7 + i) * 1600 && lengths[i] == 128);
    }
    assert(total == 384 && !switches);
    d = fixture(); d.w = 100; d.stride = 256; d.h = 2;
    assert(tiku_display_submit(&d, 1, 0, 0, 100, 2) == TIKU_DISPLAY_PENDING);
    assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_PENDING && total == 512);
    for (unsigned mode = 0; mode < 5; ++mode) {
        d = fixture();
        if (mode == 0) d.stride = 1599;
        if (mode == 1) d.fb = (char *)d.fb + 1;
        if (mode == 2) d.fb = (char *)d.front + 64;
        if (mode == 3) d.fmt = TIKU_DISPLAY_FMT_RGBA8888;
        if (mode == 4) d.stride = 64;
        assert(tiku_display_submit(&d, 1, 0, 0, 800, 480) == TIKU_DISPLAY_ERR_INVALID);
        assert(!state.phase && !count);
    }
    d = fixture();
    assert(tiku_display_submit(&d, 0, 0, 0, 1, 1) == TIKU_DISPLAY_ERR_INVALID);
    assert(tiku_display_submit(&d, 1, 799, 0, 2, 1) == TIKU_DISPLAY_ERR_INVALID);
    assert(tiku_display_submit(&d, 1, 0, 480, 1, 1) == TIKU_DISPLAY_ERR_INVALID);
    async_cap = 0;
    assert(tiku_display_submit(&d, 1, 0, 0, 1, 1) == TIKU_DISPLAY_ERR_UNSUPPORTED);
    assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_ERR_UNSUPPORTED);
    assert(!tiku_display_fill_rect(&d, 2, 3, 4, 5, 0));
    assert(!tiku_display_flip(&d) && d.front == buffers[1]);
}

/** @brief Failed publication and switch timeouts retain ownership through stopped proof. */
static void faults(void)
{
    for (unsigned mode = 0; mode < 3; ++mode) {
        tiku_display_t d = fixture();
        assert(tiku_display_submit(&d, 1, 0, 0, 1, 1) == TIKU_DISPLAY_PENDING);
        if (mode == 0) publish_error = 1;
        assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_PENDING);
        if (mode) {
            now = UINT32_MAX - 100;
            assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_PENDING);
            if (mode == 1) switch_result = TIKU_DISPLAY_ERR_STATE;
            else now += 500000;
            assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_PENDING);
        }
        assert(stop_requests == 1 && d.front == buffers[0] && state.phase);
        assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_PENDING);
        stopped = 1;
        assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_ERR_RETIRED);
        assert(d.front == buffers[0] && d.fb == buffers[1] && !state.phase);
        assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_ERR_TOKEN);
    }
    tiku_display_t d = fixture(); publish_error = 1;
    assert(tiku_display_submit(&d, 1, 0, 0, 1, 1) == TIKU_DISPLAY_PENDING);
    assert(tiku_display_poll(&d, 1) == TIKU_DISPLAY_PENDING);
    now = 500000;
    if (!setjmp(fatal)) { (void)tiku_display_poll(&d, 1); assert(0); }
    assert(state.phase && d.front == buffers[0]);
}

int main(void)
{
    full_frame(); damage_and_refusals(); faults();
    puts("Display publication: ranges, batches, tokens, faults and parked ownership passed");
    return 0;
}
