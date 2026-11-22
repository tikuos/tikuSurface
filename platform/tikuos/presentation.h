/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * presentation.h - retained render views and paired RGB565 conversion.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_PRESENTATION_H
#define SURFACE_PRESENTATION_H
#include "tiku_framebuffer.h"

/**
 * @brief Submit refuses BUSY/error without retention; poll error proves DMA retirement.
 * @note amend makes pixels changed in place in the buffer on show visible; it
 * is called only between submissions. Without it no touch mark is drawn.
 */
typedef struct {
    tiku_present_result_t (*submit)(void *, unsigned, tiku_rect_t, uint64_t);
    tiku_present_result_t (*poll)(void *, uint64_t);
    int (*amend)(void *, unsigned, tiku_rect_t);
} surface_scanout_ops_t;

/** @brief Rectangles a scanout buffer keeps apart while each pair's union
 *  would be more than twice their area. */
#define SURFACE_PRESENT_PARTS 2

/** @brief Side of the square a touch mark covers, centred on the contact. */
#define SURFACE_MARK_SIZE 32

/**
 * @brief Serialized provider; caller retains buffers, operations and sequence storage.
 * @note pending lists the rectangles each buffer lacks. A frame converts its
 * back buffer's parts, convert_px pixels, and submits their bounding region.
 * marked is where each buffer holds the touch mark, under the pixels it
 * covers, and drawn the mark_seq it was drawn for.
 */
typedef struct {
    uint16_t *buffers[2];
    tiku_rect_t pending[2][SURFACE_PRESENT_PARTS], region;
    tiku_present_view_t source;
    const surface_scanout_ops_t *ops;
    void *context;
    uint64_t *sequence, ticket;
    unsigned front, phase, failed, x, y, pixel_budget, part;
    uint32_t convert_px;
    int mark_on, mark_x, mark_y;
    unsigned mark_seq, drawn[2];
    tiku_rect_t marked[2];
    uint16_t under[2][SURFACE_MARK_SIZE * SURFACE_MARK_SIZE];
} surface_presentation_t;

/**
 * @brief Bind disjoint 64-byte-aligned 800x480 RGB565 buffers, initially invalid.
 * @note Call on an idle/closed provider. The sequence starts at zero once and
 * persists across reopen. Pixel budgets are 1..4096; front is the scanned index.
 */
int surface_presentation_init(surface_presentation_t *, uint16_t *, size_t,
    uint16_t *, size_t, unsigned front, unsigned pixel_budget, uint64_t *sequence,
    const surface_scanout_ops_t *, void *context);
/** @brief Retain a valid SDK view; BUSY/error retains nothing. */
tiku_present_result_t surface_presentation_submit(void *, const tiku_present_view_t *);
/** @brief Convert at most pixel_budget pixels, submit once or poll once per call. */
tiku_present_result_t surface_presentation_poll(surface_presentation_t *);
/**
 * @brief Show a touch mark centred on (@p x, @p y), or none when @p on is 0.
 * @note Between submissions the buffer on show changes at once; during one it
 * follows when the flip completes. Each frame submitted carries the mark.
 */
void surface_presentation_mark(surface_presentation_t *, int on, int x, int y);
/** @brief Complete retained source with error after the transport proves hardware closed. */
int surface_presentation_closed(surface_presentation_t *);
#endif
