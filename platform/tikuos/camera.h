/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * camera.h - the S31's camera, its stream of pictures and its JPEG encoder,
 * for the owner's turns, without SDK types.
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
/** A streamed picture: every other line of a frame, in one half of the
 *  frame's buffer. */
#define SURFACE_CAMERA_PICTURE_H (SURFACE_CAMERA_H / 2u)
/** @brief Start the sensor; owner turn, the touch bus open.  @return OK or ERROR. */
int surface_os_camera_open(void);
/** @brief Stop the sensor and release its pins; owner turn. */
void surface_os_camera_close(void);
/** @brief Stream pictures into the two halves of @p frame, the owner process
 *  polled after each; stop streaming when @p frame is NULL.  @return OK or
 *  ERROR. */
int surface_os_camera_stream(void *frame);
/** @brief The newest streamed picture's number, from 1, and in *@p half the
 *  half of the frame that holds it; 0 before the first or when not
 *  streaming. */
uint32_t surface_os_camera_newest(unsigned *half);
/** @brief Drop the cache's copy of half @p half of @p frame, so the
 *  processor reads the picture the camera wrote there. */
void surface_os_camera_drop(void *frame, unsigned half);
/** @brief Arm a capture of one whole frame into @p frame, ending a stream at
 *  the next frame.  @return BUSY or ERROR. */
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
