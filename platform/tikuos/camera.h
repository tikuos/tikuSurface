/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * camera.h - the S31's camera and JPEG encoder for the owner's turns,
 * without SDK types.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_OS_CAMERA_H
#define SURFACE_OS_CAMERA_H
#include <stdint.h>
enum { SURFACE_CAMERA_OK = 0, SURFACE_CAMERA_BUSY = 1, SURFACE_CAMERA_ERROR = -1,
    SURFACE_CAMERA_ROOM = -2 };
#define SURFACE_CAMERA_W 1280u
#define SURFACE_CAMERA_H 720u
/** A frame: UYVY, 2 bytes a pixel, in a 64-byte aligned PSRAM buffer. */
#define SURFACE_CAMERA_FRAME_BYTES (SURFACE_CAMERA_W * SURFACE_CAMERA_H * 2u)
/** @brief Start the sensor; owner turn, the touch bus open.  @return OK or ERROR. */
int surface_os_camera_open(void);
/** @brief Stop the sensor and release its pins; owner turn. */
void surface_os_camera_close(void);
/** @brief Arm a capture of one whole frame into @p frame.  @return BUSY or ERROR. */
int surface_os_camera_capture(void *frame);
/** @brief Advance the capture.  @return BUSY, OK once @p frame holds it, or ERROR. */
int surface_os_camera_capture_poll(void);
/** @brief Start encoding @p frame at @p quality into @p out, @p cap bytes, 64-byte
 *  aligned.  @return BUSY or ERROR. */
int surface_os_camera_encode(void *frame, unsigned quality, uint8_t *out, uint32_t cap);
/** @brief Advance the encode.  @return BUSY; OK with *@p len the file's size; ROOM
 *  when it did not fit; or ERROR. */
int surface_os_camera_encode_poll(uint32_t *len);
#endif
