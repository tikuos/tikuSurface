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
static const surface_scanout_ops_t ops = {submit, poll, NULL, NULL, NULL};

/** @brief Fill a rectangle from an independent input/expected-output color pair. */
static void color(tiku_rect_t r, uint32_t input, uint16_t output)
{
    for (int y = r.y; y < r.y + r.h; ++y) for (int x = r.x; x < r.x + r.w; ++x) {
        pixels[y * 800 + x] = input;
        expected[y * 800 + x] = output;
    }
}

/** @brief Convert until transport submission with a finite work bound; return the polls. */
static unsigned convert(surface_presentation_t *p)
{
    unsigned before = submits, polls = 0;
    for (unsigned i = 0; i < 4000 && submits == before; ++i, ++polls)
        assert(surface_presentation_poll(p) == TIKU_PRESENT_BUSY);
    assert(submits == before + 1);
    return polls;
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
        unsigned polls = convert(&p);
        assert(target == (previous ^ 1u));
        assert(!memcmp(scanout[target], expected, sizeof expected));
        if (f < 2) assert(damage.x == 0 && damage.y == 0 && damage.w == 800 && damage.h == 480);
        /* A buffer lacking two far-apart rectangles converts each, at most
         * 1280 pixels here, and the transport is given their bounds. */
        if (f >= 2) assert(polls <= 3);
        if (f == 2) assert(p.convert_px == 15 + 551 && damage.x == 3 && damage.y == 7 &&
            damage.w == 796 && damage.h == 472);
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

static unsigned amends, amended_buffer;
static tiku_rect_t amended;
/** @brief Record which pixels of the buffer on show were changed in place. */
static int amend(void *context, unsigned index, tiku_rect_t r)
{ (void)context; ++amends; amended_buffer = index; amended = r; return 1; }
static const surface_scanout_ops_t marking = {submit, poll, amend, NULL, NULL};

/** @brief Whether two rectangles are the same. */
static int same(tiku_rect_t a, tiku_rect_t b)
{ return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

/** @brief Present damage @p r, with the source already holding its pixels, through its flip. */
static void flip(surface_presentation_t *p, tiku_framebuffer_t *frame, tiku_presenter_t presenter,
    tiku_rect_t r)
{
    assert(tiku_framebuffer_begin(frame, &r));
    assert(tiku_framebuffer_present(frame, presenter) == TIKU_PRESENT_OK);
    completion_result = TIKU_PRESENT_BUSY;
    convert(p);
    assert(surface_presentation_poll(p) == TIKU_PRESENT_BUSY);
    completion_result = TIKU_PRESENT_OK;
    assert(surface_presentation_poll(p) == TIKU_PRESENT_OK);
}

/** @brief A touch mark shows at once, rides every frame, follows a lift after a flip
 *  and leaves both buffers as the source made them. */
static void marks(void)
{
    surface_host_t host;
    surface_config_t config = surface_host_config(&host);
    tiku_framebuffer_t *frame = tiku_framebuffer_borrow(config.allocator, pixels, sizeof pixels, 800, 480);
    surface_presentation_t p;
    const uint16_t fill = 0x9578; /* half white, half 0x3333 */
    sequence = 0; submits = 0; amends = 0; submission_result = TIKU_PRESENT_OK;
    for (unsigned i = 0; i < 800 * 480; ++i) {
        pixels[i] = 0x00ffffff; expected[i] = 0xffff; scanout[0][i] = scanout[1][i] = 0xdead;
    }
    assert(surface_presentation_init(&p, scanout[0], sizeof scanout[0], scanout[1], sizeof scanout[1],
        0, 4096, &sequence, &marking, NULL));
    tiku_presenter_t presenter = {surface_presentation_submit, &p};
    flip(&p, frame, presenter, (tiku_rect_t){0, 0, 800, 480});
    flip(&p, frame, presenter, (tiku_rect_t){0, 0, 1, 1});
    assert(!memcmp(scanout[0], expected, sizeof expected) && !memcmp(scanout[1], expected, sizeof expected));
    assert(!amends);

    /* A contact marks the buffer on show at once, and only that buffer. */
    unsigned shown = p.front;
    surface_presentation_mark(&p, 1, 100, 100);
    assert(amends == 1 && amended_buffer == shown && same(amended, (tiku_rect_t){84, 84, 32, 32}));
    assert(scanout[shown][100 * 800 + 100] == fill && scanout[shown][100 * 800 + 86] == 0x2104 &&
        scanout[shown][100 * 800 + 88] == 0xffff && scanout[shown][84 * 800 + 84] == 0xffff);
    assert(scanout[shown ^ 1u][100 * 800 + 100] == 0xffff);
    surface_presentation_mark(&p, 1, 100, 100);
    assert(amends == 1);

    /* A move puts back the old place and draws the new in one amend. */
    surface_presentation_mark(&p, 1, 300, 200);
    assert(amends == 2 && same(amended, (tiku_rect_t){84, 84, 232, 132}));
    assert(scanout[shown][100 * 800 + 100] == 0xffff && scanout[shown][200 * 800 + 300] == fill);

    /* A frame elsewhere goes out with the mark, and its region covers it. */
    color((tiku_rect_t){600, 400, 10, 10}, 0x00ff0000, 0xf800);
    flip(&p, frame, presenter, (tiku_rect_t){600, 400, 10, 10});
    assert(p.front == (shown ^ 1u) && amends == 2);
    assert(same(damage, (tiku_rect_t){0, 0, 610, 410}));
    assert(scanout[p.front][200 * 800 + 300] == fill && scanout[p.front][405 * 800 + 605] == 0xf800);

    /* A frame that changes the pixels under the mark: the mark is drawn over
     * the new pixels, and they are what it leaves behind. */
    color((tiku_rect_t){290, 190, 20, 20}, 0x000000ff, 0x001f);
    flip(&p, frame, presenter, (tiku_rect_t){290, 190, 20, 20});
    assert(scanout[p.front][200 * 800 + 300] == 0x1998); /* half 0x001f, half 0x3333 */

    /* Lifted during a submission: the buffer on show keeps the mark until
     * the flip, then the new one is cleared and amended. */
    tiku_rect_t r = {10, 10, 2, 2};
    assert(tiku_framebuffer_begin(frame, &r));
    assert(tiku_framebuffer_present(frame, presenter) == TIKU_PRESENT_OK);
    completion_result = TIKU_PRESENT_BUSY;
    convert(&p);
    unsigned before = amends, held = p.front;
    surface_presentation_mark(&p, 0, 0, 0);
    assert(amends == before && scanout[held][200 * 800 + 300] == 0x1998);
    completion_result = TIKU_PRESENT_OK;
    assert(surface_presentation_poll(&p) == TIKU_PRESENT_OK);
    assert(amends == before + 1 && amended_buffer == p.front && same(amended, (tiku_rect_t){284, 184, 32, 32}));
    assert(scanout[p.front][200 * 800 + 300] == 0x001f);

    /* The buffer that last held it is converted without it: both end as the source. */
    flip(&p, frame, presenter, (tiku_rect_t){0, 0, 1, 1});
    flip(&p, frame, presenter, (tiku_rect_t){0, 0, 1, 1});
    assert(!memcmp(scanout[0], expected, sizeof expected) && !memcmp(scanout[1], expected, sizeof expected));

    /* A mouse crossing the bottom edge leaves the pixels under its old
     * position intact; removing it restores its last hotspot too. */
    surface_presentation_cursor(&p, 0, 32, 799, 479);
    assert(scanout[p.front][479 * 800 + 799] == 0);
    surface_presentation_cursor(&p, 0, 32, 0, 0);
    assert(amended.w == 32 && amended.h == 32);
    assert(scanout[p.front][24 * 800 + 2] == 0);
    surface_presentation_cursor(&p, 0, 12, 0, 0);
    assert(scanout[p.front][24 * 800 + 2] == expected[24 * 800 + 2]);
    assert(scanout[p.front][479 * 800 + 799] == 0xffff);
    assert(scanout[p.front][0] == 0);
    surface_presentation_cursor(&p, -1, 0, 0, 0);
    assert(scanout[p.front][0] == 0xffff);

    /* Without an amend operation no mark is drawn. */
    assert(surface_presentation_init(&p, scanout[0], sizeof scanout[0], scanout[1], sizeof scanout[1],
        p.front, 4096, &sequence, &ops, NULL));
    surface_presentation_mark(&p, 1, 100, 100);
    assert(scanout[p.front][100 * 800 + 100] == 0xffff && !p.mark_on);
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

/* A hardware converter: it converts a part at once but reports it running
 * for `runs` polls; it may refuse, or fail after writing garbage. */
static unsigned hw_calls, hw_runs, hw_left, hw_refuse, hw_fail, hw_polls;
static tiku_rect_t hw_parts[4];

static int hw_convert(void *context, unsigned back, const tiku_present_view_t *v, tiku_rect_t r)
{
    (void)context;
    if (hw_calls < 4) hw_parts[hw_calls] = r;
    hw_calls++;
    if (hw_refuse) return -1;
    for (int y = r.y; y < r.y + r.h; ++y) for (int x = r.x; x < r.x + r.w; ++x) {
        uint32_t c = ((const uint32_t *)((const unsigned char *)v->pixels + y * v->stride))[x];
        scanout[back][y * 800 + x] = hw_fail ? 0xbeef : (uint16_t)(((c >> 8) & 0xf800u) |
            ((c >> 5) & 0x07e0u) | ((c >> 3) & 0x001fu));
    }
    hw_left = hw_runs;
    return 0;
}

static int hw_poll(void *context)
{
    (void)context;
    hw_polls++;
    if (hw_left) { hw_left--; return 1; }
    return hw_fail ? -1 : 0;
}

static const surface_scanout_ops_t hardware_ops = {submit, poll, NULL, hw_convert, hw_poll};

/** @brief One frame of @p r over a full first frame, through the hardware
 *  converter: the polls until the transport is given it. */
static unsigned hw_frame(surface_presentation_t *p, tiku_framebuffer_t *frame,
                         tiku_presenter_t presenter, tiku_rect_t r, uint32_t in, uint16_t out)
{
    unsigned polls;

    assert(tiku_framebuffer_begin(frame, &r));
    color(r, in, out);
    assert(tiku_framebuffer_present(frame, presenter) == TIKU_PRESENT_OK);
    polls = convert(p);
    assert(!memcmp(scanout[target], expected, sizeof expected));
    completion_result = TIKU_PRESENT_OK;
    assert(surface_presentation_poll(p) == TIKU_PRESENT_OK);
    completion_result = TIKU_PRESENT_BUSY;
    return polls;
}

/** @brief Parts go to the hardware converter one at a time; a refusal or a
 *  failure leaves the frame to the CPU, which converts it whole. */
static void hardware(void)
{
    surface_host_t host;
    surface_config_t config = surface_host_config(&host);
    tiku_framebuffer_t *frame = tiku_framebuffer_borrow(config.allocator, pixels, sizeof pixels, 800, 480);
    tiku_presenter_t presenter;
    surface_presentation_t p;
    unsigned polls;

    sequence = 0; submits = 0;
    submission_result = TIKU_PRESENT_OK; completion_result = TIKU_PRESENT_BUSY;
    for (unsigned i = 0; i < 800 * 480; ++i) { pixels[i] = 0x00ffffff; expected[i] = 0xffff; }
    assert(surface_presentation_init(&p, scanout[0], sizeof scanout[0], scanout[1], sizeof scanout[1],
        0, 64, &sequence, &hardware_ops, NULL));
    presenter = (tiku_presenter_t){surface_presentation_submit, &p};
    /* The first frame is the whole screen, in one part. */
    hw_runs = 3;
    polls = hw_frame(&p, frame, presenter, (tiku_rect_t){0, 0, 800, 480}, 0x00ffffff, 0xffff);
    assert(hw_calls == 1 && hw_parts[0].w == 800 && hw_parts[0].h == 480 && polls <= 6);
    hw_frame(&p, frame, presenter, (tiku_rect_t){0, 0, 800, 480}, 0x00ffffff, 0xffff);
    /* Two far-apart parts: two jobs, each started at its own corner. */
    hw_calls = 0;
    polls = hw_frame(&p, frame, presenter, (tiku_rect_t){3, 7, 5, 3}, 0x00ff0000, 0xf800);
    assert(hw_calls == 1);
    hw_calls = 0;
    color((tiku_rect_t){3, 7, 5, 3}, 0x00ff0000, 0xf800);
    polls = hw_frame(&p, frame, presenter, (tiku_rect_t){780, 450, 19, 29}, 0x0000ff00, 0x07e0);
    assert(hw_calls == 2 && hw_parts[0].x == 3 && hw_parts[0].y == 7 &&
           hw_parts[1].x == 780 && hw_parts[1].y == 450 && polls <= 10);
    /* Refused: the CPU converts the frame's two parts, 64 pixels a poll. */
    hw_frame(&p, frame, presenter, (tiku_rect_t){0, 0, 64, 10}, 0x000000ff, 0x001f);
    hw_refuse = 1;
    hw_calls = 0;
    polls = hw_frame(&p, frame, presenter, (tiku_rect_t){200, 300, 64, 10}, 0x00ffff00, 0xffe0);
    assert(hw_calls == 1 && polls >= 20);
    hw_refuse = 0;
    /* Failed after writing garbage: the CPU converts the part again. */
    hw_fail = 1;
    hw_calls = 0;
    polls = hw_frame(&p, frame, presenter, (tiku_rect_t){100, 100, 64, 10}, 0x00ff00ff, 0xf81f);
    assert(hw_calls == 1 && polls >= 10);
    hw_fail = 0;
    /* Closed while a job runs: it is waited for first. */
    hw_runs = 5;
    assert(tiku_framebuffer_begin(frame, &(tiku_rect_t){0, 0, 8, 8}));
    assert(tiku_framebuffer_present(frame, presenter) == TIKU_PRESENT_OK);
    assert(surface_presentation_poll(&p) == TIKU_PRESENT_BUSY && p.hw);
    hw_polls = 0;
    assert(surface_presentation_closed(&p));
    assert(!p.hw && hw_polls >= 5);
    assert(p.failed && tiku_framebuffer_begin(frame, NULL));
    assert(tiku_framebuffer_abandon(frame));
    assert(tiku_framebuffer_destroy(&frame) && !host.memory.live);
}

/** @brief An early lease ends with conversion: the owner draws again while
 *  the transport waits, and the next frame is refused until the panel has
 *  this one. */
static void early_lease(void)
{
    surface_host_t host;
    surface_config_t config = surface_host_config(&host);
    tiku_framebuffer_t *frame = tiku_framebuffer_borrow(config.allocator, pixels, sizeof pixels, 800, 480);
    surface_presentation_t p;
    sequence = 0; submits = 0;
    submission_result = TIKU_PRESENT_OK; completion_result = TIKU_PRESENT_BUSY;
    assert(surface_presentation_init(&p, scanout[0], sizeof scanout[0], scanout[1], sizeof scanout[1],
        0, 4096, &sequence, &ops, NULL));
    p.early = 1;
    tiku_presenter_t presenter = {surface_presentation_submit, &p};
    assert(tiku_framebuffer_begin(frame, NULL));
    assert(tiku_framebuffer_present(frame, presenter) == TIKU_PRESENT_OK);
    /* Held while the source is read. */
    assert(surface_presentation_poll(&p) == TIKU_PRESENT_BUSY && !tiku_framebuffer_begin(frame, NULL));
    convert(&p);
    assert(surface_presentation_poll(&p) == TIKU_PRESENT_BUSY && p.front == 0);
    assert(tiku_framebuffer_result(frame) == TIKU_PRESENT_OK);
    assert(tiku_framebuffer_begin(frame, NULL));
    assert(tiku_framebuffer_present(frame, presenter) == TIKU_PRESENT_BUSY);
    completion_result = TIKU_PRESENT_OK;
    assert(surface_presentation_poll(&p) == TIKU_PRESENT_OK && p.front == 1 && !p.phase);
    completion_result = TIKU_PRESENT_BUSY;
    /* The panel has the frame: the next one is taken. */
    assert(tiku_framebuffer_present(frame, presenter) == TIKU_PRESENT_OK);
    convert(&p);
    /* A transport failure after the lease ended fails the presentation. */
    completion_result = TIKU_PRESENT_ERROR;
    assert(surface_presentation_poll(&p) == TIKU_PRESENT_ERROR && p.failed && p.front == 1);
    assert(tiku_framebuffer_result(frame) == TIKU_PRESENT_OK);
    assert(tiku_framebuffer_begin(frame, NULL));
    assert(tiku_framebuffer_present(frame, presenter) == TIKU_PRESENT_ERROR);
    (void)tiku_framebuffer_abandon(frame);
    assert(tiku_framebuffer_destroy(&frame) && !host.memory.live);
}

int main(void)
{
    frames(); refusals_and_errors(); layouts_and_retries(); marks(); hardware(); early_lease();
    puts("Presentation: bounded conversion, colors, buffer age, separate parts, source leases, early leases, faults, touch marks and the hardware converter passed");
    return 0;
}
