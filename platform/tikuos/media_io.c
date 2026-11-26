/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * media_io.c - the desktop media client's kernel context and permissions.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "media_io.h"
#include "runtime.h"
#include <services/storage/tiku_media_store.h>

size_t surface_os_media_size(void) { return tiku_posix_media_queue_size(); }

void surface_os_media_init(void *storage)
{
    unsigned permissions = TIKU_MEDIA_READ | TIKU_MEDIA_WRITE;
#if SURFACE_DIRECTORIES
    permissions |= TIKU_MEDIA_ADMIN;
#endif
    tiku_posix_media_queue_init(storage, 0x53555246u, permissions);
}

int surface_os_media_submit(void *storage, surface_media_io_t *io)
{
    return surface_os_in_owner() ? tiku_posix_media_queue_submit(storage, io) : SURFACE_MEDIA_INVALID;
}

int surface_os_media_poll(void *storage)
{
    return surface_os_in_owner() && tiku_posix_media_queue_poll(storage);
}

int surface_os_media_idle(void *storage)
{
    return surface_os_in_owner() ? tiku_posix_media_queue_idle(storage) : 0;
}

int surface_os_media_cancel(void *storage)
{
    return surface_os_in_owner() && tiku_posix_media_queue_cancel(storage);
}

unsigned surface_os_media_foreign(const void *storage)
{
    return tiku_posix_media_queue_foreign(storage);
}
