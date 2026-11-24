/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * media_io.h - neutral queued media request and result records.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_MEDIA_IO_H
#define SURFACE_MEDIA_IO_H
#ifndef SURFACE_DIRECTORIES
#define SURFACE_DIRECTORIES 0
#endif
#include <stddef.h>
#include <stdint.h>
/** @brief Caller reservation for the queued read and mutation bridge; aligned to eight bytes. */
#define SURFACE_MEDIA_BRIDGE_BYTES 16384u
/** @brief Retained state slots for the firmware card owners. */
#define SURFACE_MEDIA_READ_BYTES 6144u
#define SURFACE_MEDIA_MUTATION_BYTES 8192u
/** @brief Operations advance through the firmware's serialized storage queue.
 *  SPACE is answered at once from the mounted volume: offset is its size in
 *  bytes and size the bytes a writer may still use. */
enum { SURFACE_MEDIA_ENUMERATE, SURFACE_MEDIA_OPEN, SURFACE_MEDIA_READ, SURFACE_MEDIA_CLOSE,
    SURFACE_MEDIA_RENAME, SURFACE_MEDIA_DELETE, SURFACE_MEDIA_REPLACE, SURFACE_MEDIA_MKDIR, SURFACE_MEDIA_TREE_RENAME, SURFACE_MEDIA_TREE_DELETE, SURFACE_MEDIA_DIRECTORY_UPGRADE, SURFACE_MEDIA_TREE_COPY,
    SURFACE_MEDIA_SPACE };
/** @brief Result codes copied without exposing filesystem types to SDK
 *  consumers; FULL is a volume without room for the change. */
enum { SURFACE_MEDIA_OK, SURFACE_MEDIA_END, SURFACE_MEDIA_BUSY, SURFACE_MEDIA_STALE,
    SURFACE_MEDIA_UNAVAILABLE, SURFACE_MEDIA_INVALID, SURFACE_MEDIA_UNSUPPORTED, SURFACE_MEDIA_LIMIT,
    SURFACE_MEDIA_FULL };
/** @brief One caller-owned operation retained until poll reports completion. */
typedef struct {
    unsigned operation, exclusive;
#if SURFACE_DIRECTORIES
    unsigned directory;
    uint64_t prepared,total;
#endif
    char name[64], found[64], destination[64];
    uint64_t epoch, rank, reader, offset, size, id;
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
/** @brief Advance one bounded shared storage phase; nonzero retires the submitted view. */
int surface_os_media_poll(void *);
/** @brief Cancel this bridge's queued operation and drain an older owner; nonzero permits retirement. */
int surface_os_media_cancel(void *);
/** @brief On an owner turn with no request: close a card session left
 *  parked longer than the build's linger; 0, or TIKU_MFS_BUSY or
 *  TIKU_MFS_IO from closing it. */
int surface_os_media_idle(void *);
/** @brief Changes to the card's names this bridge did not make, counted at
 *  its submissions, completions and idle turns since it was initialized;
 *  read from any thread. */
unsigned surface_os_media_foreign(const void *);
#endif
