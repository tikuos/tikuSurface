/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * camera.c - the SC101IOT on the S31's DVP port and its JPEG encoder,
 * for the desktop runtime's owner turns.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "camera.h"
#include <arch/esp32s31/tiku_camera_arch.h>
#include <arch/esp32s31/tiku_jpeg_arch.h>

_Static_assert(SURFACE_CAMERA_FRAME_BYTES == TIKU_S31_CAM_FRAME_BYTES, "camera frame size");

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
