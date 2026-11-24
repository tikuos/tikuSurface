/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * media_io.c - queued reads and checked file mutations on mounted media.
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
    _Alignas(8) unsigned char read_state[SURFACE_MEDIA_READ_BYTES];
    _Alignas(8) unsigned char mutation_state[SURFACE_MEDIA_MUTATION_BYTES];
    unsigned ready;
    unsigned answered;          /* the view was answered at its submission */
    uint64_t known;             /* the card's names epoch, accounted for */
    volatile unsigned foreign;  /* name changes this bridge did not make */
} media_bridge_t;
_Static_assert(sizeof(media_bridge_t)<=SURFACE_MEDIA_BRIDGE_BYTES,"Media bridge exceeds runtime reservation");

size_t surface_os_media_size(void)
{ return tiku_media_card_read_size() <= SURFACE_MEDIA_READ_BYTES && tiku_media_card_mutation_size() <= SURFACE_MEDIA_MUTATION_BYTES ? sizeof(media_bridge_t) : SIZE_MAX; }
void surface_os_media_init(void *data)
{
    media_bridge_t *b = data;
    memset(b, 0, sizeof *b);
    b->ready = tiku_media_card_read_size() <= sizeof b->read_state &&
        tiku_media_card_mutation_size() <= sizeof b->mutation_state;
    if (b->ready) {
        tiku_media_card_read_init(b->read_state);
        tiku_media_card_mutation_init(b->mutation_state);
    }
}
/** @brief Convert media results while keeping stale identity distinct from removal. */
static int status(int r)
{
    if (!r) return SURFACE_MEDIA_OK;
    if (r == TIKU_MFS_NOENT) return SURFACE_MEDIA_END;
    if (r == TIKU_MFS_BUSY) return SURFACE_MEDIA_BUSY;
    if (r == TIKU_MFS_STALE) return SURFACE_MEDIA_STALE;
    if (r == TIKU_MFS_ARG) return SURFACE_MEDIA_INVALID;
    if (r == TIKU_MFS_LIMIT) return SURFACE_MEDIA_LIMIT;
    if (r == TIKU_MFS_SPACE) return SURFACE_MEDIA_FULL;
    if (r == TIKU_MFS_UNSUPPORTED) return SURFACE_MEDIA_UNSUPPORTED;
    return SURFACE_MEDIA_UNAVAILABLE;
}

/** @brief Account for the card's names epoch: any change since the last
 *  account beyond @p own of this bridge's own was another client's. */
static void account(media_bridge_t *b, uint64_t own)
{
    tiku_media_store_t *s = tiku_media_card_store();
    uint64_t e = s && s->fs ? tiku_mfs_names_epoch(s->fs) : 0;

    if (e == 0) return;
    if (b->known != 0 && e != b->known && e - b->known > own) b->foreign++;
    b->known = e;
}

/** @brief Answer a space request from the mounted volume: its bytes, and
 *  the bytes a writer may use past the catalog's reserve. */
static int space(media_bridge_t *b, surface_media_io_t *v)
{
    tiku_media_store_t *s = tiku_media_card_store();
    if (!s || !s->fs || !s->fs->mounted) return SURFACE_MEDIA_UNAVAILABLE;
    const tiku_mfs_t *f = s->fs;
    uint64_t sectors = (uint64_t)f->free_segments * TIKU_MFS_SEGMENT +
        (f->tail % TIKU_MFS_SEGMENT ? TIKU_MFS_SEGMENT - f->tail % TIKU_MFS_SEGMENT : 0);
    v->offset = (uint64_t)f->segments * TIKU_MFS_SEGMENT * 512u;
    v->size = sectors > TIKU_MFS_SEGMENT ? (sectors - TIKU_MFS_SEGMENT) * 512u : 0;
    v->result = SURFACE_MEDIA_OK;
    b->answered = 1;
    b->view = v;
    return SURFACE_MEDIA_OK;
}

int surface_os_media_submit(void *data, surface_media_io_t *v)
{
    media_bridge_t *b = data;
    if (b && b->ready && !b->view && surface_os_in_owner()) account(b, 0);
    if (b && v && surface_os_in_owner() && v->operation == SURFACE_MEDIA_SPACE) {
        if (!b->ready) return SURFACE_MEDIA_UNAVAILABLE;
        if (b->view) return SURFACE_MEDIA_BUSY;
        return space(b, v);
    }
    if (!b || !v || !surface_os_in_owner() || v->operation > (SURFACE_DIRECTORIES?SURFACE_MEDIA_TREE_COPY:SURFACE_MEDIA_REPLACE) ||
        v->length > ((v->operation==SURFACE_MEDIA_REPLACE||v->operation==SURFACE_MEDIA_TREE_COPY)?65536u:1024u))
        return SURFACE_MEDIA_INVALID;
    if (!b->ready) return SURFACE_MEDIA_UNAVAILABLE;
    if (b->view) return SURFACE_MEDIA_BUSY;
    if (v->operation == SURFACE_MEDIA_ENUMERATE || v->operation == SURFACE_MEDIA_OPEN ||
        v->operation >= SURFACE_MEDIA_RENAME)
        b->store = tiku_media_card_store();
    if (!b->store) return SURFACE_MEDIA_UNAVAILABLE;
    static const tiku_media_op_t operations[] = {TIKU_MEDIA_ENUMERATE, TIKU_MEDIA_OPEN_VERSION,
        TIKU_MEDIA_READ_AT, TIKU_MEDIA_CLOSE, TIKU_MEDIA_RENAME, TIKU_MEDIA_DELETE_VERSION, TIKU_MEDIA_REPLACE
#if SURFACE_DIRECTORIES
        ,TIKU_MEDIA_MKDIR,TIKU_MEDIA_TREE_RENAME,TIKU_MEDIA_TREE_DELETE,TIKU_MEDIA_DIRECTORY_UPGRADE,TIKU_MEDIA_TREE_COPY
#endif
        };
    b->request = (tiku_media_request_t){.op = operations[v->operation], .client = 0x53555246u,
        .permissions = v->operation==SURFACE_MEDIA_DIRECTORY_UPGRADE?TIKU_MEDIA_ADMIN:v->operation >= SURFACE_MEDIA_RENAME ? TIKU_MEDIA_WRITE : TIKU_MEDIA_READ, .name = v->name, .names_epoch = v->epoch,
        .rank = v->rank, .exclusive = (uint8_t)v->exclusive, .info = &b->info,
        .id = v->operation >= SURFACE_MEDIA_RENAME ? v->id : v->reader, .offset = v->offset,
        .buffer = (v->operation == SURFACE_MEDIA_RENAME || v->operation == SURFACE_MEDIA_TREE_RENAME) ? (void *)v->destination : v->bytes, .length = (uint32_t)v->length};
#if SURFACE_DIRECTORIES
    if(v->operation==SURFACE_MEDIA_TREE_COPY){b->request.destination=v->destination;
        b->request.permissions=TIKU_MEDIA_READ|TIKU_MEDIA_WRITE;}
#endif
    int r = tiku_media_store_submit(b->store, &b->request);
    if (!r) b->view = v;
    return status(r);
}

/** @brief Report retirement of both shared-queue owners before releasing bridge memory. */
static int idle(const media_bridge_t *b)
{ return tiku_media_card_read_idle(b->read_state) && tiku_media_card_mutation_idle(b->mutation_state); }
/** @brief Advance the active owner or dispatch the FIFO head without synchronous I/O. */
static void progress(media_bridge_t *b)
{
    if (!tiku_media_card_read_idle(b->read_state))
        (void)tiku_media_card_read_step(b->read_state, b->store);
    else if (!tiku_media_card_mutation_idle(b->mutation_state) || (b->store->count &&
        (b->store->queue[0]->op == TIKU_MEDIA_RENAME || b->store->queue[0]->op == TIKU_MEDIA_DELETE_VERSION || b->store->queue[0]->op == TIKU_MEDIA_REPLACE
#if SURFACE_DIRECTORIES
         ||b->store->queue[0]->op == TIKU_MEDIA_MKDIR || b->store->queue[0]->op == TIKU_MEDIA_TREE_RENAME || b->store->queue[0]->op == TIKU_MEDIA_TREE_DELETE || b->store->queue[0]->op == TIKU_MEDIA_DIRECTORY_UPGRADE || b->store->queue[0]->op == TIKU_MEDIA_TREE_COPY
#endif
         )))
        (void)tiku_media_card_mutation_step(b->mutation_state, b->store);
    else (void)tiku_media_card_read_step(b->read_state, b->store);
}

int surface_os_media_poll(void *data)
{
    media_bridge_t *b = data;
    if (!b || !b->ready || !surface_os_in_owner()) return 0;
    if (!b->view) {
        if (b->store && b->store->count) progress(b);
        return 0;
    }
    if (b->answered) {
        b->answered = 0;
        b->view = NULL;
        return 1;
    }
    if (!b->request.done || !idle(b)) progress(b);
#if SURFACE_DIRECTORIES
    if(b->view->operation==SURFACE_MEDIA_TREE_COPY){b->view->prepared=b->request.prepared;b->view->total=b->request.total;}
#endif
    if (!b->request.done || !idle(b)) return 0;
    surface_media_io_t *v = b->view;
    int r = b->request.result;
    v->result = status(r);
    if (v->operation == SURFACE_MEDIA_READ && r >= 0) {
        v->count = (size_t)r; v->result = SURFACE_MEDIA_OK;
    }
    if (v->operation == SURFACE_MEDIA_ENUMERATE && (r == 0 || r == TIKU_MFS_NOENT)) {
        v->epoch = b->request.names_epoch; v->rank = b->request.rank;
#if SURFACE_DIRECTORIES
        v->directory=!r&&b->info.policy==TIKU_MFS_DIRECTORY;
#endif
        if (!r) { memcpy(v->found, b->info.name, 64); v->size = b->info.length; v->id = b->info.id; }
    }
    if (v->operation == SURFACE_MEDIA_OPEN && !r) {
        v->reader = b->request.id; v->size = b->info.length;
#if SURFACE_FILE_INFO
        v->id = b->info.id;
#endif
    }
    if (v->operation >= SURFACE_MEDIA_RENAME && !r) {v->epoch = b->store->fs->names_epoch;v->id=b->request.id;}
    /* A rename, delete or replace changes the names once. */
    account(b, v->operation >= SURFACE_MEDIA_RENAME && v->operation <= SURFACE_MEDIA_REPLACE && !r);
    b->view = NULL;
    return 1;
}

int surface_os_media_idle(void *data)
{
    media_bridge_t *b = data;
    if (!b || !b->ready || !surface_os_in_owner() || b->view) return 0;
    account(b, 0);
    return tiku_media_card_park_expire();
}

unsigned surface_os_media_foreign(const void *data)
{
    const media_bridge_t *b = data;
    return b ? b->foreign : 0u;
}

int surface_os_media_cancel(void *data)
{
    media_bridge_t *b = data;
    if (!b || !b->ready || !surface_os_in_owner()) return 0;
    if (b->answered) {
        b->answered = 0;
        b->view = NULL;
    }
    if (b->view && !b->request.done && b->view->operation != SURFACE_MEDIA_CLOSE) {
        int r = b->view->operation >= SURFACE_MEDIA_RENAME ?
            tiku_media_card_mutation_cancel(b->mutation_state, b->store, &b->request) :
            tiku_media_card_read_cancel(b->read_state, b->store, &b->request);
        if (r) return 0;
    }
    if (!idle(b)) progress(b);
    return idle(b);
}
