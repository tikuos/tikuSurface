/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * presentation.h - retained render views and paired RGB565 conversion.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_PRESENTATION_H
#define SURFACE_PRESENTATION_H
#include "tiku_framebuffer.h"

/** @brief Submit refuses BUSY/error without retention; poll error proves DMA retirement. */
typedef struct {
    tiku_present_result_t (*submit)(void *, unsigned, tiku_rect_t, uint64_t);
    tiku_present_result_t (*poll)(void *, uint64_t);
} surface_scanout_ops_t;

/** @brief Serialized provider; caller retains buffers, operations and sequence storage. */
typedef struct {
    uint16_t *buffers[2];
    tiku_rect_t pending[2], region;
    tiku_present_view_t source;
    const surface_scanout_ops_t *ops;
    void *context;
    uint64_t *sequence, ticket;
    unsigned front, phase, failed, x, y, pixel_budget;
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
/** @brief Complete retained source with error after the transport proves hardware closed. */
int surface_presentation_closed(surface_presentation_t *);
#endif
