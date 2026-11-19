/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_display_binding.c - firmware result mapping and retained display lifecycle.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "display.h"
#include "runtime.h"
#include <interfaces/display/tiku_display.h>
#include <arch/esp32s31/tiku_display_arch.h>
#include <assert.h>
#include <stdio.h>

static _Alignas(64) uint16_t pixels[2][800 * 480];
static int owner = 1, initialized, init_error, pair_error, close_error, health_error;
static int submit_result = TIKU_DISPLAY_PENDING, poll_result = TIKU_DISPLAY_PENDING;
static unsigned calls;
static uint32_t caps = TIKU_DISPLAY_CAP_FLIP | TIKU_DISPLAY_CAP_ASYNC;
static tiku_display_t *active;
int surface_os_in_owner(void) { return owner; }
int tiku_display_init(tiku_display_t *d, void *front, uint16_t w, uint16_t h)
{
    assert(!initialized && front == pixels[0] && w == 800 && h == 480);
    ++calls;
    if (init_error) return TIKU_DISPLAY_ERR_STATE;
    initialized = 1; active = d; d->front = d->fb = front;
    return 0;
}
uint32_t tiku_display_caps(void) { return caps; }
int tiku_display_set_buffers(tiku_display_t *d, void *front, void *back)
{
    assert(initialized && d == active && front == pixels[0] && back == pixels[1]);
    if (pair_error) return TIKU_DISPLAY_ERR_STATE;
    d->front = front; d->fb = back; return 0;
}
int tiku_display_submit(tiku_display_t *d, uint64_t token, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    assert(initialized && d == active && token == 23 && x == 31 && y == 4 && w == 2 && h == 3);
    ++calls; return submit_result;
}
int tiku_display_poll(tiku_display_t *d, uint64_t token)
{
    assert(initialized && d == active && token == 23); ++calls;
    if (poll_result == TIKU_DISPLAY_OK) { void *old = d->front; d->front = d->fb; d->fb = old; }
    return poll_result;
}
int tiku_s31_display_poll(tiku_s31_display_stats_t *stats)
{ assert(initialized && !stats); ++calls; return health_error; }
int tiku_s31_display_close(tiku_display_t *d)
{
    assert(initialized && d == active); ++calls;
    if (close_error) return TIKU_DISPLAY_ERR_STATE;
    initialized = 0; return 0;
}

/** @brief Require owner context without changing hardware or pending ownership. */
static void context_refusal(void)
{
    unsigned before = calls; owner = 0;
    assert(!surface_os_display_open(pixels[0], pixels[1]));
    assert(surface_os_display_submit(1, 31, 4, 2, 3, 23) == SURFACE_SCANOUT_ERROR);
    assert(surface_os_display_poll(23) == SURFACE_SCANOUT_REFUSED);
    assert(!surface_os_display_close() && !surface_os_display_health());
    assert(calls == before); owner = 1;
}
int main(void)
{
    context_refusal();
    init_error = 1;
    assert(!surface_os_display_open(pixels[0], pixels[1]) && surface_os_display_close());
    init_error = 0;
    for (unsigned failure = 0; failure < 2; ++failure) {
        caps = failure ? TIKU_DISPLAY_CAP_FLIP | TIKU_DISPLAY_CAP_ASYNC : TIKU_DISPLAY_CAP_FLIP;
        pair_error = failure;
        assert(!surface_os_display_open(pixels[0], pixels[1]) && initialized);
        close_error = 1;
        assert(!surface_os_display_close() && initialized);
        close_error = 0;
        assert(surface_os_display_close() && !initialized);
    }
    pair_error = 0;
    assert(surface_os_display_open(pixels[0], pixels[1]));
    context_refusal();
    assert(!surface_os_display_open(pixels[0], pixels[1]));
    assert(surface_os_display_health()); health_error = 1;
    assert(!surface_os_display_health()); health_error = 0;
    unsigned before = calls;
    assert(surface_os_display_submit(0, 31, 4, 2, 3, 23) == SURFACE_SCANOUT_ERROR);
    assert(surface_os_display_submit(1, 799, 4, 2, 3, 23) == SURFACE_SCANOUT_ERROR && calls == before);
    assert(surface_os_display_submit(1, 31, 4, 2, 3, 23) == SURFACE_SCANOUT_OK);
    submit_result = TIKU_DISPLAY_ERR_STATE;
    assert(surface_os_display_submit(1, 31, 4, 2, 3, 23) == SURFACE_SCANOUT_ERROR);
    assert(surface_os_display_poll(23) == SURFACE_SCANOUT_BUSY);
    const int refusals[] = {TIKU_DISPLAY_ERR_STATE, TIKU_DISPLAY_ERR_TOKEN,
        TIKU_DISPLAY_ERR_INVALID, TIKU_DISPLAY_ERR_UNSUPPORTED};
    for (unsigned i = 0; i < sizeof refusals / sizeof refusals[0]; ++i) {
        poll_result = refusals[i];
        assert(surface_os_display_poll(23) == SURFACE_SCANOUT_REFUSED && initialized);
    }
    poll_result = TIKU_DISPLAY_ERR_RETIRED;
    assert(surface_os_display_poll(23) == SURFACE_SCANOUT_ERROR && initialized);
    poll_result = TIKU_DISPLAY_OK;
    assert(surface_os_display_poll(23) == SURFACE_SCANOUT_OK);
    assert(surface_os_display_submit(1, 31, 4, 2, 3, 23) == SURFACE_SCANOUT_ERROR);
    assert(surface_os_display_close() && surface_os_display_close() && !initialized);
    puts("Display binding: context, partial open, close retry, back buffer and terminal result mapping passed");
    return 0;
}
