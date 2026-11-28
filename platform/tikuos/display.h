/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * display.h - firmware scanout operations without SDK types.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_DISPLAY_H
#define SURFACE_DISPLAY_H
#include <stdint.h>
#include "../../system/product/surface_panel.h"
/** @brief OK accepts/completes; ERROR refuses submit or proves failed DMA retirement. */
enum { SURFACE_SCANOUT_OK = 0, SURFACE_SCANOUT_BUSY = 1,
    SURFACE_SCANOUT_ERROR = -1, SURFACE_SCANOUT_REFUSED = -2 };
/** @brief Open a paired board display; caller retains buffers until close succeeds. */
int surface_os_display_open(uint16_t *front, uint16_t *back);
/** @brief Submit the named back buffer; refusal retains no new request. */
int surface_os_display_submit(unsigned back, unsigned x, unsigned y,
    unsigned w, unsigned h, uint64_t token);
/**
 * @brief Write back a rectangle of the buffer on show after drawing into it.
 * @return Nonzero once the rectangle's cache lines reached PSRAM; zero for the
 *         back buffer, a rectangle off the screen or a failed write-back.
 * @note Call only between a completed submission and the next one.
 */
int surface_os_display_amend(unsigned buffer, unsigned x, unsigned y,
    unsigned w, unsigned h);
/**
 * @brief Start converting the @p w x @p h rectangle at (@p x, @p y) of an
 *        XRGB8888 view with rows of @p stride bytes into the same
 *        rectangle of back buffer @p back, by the board's PPA.
 * @return SURFACE_SCANOUT_OK once running; SURFACE_SCANOUT_REFUSED leaves
 *         the buffer unchanged
 * @note The view and the back buffer stay unchanged until the poll ends it.
 */
int surface_os_display_convert(unsigned back, const void *pixels, unsigned stride,
    unsigned x, unsigned y, unsigned w, unsigned h);
/** @brief Start a 32-bit copy or solid fill (NULL source) on cache-line-aligned rows. A nonzero
 * result retains the pixels until paint_poll returns OK or ERROR. */
int surface_os_display_paint(uint32_t *pixels, unsigned stride, const uint32_t *source,
                             unsigned src_stride, unsigned width, unsigned height, uint32_t color);
int surface_os_display_paint_poll(void);
/** @brief The conversion's state: BUSY, OK, or ERROR once it failed. */
int surface_os_display_convert_poll(void);
/** @brief Nonzero when a conversion's end queues a turn of the owner process
 *  from its interrupt; zero leaves it to the timer turns. */
int surface_os_display_convert_wakes(void);
/** @brief The conversions started, those that failed or were refused, the
 *  owner turns queued by completion callbacks, and the last elapsed microseconds. */
void surface_os_display_convert_stats(uint32_t *jobs, uint32_t *failed, uint32_t *wakes,
    uint32_t *last_us);
/** @brief Poll a request; REFUSED preserves its ownership and requires recovery. */
int surface_os_display_poll(uint64_t token);
/** @brief Observe idle/conversion scanout events; zero requests recovery. */
int surface_os_display_health(void);
/** @brief Return nonzero after hardware releases both buffers, including failed open. */
int surface_os_display_close(void);
#endif
