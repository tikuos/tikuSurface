/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * media_io.h - neutral queued media request and result records.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_MEDIA_IO_H
#define SURFACE_MEDIA_IO_H
#include <compat/posix/tiku_posix_media_io.h>

typedef tiku_posix_media_io_t surface_media_io_t;
#define SURFACE_MEDIA_BRIDGE_BYTES TIKU_POSIX_MEDIA_BRIDGE_BYTES
#define SURFACE_MEDIA_READ_BYTES TIKU_POSIX_MEDIA_READ_BYTES
#define SURFACE_MEDIA_MUTATION_BYTES TIKU_POSIX_MEDIA_MUTATION_BYTES
#define SURFACE_MEDIA_ENUMERATE TIKU_POSIX_MEDIA_ENUMERATE
#define SURFACE_MEDIA_OPEN TIKU_POSIX_MEDIA_OPEN
#define SURFACE_MEDIA_READ TIKU_POSIX_MEDIA_READ
#define SURFACE_MEDIA_CLOSE TIKU_POSIX_MEDIA_CLOSE
#define SURFACE_MEDIA_RENAME TIKU_POSIX_MEDIA_RENAME
#define SURFACE_MEDIA_DELETE TIKU_POSIX_MEDIA_DELETE
#define SURFACE_MEDIA_REPLACE TIKU_POSIX_MEDIA_REPLACE
#define SURFACE_MEDIA_MKDIR TIKU_POSIX_MEDIA_MKDIR
#define SURFACE_MEDIA_TREE_RENAME TIKU_POSIX_MEDIA_TREE_RENAME
#define SURFACE_MEDIA_TREE_DELETE TIKU_POSIX_MEDIA_TREE_DELETE
#define SURFACE_MEDIA_DIRECTORY_UPGRADE TIKU_POSIX_MEDIA_DIRECTORY_UPGRADE
#define SURFACE_MEDIA_TREE_COPY TIKU_POSIX_MEDIA_TREE_COPY
#define SURFACE_MEDIA_SPACE TIKU_POSIX_MEDIA_SPACE
#define SURFACE_MEDIA_BEGIN TIKU_POSIX_MEDIA_BEGIN
#define SURFACE_MEDIA_RESUME TIKU_POSIX_MEDIA_RESUME
#define SURFACE_MEDIA_APPEND TIKU_POSIX_MEDIA_APPEND
#define SURFACE_MEDIA_SYNC TIKU_POSIX_MEDIA_SYNC
#define SURFACE_MEDIA_FINALIZE TIKU_POSIX_MEDIA_FINALIZE
#define SURFACE_MEDIA_RELEASE TIKU_POSIX_MEDIA_RELEASE
#define SURFACE_MEDIA_DISCARD TIKU_POSIX_MEDIA_DISCARD
#define SURFACE_MEDIA_RENAME_OVER TIKU_POSIX_MEDIA_RENAME_OVER
#define SURFACE_MEDIA_OK TIKU_POSIX_MEDIA_OK
#define SURFACE_MEDIA_END TIKU_POSIX_MEDIA_END
#define SURFACE_MEDIA_BUSY TIKU_POSIX_MEDIA_BUSY
#define SURFACE_MEDIA_STALE TIKU_POSIX_MEDIA_STALE
#define SURFACE_MEDIA_UNAVAILABLE TIKU_POSIX_MEDIA_UNAVAILABLE
#define SURFACE_MEDIA_INVALID TIKU_POSIX_MEDIA_INVALID
#define SURFACE_MEDIA_UNSUPPORTED TIKU_POSIX_MEDIA_UNSUPPORTED
#define SURFACE_MEDIA_LIMIT TIKU_POSIX_MEDIA_LIMIT
#define SURFACE_MEDIA_FULL TIKU_POSIX_MEDIA_FULL

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
