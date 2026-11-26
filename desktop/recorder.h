/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * recorder.h - Sound Recorder: the board's microphones through /dev/mic,
 * recordings in ~/Recordings on the card.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_RECORDER_H_
#define SURFACE_RECORDER_H_
#include "tiku_window.h"
#include <stdint.h>

/** @brief Open Sound Recorder in @p workspace within @p area, or bring it
 *  forward; @p damage redraws part of its window. */
tiku_window_t *surface_recorder_open(tiku_workspace_t *workspace, tiku_rect_t area, int *dirty,
                                     void (*damage)(tiku_window_t *window, tiku_rect_t rect));

/** @brief Each desktop turn: move what the microphone captured into the
 *  take and redraw the scope, the meter and the time. */
void surface_recorder_tick(int64_t now);

/** @brief The WAV header for @p samples mono 16 kHz PCM16 samples. */
void surface_recorder_wav_header(uint8_t out[44], uint32_t samples);

#endif
