/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * card_fixture.c - an SD card on the host: MFS on a RAM disk behind the
 * media service and the desktop's bridge, with the card's read and write
 * transports completing a sector one poll after it is asked for.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "card_fixture.h"
#include "runtime.h"
#include <services/storage/tiku_media_card.h>
#include <services/storage/tiku_media_async.h>
#include <services/storage/tiku_media_mutation.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define DISK_BYTES (64u * 1024u * 1024u)
static unsigned char *disk;
static unsigned char used[4096], mark[4096];
static tiku_mfs_t fs;
static tiku_media_store_t store;
static tiku_block_t block;
static const uint8_t uuid[16] = {7};
static _Alignas(8) unsigned char bridge[SURFACE_MEDIA_BRIDGE_BYTES];
static int writes_fail;
static unsigned requests[16];
static unsigned foreign_base;       /* the counts of bridges before a remount */

static int read_disk(void *ctx, uint64_t lba, uint32_t n, void *out)
{ (void)ctx; if (lba + n > DISK_BYTES / 512u) return -1; memcpy(out, disk + lba * 512u, n * 512u); return 0; }
static int write_disk(void *ctx, uint64_t lba, uint32_t n, const void *in)
{
    (void)ctx;
    if (writes_fail || lba + n > DISK_BYTES / 512u) return -1;
    memcpy(disk + lba * 512u, in, n * 512u);
    return 0;
}
static int sync_disk(void *ctx) { (void)ctx; return writes_fail ? -1 : 0; }

int surface_os_in_owner(void) { return 1; }
int tiku_media_card_park_expire(void) { return 0; }
tiku_media_store_t *tiku_media_card_store(void) { return fs.mounted ? &store : NULL; }

/* The read transport: one sector a poll. */
typedef struct {
    tiku_media_async_t queue;
    uint64_t lba;
    void *output;
    unsigned opened, pending;
} read_state_t;
static int acquire_read(void *ctx, void *backend)
{ (void)backend; read_state_t *r = ctx; r->opened = 1; r->pending = 1; return 1; }
static int start_read(void *ctx, uint64_t lba, void *output)
{ read_state_t *r = ctx; r->lba = lba; r->output = output; r->pending = 2; return 1; }
static int poll_read(void *ctx)
{
    read_state_t *r = ctx;
    int rc = r->pending == 2 ? read_disk(NULL, r->lba, 1, r->output) : 0;

    r->pending = 0;
    if (rc) r->opened = 0;
    return rc ? TIKU_MFS_IO : 0;
}
static int close_read(void *ctx) { read_state_t *r = ctx; r->opened = r->pending = 0; return 0; }
size_t tiku_media_card_read_size(void) { return sizeof(read_state_t); }
void tiku_media_card_read_init(void *ctx)
{
    static const tiku_media_async_ops_t ops = {acquire_read, start_read, poll_read, close_read};
    read_state_t *r = ctx;

    memset(r, 0, sizeof *r);
    tiku_media_async_init(&r->queue, &ops, r);
}
int tiku_media_card_read_step(void *ctx, tiku_media_store_t *s)
{ return tiku_media_async_step(&((read_state_t *)ctx)->queue, s); }
int tiku_media_card_read_idle(const void *ctx) { return !((const read_state_t *)ctx)->queue.phase; }
int tiku_media_card_read_cancel(void *ctx, tiku_media_store_t *s, tiku_media_request_t *request)
{ return tiku_media_async_cancel(&((read_state_t *)ctx)->queue, s, request); }

/* The write transport: one sector, or the barrier, a poll. */
typedef struct {
    tiku_media_mutation_t queue;
    uint64_t lba;
    void *buffer;
    unsigned opened, pending;
} write_state_t;
static int acquire_write(void *ctx, void *backend)
{ (void)backend; write_state_t *w = ctx; w->opened = 1; w->pending = 1; return 1; }
static int write_io(write_state_t *w, uint64_t lba, void *p, unsigned kind)
{ w->lba = lba; w->buffer = p; w->pending = kind; return 1; }
static int mutation_read(void *ctx, uint64_t lba, void *p) { return write_io(ctx, lba, p, 2); }
static int mutation_write(void *ctx, uint64_t lba, const void *p) { return write_io(ctx, lba, (void *)p, 3); }
static int mutation_sync(void *ctx) { return write_io(ctx, 0, NULL, 4); }
static int poll_write(void *ctx)
{
    write_state_t *w = ctx;
    int rc = w->pending == 2 ? read_disk(NULL, w->lba, 1, w->buffer) :
             w->pending == 3 ? write_disk(NULL, w->lba, 1, w->buffer) :
             w->pending == 4 ? sync_disk(NULL) : 0;

    w->pending = 0;
    w->buffer = NULL;
    if (rc) w->opened = 0;
    return rc ? TIKU_MFS_IO : 0;
}
static int close_write(void *ctx)
{ write_state_t *w = ctx; w->opened = w->pending = 0; w->buffer = NULL; return 0; }
static size_t write_offset(void) { return (sizeof(write_state_t) + 7u) & ~(size_t)7u; }
size_t tiku_media_card_mutation_size(void) { return write_offset() + tiku_mfs_mutation_size(); }
void tiku_media_card_mutation_init(void *ctx)
{
    static const tiku_media_mutation_ops_t ops = {acquire_write, mutation_read, mutation_write,
        mutation_sync, poll_write, close_write};
    write_state_t *w = ctx;
    tiku_mfs_mutation_t *m;

    memset(w, 0, sizeof *w);
    m = tiku_mfs_mutation_init((unsigned char *)ctx + write_offset(), tiku_mfs_mutation_size());
    assert(m != NULL);
    tiku_media_mutation_init(&w->queue, m, &ops, w);
}
int tiku_media_card_mutation_step(void *ctx, tiku_media_store_t *s)
{ return tiku_media_mutation_step(&((write_state_t *)ctx)->queue, s); }
int tiku_media_card_mutation_idle(const void *ctx) { return !((const write_state_t *)ctx)->queue.phase; }
int tiku_media_card_mutation_cancel(void *ctx, tiku_media_store_t *s, tiku_media_request_t *r)
{ return tiku_media_mutation_cancel(&((write_state_t *)ctx)->queue, s, r); }

/** @brief Mount the card from its sectors behind a fresh service and bridge. */
static void mount(void)
{
    memset(&fs, 0, sizeof fs);
    assert(!tiku_media_store_init(&store, &fs, &block, uuid, used, mark, sizeof used, NULL, NULL, NULL));
    assert(!tiku_mfs_mount(&fs, &block, uuid, used, mark, sizeof used));
    assert(surface_os_media_size() <= sizeof bridge);
    foreign_base += surface_os_media_foreign(bridge) + 1u;
    surface_os_media_init(bridge);
}

void card_fixture_reset(void)
{
    if (disk == NULL) disk = malloc(DISK_BYTES);
    assert(disk != NULL);
    memset(disk, 0, DISK_BYTES);
    writes_fail = 0;
    memset(requests, 0, sizeof requests);
    block = (tiku_block_t){.context = NULL, .sectors = DISK_BYTES / 512u, .read = read_disk,
                           .write = write_disk, .sync = sync_disk};
    memset(&fs, 0, sizeof fs);
    assert(!tiku_media_store_init(&store, &fs, &block, uuid, used, mark, sizeof used, NULL, NULL, NULL));
    assert(!tiku_mfs_format(&fs, &block, uuid, used, mark, sizeof used));
    mount();
}

void card_fixture_request(surface_media_io_t *io)
{
    unsigned turns = 0;
    int r;

    if (io->operation < sizeof requests / sizeof requests[0]) requests[io->operation]++;
    while ((r = surface_os_media_submit(bridge, io)) == SURFACE_MEDIA_BUSY) {
        (void)surface_os_media_poll(bridge);
        assert(++turns < 1000000u);
    }
    if (r != SURFACE_MEDIA_OK) {
        io->result = r;
        return;
    }
    while (!surface_os_media_poll(bridge)) assert(++turns < 1000000u);
}

void card_fixture_put(const char *name, const void *bytes, size_t len)
{
    uint64_t id;

    assert(!tiku_mfs_begin(&fs, name, TIKU_MFS_STREAM, 0, NULL, &id));
    for (size_t at = 0; at < len; at += TIKU_MFS_CHUNK) {
        size_t n = len - at < TIKU_MFS_CHUNK ? len - at : TIKU_MFS_CHUNK;

        assert(!tiku_mfs_append(&fs, (const unsigned char *)bytes + at, (uint32_t)n));
    }
    assert(!tiku_mfs_finalize(&fs));
}

long card_fixture_get(const char *name, void *out, size_t max)
{
    tiku_mfs_object_t o;
    uint64_t reader;
    long got;

    if (tiku_mfs_stat(&fs, name, &o) != 0) return -1;
    assert(o.length <= max);
    assert(!tiku_mfs_open_read(&fs, name, &reader));
    for (got = 0; (uint64_t)got < o.length;) {
        uint64_t left = o.length - (uint64_t)got;
        int n = tiku_mfs_read_at(&fs, reader, (uint64_t)got, (unsigned char *)out + got,
                                 (uint32_t)(left < TIKU_MFS_CHUNK ? left : TIKU_MFS_CHUNK));

        assert(n > 0);
        got += n;
    }
    assert(!tiku_mfs_close_read(&fs, reader));
    assert(got == (long)o.length);
    return got;
}

void card_fixture_pending(const char *name)
{
    static const uint8_t digest[32];
    uint64_t id;

    assert(!tiku_mfs_begin(&fs, name, TIKU_MFS_ATOMIC, 1, digest, &id));
    assert(!tiku_mfs_release(&fs));
}

unsigned card_fixture_count(void)
{
    tiku_mfs_cursor_t cursor;
    tiku_mfs_object_t o;
    unsigned n = 0;

    memset(&cursor, 0, sizeof cursor);
    while (tiku_mfs_list(&fs, &cursor, &o) == 0) n++;
    return n;
}

void card_fixture_remount(void) { mount(); }
void card_fixture_fail_writes(int on) { writes_fail = on; }
unsigned card_fixture_requests(unsigned operation)
{ return operation < sizeof requests / sizeof requests[0] ? requests[operation] : 0u; }

void card_fixture_delete(const char *name)
{
    tiku_mfs_object_t o;

    assert(!tiku_mfs_stat(&fs, name, &o));
    assert(!tiku_mfs_delete(&fs, o.id));
}

unsigned card_fixture_foreign(void)
{
    (void)surface_os_media_idle(bridge);
    return foreign_base + surface_os_media_foreign(bridge);
}
