/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_display_binding.c - firmware result mapping and retained display lifecycle.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "display.h"
#include "runtime.h"
#include <interfaces/display/tiku_display.h>
#include <arch/esp32s31/tiku_display_arch.h>
#include <arch/esp32s31/tiku_psram_arch.h>
#include <arch/esp32s31/tiku_ppa_arch.h>
#include <arch/esp32s31/tiku_runtime_arch.h>
#include <assert.h>
#include <stdio.h>

static _Alignas(64) uint16_t pixels[2][800 * 480];
static int owner = 1, initialized, init_error, pair_error, close_error, health_error;
static int submit_result = TIKU_DISPLAY_PENDING, poll_result = TIKU_DISPLAY_PENDING;
static unsigned calls;
static uint32_t caps = TIKU_DISPLAY_CAP_FLIP | TIKU_DISPLAY_CAP_ASYNC;
static tiku_display_t *active;
int surface_os_in_owner(void) { return owner; }
/* The S31 pixel accelerator: a job is recorded, its end is reported by
 * poll_rc, and the job-end callback is kept as notify registered it. */
static void (*job_end)(void);
static unsigned wakes, jobs;
static int convert_rc = TIKU_S31_PPA_OK, ppa_rc = TIKU_S31_PPA_RUNNING;
static tiku_s31_picture_t job_out, job_in;
void surface_os_poll_isr(void) { wakes++; }
uint64_t tiku_s31_time(void) { return 1600; }
int tiku_s31_ppa_convert(const tiku_s31_picture_t *out, uint16_t ox, uint16_t oy,
    const tiku_s31_picture_t *in, uint16_t ix, uint16_t iy, uint16_t w, uint16_t h)
{
    assert(ox == ix && oy == iy && w && h);
    job_out = *out; job_in = *in; jobs++;
    return convert_rc;
}
int tiku_s31_ppa_poll(void) { return ppa_rc; }
int tiku_s31_ppa_notify(void (*fn)(void)) { job_end = fn; return 0; }
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
static unsigned syncs, sync_error;
static uintptr_t spans[8][2];
int tiku_s31_psram_sync(void *address, uint32_t length, uint8_t invalidate)
{
    uintptr_t a = (uintptr_t)address;
    assert(!invalidate && length && !(a % TIKU_S31_PSRAM_LINE) && !(length % TIKU_S31_PSRAM_LINE));
    if (syncs < 8) { spans[syncs][0] = a; spans[syncs][1] = a + length; }
    ++syncs; return sync_error;
}
/** @brief Whether sync span @p i covers row @p row's pixels @p x..x+w of buffer @p b. */
static int covers(unsigned i, unsigned b, unsigned row, unsigned x, unsigned w)
{
    uintptr_t first = (uintptr_t)(pixels[b] + row * 800u + x);
    return spans[i][0] <= first && spans[i][1] >= first + w * sizeof(uint16_t) &&
        spans[i][1] - spans[i][0] <= w * sizeof(uint16_t) + 2u * TIKU_S31_PSRAM_LINE;
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
    assert(!surface_os_display_amend(0, 100, 200, 32, 3));
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
    /* The buffer on show takes a write-back row by row; the back buffer,
     * a rectangle off the screen and a failed write-back are refused. */
    assert(!surface_os_display_amend(1, 100, 200, 32, 3) && !syncs);
    assert(!surface_os_display_amend(0, 790, 200, 32, 3) && !surface_os_display_amend(0, 100, 200, 0, 3));
    assert(surface_os_display_amend(0, 100, 200, 32, 3) && syncs == 3);
    for (unsigned i = 0; i < 3; ++i) assert(covers(i, 0, 200 + i, 100, 32));
    sync_error = 1; syncs = 0;
    assert(!surface_os_display_amend(0, 100, 200, 32, 3) && syncs == 1);
    sync_error = 0; syncs = 0;
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
    assert(!surface_os_display_amend(0, 0, 0, 800, 1) && surface_os_display_amend(1, 0, 479, 800, 1));
    assert(syncs == 1 && covers(0, 1, 479, 0, 800));
    assert(surface_os_display_submit(1, 31, 4, 2, 3, 23) == SURFACE_SCANOUT_ERROR);
    /* Conversion into the back buffer, 0 after the flips above, by the
     * accelerator; its end wakes the owner through the callback open
     * registered. */
    static uint32_t argb[800 * 480];
    assert(job_end && surface_os_display_convert_wakes());
    assert(surface_os_display_convert(1, argb, 3200, 0, 0, 800, 480) == SURFACE_SCANOUT_REFUSED);
    assert(surface_os_display_convert(0, argb, 3199, 0, 0, 800, 480) == SURFACE_SCANOUT_REFUSED);
    assert(surface_os_display_convert(0, argb, 3200, 700, 0, 101, 480) == SURFACE_SCANOUT_REFUSED && !jobs);
    assert(surface_os_display_convert(0, argb, 3200, 10, 20, 30, 40) == SURFACE_SCANOUT_OK && jobs == 1);
    assert(job_out.base == pixels[0] && job_out.bpp == 2 && job_in.base == argb && job_in.width == 800);
    assert(surface_os_display_convert_poll() == SURFACE_SCANOUT_BUSY);
    job_end();
    ppa_rc = TIKU_S31_PPA_OK;
    assert(wakes == 1 && surface_os_display_convert_poll() == SURFACE_SCANOUT_OK);
    convert_rc = -1;
    assert(surface_os_display_convert(0, argb, 3200, 10, 20, 30, 40) == SURFACE_SCANOUT_REFUSED);
    ppa_rc = -1;
    assert(surface_os_display_convert_poll() == SURFACE_SCANOUT_ERROR);
    uint32_t done, failed, woken, last;
    surface_os_display_convert_stats(&done, &failed, &woken, &last);
    assert(done == 1 && failed == 2 && woken == 1);
    assert(surface_os_display_close() && surface_os_display_close() && !initialized && !job_end);
    puts("Display binding: context, partial open, close retry, back buffer, shown-buffer write-back, terminal result mapping and accelerator conversion passed");
    return 0;
}
