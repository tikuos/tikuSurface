/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * display.c - public display submission and board lifecycle binding.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "display.h"
#include "../../system/product/surface_panel.h"
#include "runtime.h"
#include <interfaces/display/tiku_display.h>
#if defined(TIKU_BOARD_ESP32_P4X_FUNCTION_EV_BOARD)
#define SURFACE_P4X_PPA 1
#include <arch/esp32p4/boards/tiku_p4x_panel.h>
#include <arch/esp32p4/tiku_dsi_arch.h>
#include <arch/esp32p4/tiku_ppa_arch.h>
#include <hal/tiku_cpu.h>
#else
#define SURFACE_P4X_PPA 0
#include <arch/esp32s31/tiku_display_arch.h>
#include <arch/esp32s31/tiku_psram_arch.h>
#include <arch/esp32s31/tiku_ppa_arch.h>
#include <arch/esp32s31/tiku_runtime_arch.h>
#endif

/* SURFACE_PPA 0 leaves every conversion to the CPU. */
#ifndef SURFACE_PPA
#define SURFACE_PPA 1
#endif

static tiku_display_t display;
static uint16_t *buffers[2];
static int opened;
static uint32_t ppa_jobs, ppa_failed;
static volatile uint32_t ppa_wakes, ppa_last_us;
static int ppa_notified;
#if SURFACE_P4X_PPA
static volatile int conversion = SURFACE_SCANOUT_REFUSED;
static uint32_t conversion_start, paint_start;
static volatile int paint_state = SURFACE_SCANOUT_REFUSED;

/** @brief Hand the completed raster operation back to the desktop owner. */
static void paint_ended(void *context, int result)
{
    (void)context;
    __atomic_store_n(&paint_state, result == TIKU_P4_PPA_OK ?
                     SURFACE_SCANOUT_OK : SURFACE_SCANOUT_ERROR, __ATOMIC_RELEASE);
    surface_os_poll_isr();
}
#define P4X_CONVERSION_TIMEOUT_US 250000u

/** @brief Publish the retirement of a P4 conversion and wake the display owner. */
static void conversion_ended(void *context, int result)
{
    (void)context;
    ppa_last_us = (uint32_t)surface_os_time() - conversion_start;
    ppa_wakes++;
    __atomic_store_n(&conversion, result == TIKU_P4_PPA_OK ?
                     SURFACE_SCANOUT_OK : SURFACE_SCANOUT_ERROR, __ATOMIC_RELEASE);
    surface_os_poll_isr();
}

static int conversion_state(void)
{
    if (__atomic_load_n(&conversion, __ATOMIC_ACQUIRE) == SURFACE_SCANOUT_BUSY &&
        (uint32_t)surface_os_time() - conversion_start >= P4X_CONVERSION_TIMEOUT_US &&
        tiku_p4_ppa_srm_expire(conversion_ended, NULL))
        ppa_notified = 0;
    return __atomic_load_n(&conversion, __ATOMIC_ACQUIRE);
}
#else
static uint64_t ppa_started;
static volatile int painting;

/** @brief A PPA job's end, from its interrupt: timed on the 16 MHz system
 *  timer, then the owner process runs. */
static void ppa_ended(void)
{
    if (!painting) {
        ppa_last_us = (uint32_t)((tiku_s31_time() - ppa_started) / 16u);
        ppa_wakes++;
    }
    surface_os_poll_isr();
}
#endif

int surface_os_display_open(uint16_t *front, uint16_t *back)
{
    if (!surface_os_in_owner() || opened || !front || !back || front == back) return 0;
    if (tiku_display_init(&display, front, SURFACE_DESKTOP_W, SURFACE_DESKTOP_H) != TIKU_DISPLAY_OK) return 0;
    opened = 1;
    buffers[0] = front; buffers[1] = back;
#if !SURFACE_P4X_PPA
    ppa_notified = SURFACE_PPA && tiku_s31_ppa_notify(ppa_ended) == 0;
#else
    ppa_notified = SURFACE_PPA;
#endif
    uint32_t caps = TIKU_DISPLAY_CAP_FLIP | TIKU_DISPLAY_CAP_ASYNC;
    return (tiku_display_caps() & caps) == caps &&
        tiku_display_set_buffers(&display, front, back) == TIKU_DISPLAY_OK;
}

int surface_os_display_submit(unsigned back, unsigned x, unsigned y,
    unsigned w, unsigned h, uint64_t token)
{
    if (!surface_os_in_owner() || !opened || back > 1 || display.fb != buffers[back] ||
        x >= SURFACE_DESKTOP_W || y >= SURFACE_DESKTOP_H || !w || !h || w > SURFACE_DESKTOP_W - x || h > SURFACE_DESKTOP_H - y)
        return SURFACE_SCANOUT_ERROR;
    int result = tiku_display_submit(&display, token, (uint16_t)x, (uint16_t)y,
        (uint16_t)w, (uint16_t)h);
    return result == TIKU_DISPLAY_PENDING ? SURFACE_SCANOUT_OK : SURFACE_SCANOUT_ERROR;
}

#if SURFACE_NATIVE_DESKTOP
int surface_os_display_paint(uint32_t *pixels, unsigned stride, const uint32_t *source,
                             unsigned src_stride, unsigned width, unsigned height, uint32_t color)
{
#if SURFACE_P4X_PPA
    if (!ppa_notified || !surface_os_in_owner() || !opened ||
        __atomic_load_n(&paint_state, __ATOMIC_ACQUIRE) != SURFACE_SCANOUT_REFUSED ||
        __atomic_load_n(&conversion, __ATOMIC_ACQUIRE) != SURFACE_SCANOUT_REFUSED ||
        !pixels || !width || !height || width > stride || stride > 16383u ||
        height > 8191u || width > 8191u || ((uintptr_t)pixels & 63u) ||
        ((width | stride) & 15u) ||
        (source && (width > src_stride || src_stride > 16383u))) return 0;
    tiku_p4_picture_t out = {pixels, stride, height, 4};
    int rc;
    paint_start = (uint32_t)surface_os_time();
    __atomic_store_n(&paint_state, SURFACE_SCANOUT_BUSY, __ATOMIC_RELEASE);
    if (source) {
        tiku_p4_picture_t in = {(void *)source, src_stride, height, 4};
        rc = tiku_p4_ppa_srm_async(&out, 0, 0, &in, 0, 0, width, height,
                                   TIKU_P4_PPA_SCALE_1, TIKU_P4_PPA_SCALE_1,
                                   TIKU_P4_PPA_ROT_0, 0, paint_ended, NULL);
    } else {
        rc = tiku_p4_ppa_fill_async(&out, 0, 0, width, height, color, paint_ended, NULL);
    }
    if (rc == TIKU_P4_PPA_OK) return 1;
    __atomic_store_n(&paint_state, SURFACE_SCANOUT_REFUSED, __ATOMIC_RELEASE);
    return 0;
#else
    if (!SURFACE_PPA || !surface_os_in_owner() || !opened || painting) return 0;
    painting = 1;
    int rc = source ? tiku_s31_ppa_blit(pixels, stride, source, src_stride, width, height) :
                      tiku_s31_ppa_fill(pixels, stride, width, height, color);
    if (rc == TIKU_S31_PPA_OK)
        return 1;
    painting = 0;
    return 0;
#endif
}

int surface_os_display_paint_poll(void)
{
#if !SURFACE_P4X_PPA
    if (!painting) return SURFACE_SCANOUT_ERROR;
    int rc = tiku_s31_ppa_poll();
    if (rc == TIKU_S31_PPA_RUNNING) return SURFACE_SCANOUT_BUSY;
    painting = 0;
    return rc == TIKU_S31_PPA_OK ? SURFACE_SCANOUT_OK : SURFACE_SCANOUT_ERROR;
#else
    int state = __atomic_load_n(&paint_state, __ATOMIC_ACQUIRE);
    if (state == SURFACE_SCANOUT_BUSY) {
        if ((uint32_t)surface_os_time() - paint_start < P4X_CONVERSION_TIMEOUT_US) return state;
        (void)tiku_p4_ppa_expire(paint_ended, NULL);
        state = __atomic_load_n(&paint_state, __ATOMIC_ACQUIRE);
    }
    if (state == SURFACE_SCANOUT_BUSY) return state;
    __atomic_store_n(&paint_state, SURFACE_SCANOUT_REFUSED, __ATOMIC_RELEASE);
    return state == SURFACE_SCANOUT_OK ? state : SURFACE_SCANOUT_ERROR;
#endif
}
#endif

int surface_os_display_convert(unsigned back, const void *pixels, unsigned stride,
    unsigned x, unsigned y, unsigned w, unsigned h)
{
    if (!SURFACE_PPA || !surface_os_in_owner() || !opened || back > 1 ||
        display.fb != buffers[back] || !pixels || stride < 4u * SURFACE_DESKTOP_W || (stride & 3u) ||
        stride > 4u * 16383u || x >= SURFACE_DESKTOP_W || y >= SURFACE_DESKTOP_H || !w || !h || w > SURFACE_DESKTOP_W - x || h > SURFACE_DESKTOP_H - y)
        return SURFACE_SCANOUT_REFUSED;
#if SURFACE_P4X_PPA
    if (!ppa_notified || __atomic_load_n(&conversion, __ATOMIC_ACQUIRE) != SURFACE_SCANOUT_REFUSED)
        return SURFACE_SCANOUT_REFUSED;
    tiku_p4_picture_t out = {buffers[back], SURFACE_DESKTOP_W, SURFACE_DESKTOP_H, 2};
    tiku_p4_picture_t in = {(void *)pixels, (uint16_t)(stride / 4u), SURFACE_DESKTOP_H, 4};
    conversion_start = (uint32_t)surface_os_time();
    __atomic_store_n(&conversion, SURFACE_SCANOUT_BUSY, __ATOMIC_RELEASE);
    if (tiku_p4_ppa_srm_async(&out, x, y, &in, x, y, w, h, TIKU_P4_PPA_SCALE_1,
                              TIKU_P4_PPA_SCALE_1, TIKU_P4_PPA_ROT_0, 0,
                              conversion_ended, NULL) != TIKU_P4_PPA_OK) {
        __atomic_store_n(&conversion, SURFACE_SCANOUT_REFUSED, __ATOMIC_RELEASE);
        ppa_failed++;
        return SURFACE_SCANOUT_REFUSED;
    }
#else
    tiku_s31_picture_t out = {buffers[back], SURFACE_DESKTOP_W, SURFACE_DESKTOP_H, 2};
    tiku_s31_picture_t in = {(void *)pixels, (uint16_t)(stride / 4u), SURFACE_DESKTOP_H, 4};
    if (tiku_s31_ppa_convert(&out, (uint16_t)x, (uint16_t)y, &in, (uint16_t)x, (uint16_t)y,
                             (uint16_t)w, (uint16_t)h) != TIKU_S31_PPA_OK) {
        ppa_failed++;
        return SURFACE_SCANOUT_REFUSED;
    }
    ppa_started = tiku_s31_time();
#endif
    ppa_jobs++;
    return SURFACE_SCANOUT_OK;
}

int surface_os_display_convert_poll(void)
{
#if !SURFACE_P4X_PPA
    int rc = tiku_s31_ppa_poll();

    if (rc == TIKU_S31_PPA_RUNNING) return SURFACE_SCANOUT_BUSY;
    if (rc == TIKU_S31_PPA_OK) return SURFACE_SCANOUT_OK;
    ppa_failed++;
    return SURFACE_SCANOUT_ERROR;
#else
    int state = conversion_state();
    if (state == SURFACE_SCANOUT_BUSY) return state;
    __atomic_store_n(&conversion, SURFACE_SCANOUT_REFUSED, __ATOMIC_RELEASE);
    if (state == SURFACE_SCANOUT_ERROR) ppa_failed++;
    return state == SURFACE_SCANOUT_OK ? state : SURFACE_SCANOUT_ERROR;
#endif
}

int surface_os_display_convert_wakes(void)
{ return ppa_notified; }

void surface_os_display_convert_stats(uint32_t *jobs, uint32_t *failed, uint32_t *wakes,
    uint32_t *last_us)
{
    *jobs = ppa_jobs;
    *failed = ppa_failed;
    *wakes = ppa_wakes;
    *last_us = ppa_last_us;
}

int surface_os_display_amend(unsigned buffer, unsigned x, unsigned y,
    unsigned w, unsigned h)
{
    if (!surface_os_in_owner() || !opened || buffer > 1 || display.front != buffers[buffer] ||
        x >= SURFACE_DESKTOP_W || y >= SURFACE_DESKTOP_H || !w || !h || w > SURFACE_DESKTOP_W - x || h > SURFACE_DESKTOP_H - y)
        return 0;
    /* Each row's cache lines go to PSRAM; scanout only reads this buffer,
     * so a row shows the old pixels or the new until its next scan. */
    for (unsigned row = y; row < y + h; ++row) {
        uintptr_t begin = (uintptr_t)(buffers[buffer] + row * SURFACE_DESKTOP_W + x);
#if defined(TIKU_BOARD_ESP32_P4X_FUNCTION_EV_BOARD)
        tiku_cpu_dcache_clean((void *)begin, w * sizeof(uint16_t));
#else
        uintptr_t end = begin + w * sizeof(uint16_t);
        begin &= ~(uintptr_t)(TIKU_S31_PSRAM_LINE - 1u);
        end = (end + TIKU_S31_PSRAM_LINE - 1u) & ~(uintptr_t)(TIKU_S31_PSRAM_LINE - 1u);
        if (tiku_s31_psram_sync((void *)begin, (uint32_t)(end - begin), 0)) return 0;
#endif
    }
    return 1;
}

int surface_os_display_poll(uint64_t token)
{
    if (!surface_os_in_owner() || !opened) return SURFACE_SCANOUT_REFUSED;
    int result = tiku_display_poll(&display, token);
    if (result == TIKU_DISPLAY_OK) return SURFACE_SCANOUT_OK;
    if (result == TIKU_DISPLAY_PENDING) return SURFACE_SCANOUT_BUSY;
    if (result == TIKU_DISPLAY_ERR_RETIRED) return SURFACE_SCANOUT_ERROR;
    return SURFACE_SCANOUT_REFUSED;
}

int surface_os_display_health(void)
{
    if (!surface_os_in_owner() || !opened) return 0;
#if defined(TIKU_BOARD_ESP32_P4X_FUNCTION_EV_BOARD)
    return !tiku_p4_dsi_underrun() && tiku_p4_dsi_showing(display.front) >= 0;
#else
    return !tiku_s31_display_poll(NULL);
#endif
}

int surface_os_display_close(void)
{
    if (!surface_os_in_owner()) return 0;
    if (!opened) return 1;
#if defined(TIKU_BOARD_ESP32_P4X_FUNCTION_EV_BOARD)
    if (conversion_state() == SURFACE_SCANOUT_BUSY) return 0;
    __atomic_store_n(&conversion, SURFACE_SCANOUT_REFUSED, __ATOMIC_RELEASE);
    tiku_p4_display_close();
#else
    if (tiku_s31_display_close(&display) != TIKU_DISPLAY_OK) return 0;
    if (SURFACE_PPA && ppa_notified) (void)tiku_s31_ppa_notify(NULL);
#endif
    ppa_notified = 0;
    opened = 0;
    buffers[0] = buffers[1] = NULL;
    display = (tiku_display_t){0};
    return 1;
}
