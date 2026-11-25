/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * camera.c - the SC101IOT on the S31's DVP port, its stream of pictures,
 * and its JPEG encoder, for the desktop runtime's owner turns.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "camera.h"
#include "runtime.h"
#include <arch/esp32s31/tiku_camera_arch.h>
#include <arch/esp32s31/tiku_jpeg_arch.h>
#include <arch/esp32s31/tiku_psram_arch.h>

_Static_assert(SURFACE_CAMERA_FRAME_BYTES == TIKU_S31_CAM_FRAME_BYTES, "camera frame size");
_Static_assert(SURFACE_CAMERA_PICTURE_H == TIKU_S31_CAM_PREVIEW_H, "camera picture height");

/** @brief A streamed picture's end, from the camera's interrupt: the owner
 *  process runs. */
static void picture_ended(void)
{
    surface_os_poll_isr();
}

int surface_os_camera_stream(void *frame)
{
    return tiku_s31_camera_stream(frame, frame ? SURFACE_CAMERA_FRAME_BYTES : 0u,
                                  frame ? picture_ended : 0) == TIKU_S31_CAM_OK ?
        SURFACE_CAMERA_OK : SURFACE_CAMERA_ERROR;
}

uint32_t surface_os_camera_newest(unsigned *half)
{
    uint32_t sequence = 0;
    int h = tiku_s31_camera_latest(&sequence);

    if (h < 0) return 0;
    if (half) *half = (unsigned)h;
    return sequence;
}

void surface_os_camera_drop(void *frame, unsigned half)
{
    if (frame && half < 2u)
        (void)tiku_s31_psram_invalidate((uint8_t *)frame + half * TIKU_S31_CAM_PREVIEW_BYTES,
                                        TIKU_S31_CAM_PREVIEW_BYTES);
}

int surface_os_camera_open(void)
{
    return tiku_s31_camera_open(TIKU_S31_CAM_XCLK_DIV_MIN) == TIKU_S31_CAM_OK ?
        SURFACE_CAMERA_OK : SURFACE_CAMERA_ERROR;
}

void surface_os_camera_close(void)
{
    (void)tiku_s31_camera_close();
}

int surface_os_camera_capture(void *frame)
{
    return tiku_s31_camera_capture(frame, SURFACE_CAMERA_FRAME_BYTES) == TIKU_S31_CAM_BUSY ?
        SURFACE_CAMERA_BUSY : SURFACE_CAMERA_ERROR;
}

int surface_os_camera_capture_poll(void)
{
    int rc = tiku_s31_camera_poll(0);

    return rc == TIKU_S31_CAM_BUSY ? SURFACE_CAMERA_BUSY :
           rc == TIKU_S31_CAM_OK ? SURFACE_CAMERA_OK : SURFACE_CAMERA_ERROR;
}

int surface_os_camera_encode(void *frame, unsigned quality, uint8_t *out, uint32_t cap)
{
    tiku_s31_picture_t picture = {frame, SURFACE_CAMERA_W, SURFACE_CAMERA_H, 2};

    return tiku_s31_jpeg_encode(&picture, quality, out, cap) == TIKU_S31_JPEG_RUNNING ?
        SURFACE_CAMERA_BUSY : SURFACE_CAMERA_ERROR;
}

int surface_os_camera_encode_poll(uint32_t *len)
{
    int rc = tiku_s31_jpeg_poll(len);

    return rc == TIKU_S31_JPEG_RUNNING ? SURFACE_CAMERA_BUSY :
           rc == TIKU_S31_JPEG_OK ? SURFACE_CAMERA_OK :
           rc == TIKU_S31_JPEG_ERR_ROOM ? SURFACE_CAMERA_ROOM : SURFACE_CAMERA_ERROR;
}
