/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * display.h - firmware scanout operations without SDK types.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_DISPLAY_H
#define SURFACE_DISPLAY_H
#include <stdint.h>
#define SURFACE_SCANOUT_BYTES (800u * 480u * sizeof(uint16_t))
/** @brief OK accepts/completes; ERROR refuses submit or proves failed DMA retirement. */
enum { SURFACE_SCANOUT_OK = 0, SURFACE_SCANOUT_BUSY = 1,
    SURFACE_SCANOUT_ERROR = -1, SURFACE_SCANOUT_REFUSED = -2 };
/** @brief Open a paired 800x480 display; caller retains buffers until close succeeds. */
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
/** @brief Poll a request; REFUSED preserves its ownership and requires recovery. */
int surface_os_display_poll(uint64_t token);
/** @brief Observe idle/conversion scanout events; zero requests recovery. */
int surface_os_display_health(void);
/** @brief Return nonzero after hardware releases both buffers, including failed open. */
int surface_os_display_close(void);
#endif
