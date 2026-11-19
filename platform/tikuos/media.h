/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * media.h - bounded SDK storage sessions on a queued media bridge.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_MEDIA_H
#define SURFACE_MEDIA_H
#include "tiku_paged_storage.h"
#include "media_io.h"
/** @brief One retained page/read request and bounded name continuation. */
typedef struct {
    void *bridge;
    tiku_paged_storage_t *owner;
    const tiku_storage_request_t *request;
    surface_media_io_t io;
    char prefix[64], resume[64], resume_path[65];
    uint64_t resume_epoch, resume_after;
    size_t count;
    struct { char name[64], path[65]; uint64_t epoch, after; unsigned exclusive; } positions[16];
    unsigned position_next;
    unsigned pending, cancelled, phase, exclusive;
    int final_status, eof;
} surface_media_t;
/** @brief Bind initialized firmware storage and return the read-only SDK provider. */
tiku_storage_provider_t surface_media_init(surface_media_t *, void *bridge);
/** @brief Advance at most one queue operation; completion releases the SDK request view. */
void surface_media_poll(surface_media_t *);
#endif
