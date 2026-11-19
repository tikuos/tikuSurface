/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * media_io.h - neutral queued media request and result records.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_MEDIA_IO_H
#define SURFACE_MEDIA_IO_H
#include <stddef.h>
#include <stdint.h>
/** @brief Operations advance through the firmware's serialized storage queue. */
enum { SURFACE_MEDIA_ENUMERATE, SURFACE_MEDIA_OPEN, SURFACE_MEDIA_READ, SURFACE_MEDIA_CLOSE };
/** @brief Result codes copied without exposing filesystem types to SDK consumers. */
enum { SURFACE_MEDIA_OK, SURFACE_MEDIA_END, SURFACE_MEDIA_BUSY, SURFACE_MEDIA_STALE,
    SURFACE_MEDIA_UNAVAILABLE, SURFACE_MEDIA_INVALID };
/** @brief One caller-owned operation retained until poll reports completion. */
typedef struct {
    unsigned operation, exclusive;
    char name[64], found[64];
    uint64_t epoch, rank, reader, offset, size;
    unsigned char *bytes;
    size_t length, count;
    int result;
} surface_media_io_t;
/** @brief Size of the firmware queue bridge, stored in the product's PSRAM pool. */
size_t surface_os_media_size(void);
/** @brief Initialize caller storage without mounting or writing media. */
void surface_os_media_init(void *);
/** @brief Enqueue one operation; BUSY retains nothing and permits retry. */
int surface_os_media_submit(void *, surface_media_io_t *);
/** @brief Advance one bounded shared read phase; nonzero retires the submitted view. */
int surface_os_media_poll(void *);
/** @brief Cancel this bridge's queued read and drain an older query; nonzero permits retirement. */
int surface_os_media_cancel(void *);
#endif
