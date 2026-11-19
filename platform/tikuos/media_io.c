/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * media_io.c - READ-scoped requests on the mounted media service.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "media_io.h"
#include "runtime.h"
#include <services/storage/tiku_media_card.h>
#include <string.h>
/** @brief Queue storage remains live through service completion and reader closure. */
typedef struct {
    tiku_media_store_t *store;
    tiku_media_request_t request;
    tiku_mfs_object_t info;
    surface_media_io_t *view;
    _Alignas(8) unsigned char read_state[6144];
    unsigned ready;
} media_bridge_t;

size_t surface_os_media_size(void)
{ return tiku_media_card_read_size() <= 6144 ? sizeof(media_bridge_t) : SIZE_MAX; }
void surface_os_media_init(void *data)
{
    media_bridge_t *b = data;
    memset(b, 0, sizeof *b);
    b->ready = tiku_media_card_read_size() <= sizeof b->read_state;
    if (b->ready) tiku_media_card_read_init(b->read_state);
}
/** @brief Convert media results while keeping stale identity distinct from removal. */
static int status(int r)
{
    if (!r) return SURFACE_MEDIA_OK;
    if (r == TIKU_MFS_NOENT) return SURFACE_MEDIA_END;
    if (r == TIKU_MFS_BUSY) return SURFACE_MEDIA_BUSY;
    if (r == TIKU_MFS_STALE) return SURFACE_MEDIA_STALE;
    if (r == TIKU_MFS_ARG) return SURFACE_MEDIA_INVALID;
    return SURFACE_MEDIA_UNAVAILABLE;
}

int surface_os_media_submit(void *data, surface_media_io_t *v)
{
    media_bridge_t *b = data;
    if (!b || !v || !surface_os_in_owner() || v->operation > SURFACE_MEDIA_CLOSE || v->length > 1024)
        return SURFACE_MEDIA_INVALID;
    if (!b->ready) return SURFACE_MEDIA_UNAVAILABLE;
    if (b->view) return SURFACE_MEDIA_BUSY;
    if (v->operation == SURFACE_MEDIA_ENUMERATE || v->operation == SURFACE_MEDIA_OPEN)
        b->store = tiku_media_card_store();
    if (!b->store) return SURFACE_MEDIA_UNAVAILABLE;
    static const tiku_media_op_t operations[] = {TIKU_MEDIA_ENUMERATE, TIKU_MEDIA_OPEN_VERSION,
        TIKU_MEDIA_READ_AT, TIKU_MEDIA_CLOSE};
    b->request = (tiku_media_request_t){.op = operations[v->operation], .client = 0x53555246u,
        .permissions = TIKU_MEDIA_READ, .name = v->name, .names_epoch = v->epoch,
        .rank = v->rank, .exclusive = (uint8_t)v->exclusive, .info = &b->info,
        .id = v->reader, .offset = v->offset, .buffer = v->bytes, .length = (uint32_t)v->length};
    int r = tiku_media_store_submit(b->store, &b->request);
    if (!r) b->view = v;
    return status(r);
}

int surface_os_media_poll(void *data)
{
    media_bridge_t *b = data;
    if (!b || !b->ready || !surface_os_in_owner()) return 0;
    if (!b->view) {
        if (b->store && b->store->count) (void)tiku_media_card_read_step(b->read_state, b->store);
        return 0;
    }
    if (!b->request.done || !tiku_media_card_read_idle(b->read_state))
        (void)tiku_media_card_read_step(b->read_state, b->store);
    if (!b->request.done || !tiku_media_card_read_idle(b->read_state)) return 0;
    surface_media_io_t *v = b->view;
    int r = b->request.result;
    v->result = status(r);
    if (v->operation == SURFACE_MEDIA_READ && r >= 0) {
        v->count = (size_t)r; v->result = SURFACE_MEDIA_OK;
    }
    if (v->operation == SURFACE_MEDIA_ENUMERATE && (r == 0 || r == TIKU_MFS_NOENT)) {
        v->epoch = b->request.names_epoch; v->rank = b->request.rank;
        if (!r) { memcpy(v->found, b->info.name, 64); v->size = b->info.length; }
    }
    if (v->operation == SURFACE_MEDIA_OPEN && !r) {
        v->reader = b->request.id; v->size = b->info.length;
    }
    b->view = NULL;
    return 1;
}

int surface_os_media_cancel(void *data)
{
    media_bridge_t *b = data;
    if (!b || !b->ready || !surface_os_in_owner()) return 0;
    if (b->view && !b->request.done && b->view->operation != SURFACE_MEDIA_CLOSE) {
        if (tiku_media_card_read_cancel(b->read_state, b->store, &b->request)) return 0;
    }
    if (!tiku_media_card_read_idle(b->read_state))
        (void)tiku_media_card_read_step(b->read_state, b->store);
    return tiku_media_card_read_idle(b->read_state);
}
