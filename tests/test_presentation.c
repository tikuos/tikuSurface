/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_presentation.c - independent pixels, alternating damage and retained sources.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host.h"
#include "presentation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static _Alignas(64) uint16_t scanout[2][800 * 480];
static _Alignas(64) uint32_t pixels[800 * 480];
static _Alignas(64) uint32_t padded[804 * 480];
static uint16_t expected[800 * 480];
static uint64_t sequence, submitted;
static unsigned submits, target;
static tiku_rect_t damage;
static tiku_present_result_t submission_result, completion_result;

/** @brief Forward a borrowed source with a padded layout to the presentation provider. */
static tiku_present_result_t padded_submit(void *context, const tiku_present_view_t *view)
{
    tiku_present_view_t v = *view;
    v.pixels = padded; v.stride = 804 * sizeof(uint32_t); v.bytes = sizeof padded;
    return surface_presentation_submit(context, &v);
}

/** @brief Observe the backend request without completing the retained source. */
static tiku_present_result_t submit(void *context, unsigned index, tiku_rect_t region, uint64_t token)
{
    (void)context; ++submits;
    if (submission_result == TIKU_PRESENT_OK) { target = index; damage = region; submitted = token; }
    return submission_result;
}
/** @brief Return a controlled completion only for the submitted transport ticket. */
static tiku_present_result_t poll(void *context, uint64_t token)
{ (void)context; assert(token == submitted); return completion_result; }
static const surface_scanout_ops_t ops = {submit, poll};

/** @brief Fill a rectangle from an independent input/expected-output color pair. */
static void color(tiku_rect_t r, uint32_t input, uint16_t output)
{
    for (int y = r.y; y < r.y + r.h; ++y) for (int x = r.x; x < r.x + r.w; ++x) {
        pixels[y * 800 + x] = input;
        expected[y * 800 + x] = output;
    }
}

/** @brief Convert until transport submission with a finite work bound. */
static void convert(surface_presentation_t *p)
{
    unsigned before = submits;
    for (unsigned i = 0; i < 4000 && submits == before; ++i)
        assert(surface_presentation_poll(p) == TIKU_PRESENT_BUSY);
    assert(submits == before + 1);
}

/** @brief Compare complete scanout buffers after disjoint damage alternates their age. */
static void frames(void)
{
    surface_host_t host;
    surface_config_t config = surface_host_config(&host);
    tiku_framebuffer_t *frame = tiku_framebuffer_borrow(config.allocator, pixels, sizeof pixels, 800, 480);
    surface_presentation_t p;
    sequence = 0; submits = 0;
    submission_result = TIKU_PRESENT_OK; completion_result = TIKU_PRESENT_BUSY;
    assert(surface_presentation_init(&p, scanout[0], sizeof scanout[0], scanout[1], sizeof scanout[1],
        0, 1024, &sequence, &ops, NULL));
    tiku_presenter_t presenter = {surface_presentation_submit, &p};
    const uint32_t input[] = {0, 0x00ffffff, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ff00ff, 0xaaffff00};
    const uint16_t output[] = {0, 0xffff, 0xf800, 0x07e0, 0x001f, 0xf81f, 0xffe0};
    for (unsigned i = 0; i < 800 * 480; ++i) {
        pixels[i] = input[i % 7]; expected[i] = output[i % 7];
        scanout[0][i] = scanout[1][i] = 0xdead;
    }
    tiku_rect_t rectangles[] = {{0, 0, 1, 1}, {3, 7, 5, 3}, {780, 450, 19, 29},
        {90, 30, 4, 6}, {0, 479, 800, 1}, {799, 0, 1, 480}};
    for (unsigned f = 0; f < sizeof rectangles / sizeof rectangles[0]; ++f) {
        tiku_rect_t r = rectangles[f];
        assert(tiku_framebuffer_begin(frame, &r));
        if (f) color(r, input[f], output[f]);
        unsigned previous = p.front;
        assert(tiku_framebuffer_present(frame, presenter) == TIKU_PRESENT_OK);
        assert(!tiku_framebuffer_begin(frame, NULL));
        assert(!tiku_framebuffer_destroy(&frame));
        if (!f) {
            assert(surface_presentation_poll(&p) == TIKU_PRESENT_BUSY && !submits);
            assert(!memcmp(scanout[1], expected, 1024 * sizeof(uint16_t)));
            for (unsigned i = 1024; i < 800 * 480; ++i) assert(scanout[1][i] == 0xdead);
        }
        convert(&p);
        assert(target == (previous ^ 1u));
        assert(!memcmp(scanout[target], expected, sizeof expected));
        if (f < 2) assert(damage.x == 0 && damage.y == 0 && damage.w == 800 && damage.h == 480);
        assert(surface_presentation_poll(&p) == TIKU_PRESENT_BUSY && p.front == previous);
        completion_result = (tiku_present_result_t)99;
        assert(surface_presentation_poll(&p) == TIKU_PRESENT_BUSY && !tiku_framebuffer_begin(frame, NULL));
        completion_result = TIKU_PRESENT_OK;
        assert(surface_presentation_poll(&p) == TIKU_PRESENT_OK && p.front == target);
        completion_result = TIKU_PRESENT_BUSY;
    }
    assert(sequence == 6);
    assert(tiku_framebuffer_destroy(&frame) && !host.memory.live);
}

/** @brief Invalid layouts refuse retention; transport failures require proven close. */
static void refusals_and_errors(void)
{
    surface_host_t host;
    surface_config_t config = surface_host_config(&host);
    tiku_framebuffer_t *frame = tiku_framebuffer_borrow(config.allocator, pixels, sizeof pixels, 800, 480);
    surface_presentation_t p;
    assert(!surface_presentation_init(&p, scanout[0], sizeof scanout[0], scanout[0], sizeof scanout[0], 0, 1, &sequence, &ops, NULL));
    assert(!surface_presentation_init(&p, scanout[0], 64, scanout[1], sizeof scanout[1], 0, 1, &sequence, &ops, NULL));
    for (unsigned mode = 0; mode < 4; ++mode) {
        assert(surface_presentation_init(&p, scanout[0], sizeof scanout[0], scanout[1], sizeof scanout[1], 0, 4096, &sequence, &ops, NULL));
        assert(tiku_framebuffer_begin(frame, NULL));
        submission_result = mode == 1 ? TIKU_PRESENT_ERROR : TIKU_PRESENT_OK;
        completion_result = mode == 2 ? TIKU_PRESENT_ERROR : TIKU_PRESENT_BUSY;
        assert(tiku_framebuffer_present(frame, (tiku_presenter_t){surface_presentation_submit, &p}) == TIKU_PRESENT_OK);
        if (mode == 0) assert(surface_presentation_closed(&p));
        else if (mode == 1) {
            tiku_present_result_t result = TIKU_PRESENT_BUSY;
            for (unsigned i = 0; i < 1000 && result == TIKU_PRESENT_BUSY; ++i) result = surface_presentation_poll(&p);
            assert(result == TIKU_PRESENT_ERROR);
        } else {
            convert(&p);
            if (mode == 2) assert(surface_presentation_poll(&p) == TIKU_PRESENT_ERROR);
            else {
                assert(surface_presentation_poll(&p) == TIKU_PRESENT_BUSY);
                assert(!tiku_framebuffer_destroy(&frame));
                assert(surface_presentation_closed(&p));
            }
        }
        assert(p.failed && tiku_framebuffer_begin(frame, NULL));
        assert(tiku_framebuffer_abandon(frame));
    }
    assert(tiku_framebuffer_destroy(&frame) && !host.memory.live);
}

/** @brief Padded rows, refused submissions and exhausted tickets preserve source ownership. */
static void layouts_and_retries(void)
{
    surface_host_t host;
    surface_config_t config = surface_host_config(&host);
    tiku_framebuffer_t *frame = tiku_framebuffer_borrow(config.allocator, pixels, sizeof pixels, 800, 480);
    surface_presentation_t p;
    sequence = 10;
    assert(surface_presentation_init(&p, scanout[0], sizeof scanout[0], scanout[1], sizeof scanout[1], 0, 4096, &sequence, &ops, NULL));
    assert(surface_presentation_submit(&p, NULL) == TIKU_PRESENT_ERROR);
    tiku_present_view_t invalid = {.owner = frame, .token = 1, .pixels = pixels,
        .bytes = sizeof pixels, .width = 800, .height = 480, .stride = 3200,
        .format = TIKU_PIXELS_XRGB8888, .damage = {799, 0, 2, 1}};
    assert(surface_presentation_submit(&p, &invalid) == TIKU_PRESENT_ERROR && !p.phase);
    invalid.damage = (tiku_rect_t){0, 0, 800, 480}; invalid.stride = 3196;
    assert(surface_presentation_submit(&p, &invalid) == TIKU_PRESENT_ERROR && !p.phase);
    invalid.stride = 3200; invalid.bytes--;
    assert(surface_presentation_submit(&p, &invalid) == TIKU_PRESENT_ERROR && !p.phase);
    for (unsigned y = 0; y < 480; ++y) for (unsigned x = 0; x < 804; ++x)
        padded[y * 804 + x] = x >= 800 ? 0x00ff0000 : (y & 1 ? 0x000000ff : 0x0000ff00);
    assert(tiku_framebuffer_begin(frame, NULL));
    assert(tiku_framebuffer_present(frame, (tiku_presenter_t){padded_submit, &p}) == TIKU_PRESENT_OK);
    assert(surface_presentation_submit(&p, NULL) == TIKU_PRESENT_BUSY);
    submission_result = TIKU_PRESENT_BUSY;
    convert(&p);
    assert(sequence == 11 && p.ticket == 11 && !tiku_framebuffer_begin(frame, NULL));
    assert(surface_presentation_poll(&p) == TIKU_PRESENT_BUSY && sequence == 11);
    for (unsigned y = 0; y < 480; ++y) for (unsigned x = 0; x < 800; ++x)
        assert(scanout[1][y * 800 + x] == (y & 1 ? 0x001f : 0x07e0));
    submission_result = TIKU_PRESENT_OK;
    assert(surface_presentation_poll(&p) == TIKU_PRESENT_BUSY && submitted == 11);
    completion_result = TIKU_PRESENT_OK;
    assert(surface_presentation_poll(&p) == TIKU_PRESENT_OK);
    sequence = UINT64_MAX;
    assert(tiku_framebuffer_begin(frame, NULL));
    assert(tiku_framebuffer_present(frame, (tiku_presenter_t){padded_submit, &p}) == TIKU_PRESENT_OK);
    tiku_present_result_t result = TIKU_PRESENT_BUSY;
    unsigned before = submits;
    for (unsigned i = 0; i < 1000 && result == TIKU_PRESENT_BUSY; ++i) result = surface_presentation_poll(&p);
    assert(result == TIKU_PRESENT_ERROR && submits == before && sequence == UINT64_MAX);
    assert(tiku_framebuffer_destroy(&frame) && !host.memory.live);
}

int main(void)
{
    frames(); refusals_and_errors(); layouts_and_retries();
    puts("Presentation: bounded conversion, colors, buffer age, source leases and faults passed");
    return 0;
}
