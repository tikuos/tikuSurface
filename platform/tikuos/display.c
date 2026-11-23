/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * display.c - public display submission and S31 lifecycle binding.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "display.h"
#include "runtime.h"
#include <interfaces/display/tiku_display.h>
#include <arch/esp32s31/tiku_display_arch.h>
#include <arch/esp32s31/tiku_psram_arch.h>
#include <arch/esp32s31/tiku_ppa_arch.h>

/* SURFACE_PPA 0 leaves every conversion to the CPU. */
#ifndef SURFACE_PPA
#define SURFACE_PPA 1
#endif

static tiku_display_t display;
static uint16_t *buffers[2];
static int opened;
static uint32_t ppa_jobs, ppa_failed;

int surface_os_display_open(uint16_t *front, uint16_t *back)
{
    if (!surface_os_in_owner() || opened || !front || !back || front == back) return 0;
    if (tiku_display_init(&display, front, 800, 480) != TIKU_DISPLAY_OK) return 0;
    opened = 1;
    buffers[0] = front; buffers[1] = back;
    uint32_t caps = TIKU_DISPLAY_CAP_FLIP | TIKU_DISPLAY_CAP_ASYNC;
    return (tiku_display_caps() & caps) == caps &&
        tiku_display_set_buffers(&display, front, back) == TIKU_DISPLAY_OK;
}

int surface_os_display_submit(unsigned back, unsigned x, unsigned y,
    unsigned w, unsigned h, uint64_t token)
{
    if (!surface_os_in_owner() || !opened || back > 1 || display.fb != buffers[back] ||
        x >= 800 || y >= 480 || !w || !h || w > 800 - x || h > 480 - y)
        return SURFACE_SCANOUT_ERROR;
    int result = tiku_display_submit(&display, token, (uint16_t)x, (uint16_t)y,
        (uint16_t)w, (uint16_t)h);
    return result == TIKU_DISPLAY_PENDING ? SURFACE_SCANOUT_OK : SURFACE_SCANOUT_ERROR;
}

int surface_os_display_convert(unsigned back, const void *pixels, unsigned stride,
    unsigned x, unsigned y, unsigned w, unsigned h)
{
    if (!SURFACE_PPA || !surface_os_in_owner() || !opened || back > 1 ||
        display.fb != buffers[back] || !pixels || stride < 3200 || (stride & 3u) ||
        stride > 4u * 16383u || x >= 800 || y >= 480 || !w || !h || w > 800 - x || h > 480 - y)
        return SURFACE_SCANOUT_REFUSED;
    tiku_s31_picture_t out = {buffers[back], 800, 480, 2};
    tiku_s31_picture_t in = {(void *)pixels, (uint16_t)(stride / 4u), 480, 4};
    if (tiku_s31_ppa_convert(&out, (uint16_t)x, (uint16_t)y, &in, (uint16_t)x, (uint16_t)y,
                             (uint16_t)w, (uint16_t)h) != TIKU_S31_PPA_OK) {
        ppa_failed++;
        return SURFACE_SCANOUT_REFUSED;
    }
    ppa_jobs++;
    return SURFACE_SCANOUT_OK;
}

int surface_os_display_convert_poll(void)
{
    int rc = tiku_s31_ppa_poll();

    if (rc == TIKU_S31_PPA_RUNNING) return SURFACE_SCANOUT_BUSY;
    if (rc == TIKU_S31_PPA_OK) return SURFACE_SCANOUT_OK;
    ppa_failed++;
    return SURFACE_SCANOUT_ERROR;
}

void surface_os_display_convert_stats(uint32_t *jobs, uint32_t *failed)
{
    *jobs = ppa_jobs;
    *failed = ppa_failed;
}

int surface_os_display_amend(unsigned buffer, unsigned x, unsigned y,
    unsigned w, unsigned h)
{
    if (!surface_os_in_owner() || !opened || buffer > 1 || display.front != buffers[buffer] ||
        x >= 800 || y >= 480 || !w || !h || w > 800 - x || h > 480 - y)
        return 0;
    /* Each row's cache lines go to PSRAM; scanout only reads this buffer,
     * so a row shows the old pixels or the new until its next scan. */
    for (unsigned row = y; row < y + h; ++row) {
        uintptr_t begin = (uintptr_t)(buffers[buffer] + row * 800u + x);
        uintptr_t end = begin + w * sizeof(uint16_t);
        begin &= ~(uintptr_t)(TIKU_S31_PSRAM_LINE - 1u);
        end = (end + TIKU_S31_PSRAM_LINE - 1u) & ~(uintptr_t)(TIKU_S31_PSRAM_LINE - 1u);
        if (tiku_s31_psram_sync((void *)begin, (uint32_t)(end - begin), 0)) return 0;
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
{ return surface_os_in_owner() && opened && !tiku_s31_display_poll(NULL); }

int surface_os_display_close(void)
{
    if (!surface_os_in_owner()) return 0;
    if (!opened) return 1;
    if (tiku_s31_display_close(&display) != TIKU_DISPLAY_OK) return 0;
    opened = 0;
    buffers[0] = buffers[1] = NULL;
    display = (tiku_display_t){0};
    return 1;
}
