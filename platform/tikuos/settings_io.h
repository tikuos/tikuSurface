/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * settings_io.h - calibration records in the existing data store.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_SETTINGS_IO_H
#define SURFACE_SETTINGS_IO_H
#include <stddef.h>
/** @brief Read the product record; 0 succeeds, 1 is absent, -1 is unavailable. */
int surface_os_settings_read(unsigned char *, size_t capacity, size_t *length);
/** @brief Atomically replace the product record; zero acknowledges storage success. */
int surface_os_settings_write(const unsigned char *, size_t length);
/** @brief PSRAM bridge capacity, aligned to eight bytes and initialized to zero. */
#define SURFACE_SETTINGS_IO_BYTES 512u
/** @brief Copy and queue a calibration record; 1 accepted, -1 refused. */
int surface_os_settings_begin(void *bridge, const unsigned char *, size_t length);
/** @brief Advance one storage phase; 1 pending, 0 saved durably, -1 failed or cancelled. */
int surface_os_settings_poll(void *bridge);
/** @brief Request cancellation; the caller must poll to retirement before releasing the bridge. */
void surface_os_settings_cancel(void *bridge);
#endif
