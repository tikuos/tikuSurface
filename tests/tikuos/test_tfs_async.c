/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_tfs_async.c - polled replacement ownership and interrupted durable writes.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <kernel/fs/tiku_tfs_async.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static _Alignas(8) unsigned char disk[TIKU_TFS_EXTENT_FOR_SLOTS(TIKU_TFS_MIN_SLOTS)];
static unsigned char baseline[sizeof disk], payload[8193], got[sizeof payload];
static const unsigned char old[] = "previous calibration", neighbour[] = "user note";
static tiku_tfs_t fs;
static unsigned sync_allowed, sync_calls, fault_cases, cancel_cases;

/** @brief Allow synchronous writes only during fixture setup and mount. */
static int write_nvm(tiku_nvm_backend_t *be, size_t off, const void *data, size_t n)
{
    assert(sync_allowed && off <= be->size && n <= be->size-off);
    ++sync_calls; memcpy(be->base+off,data,n); return 0;
}
static tiku_nvm_backend_t backend = {.base=disk,.size=sizeof disk,.write=write_nvm};

/** @brief A delayed backend borrows the source and can fail before or after persistence. */
typedef struct {
    const void *source;
    unsigned char borrowed[256];
    size_t off, length, lengths[64];
    unsigned callbacks, accepted, delay, attempt, fail_call, fail_prefix, reject_call;
} model_t;

/** @brief Reject the first attempt at each write, then retain the caller's bytes. */
static int begin(void *context, tiku_nvm_backend_t *be, size_t off, const void *data, size_t n)
{
    model_t *m=context;
    ++m->callbacks;
    assert(be==&backend && !m->source && n && n<=sizeof m->borrowed);
    assert(off<=be->size && n<=be->size-off);
    if (m->accepted==m->reject_call) return TFS_ERR_IO;
    if (!m->attempt++) return TFS_ERR_BUSY;
    m->attempt=0; m->source=data; m->off=off; m->length=n; m->delay=2;
    memcpy(m->borrowed,data,n);
    assert(m->accepted<64); m->lengths[m->accepted++]=n;
    return 1;
}

/** @brief Check borrowed bytes on each poll; the last poll persists or tears the request. */
static int poll(void *context)
{
    model_t *m=context;
    ++m->callbacks;
    assert(m->source && !memcmp(m->source,m->borrowed,m->length));
    if (m->delay) return --m->delay ? 1 : TFS_ERR_BUSY;
    size_t n=m->length;
    int failed=m->accepted-1==m->fail_call;
    if (failed) { assert(m->fail_prefix<=n); n=m->fail_prefix; }
    memcpy(disk+m->off,m->source,n); m->source=NULL;
    return failed ? TFS_ERR_IO : 0;
}
static const tiku_tfs_async_ops_t ops = {begin,poll};

/** @brief Restore backend availability and discard volatile filesystem state. */
static void remount(void)
{
    sync_allowed=1; memset(&fs,0,sizeof fs);
    assert(tiku_tfs_mount(&fs,&backend)==TFS_OK); sync_allowed=0;
}

/** @brief Prepare an unrelated file and an optional previous version. */
static void prepare(int prior)
{
    sync_allowed=1; memset(disk,0,sizeof disk); memset(&fs,0,sizeof fs);
    assert(tiku_tfs_init(&fs,&backend)==TFS_OK);
    assert(tiku_tfs_write(&fs,"user-note",neighbour,sizeof neighbour)==TFS_OK);
    if (prior) assert(tiku_tfs_write(&fs,"calibration",old,sizeof old)==TFS_OK);
    sync_allowed=0; memcpy(baseline,disk,sizeof disk);
}

/** @brief Compare a file's reported length and complete contents. */
static int matches(const char *name, const void *bytes, size_t n)
{
    size_t length=0;
    int rc=tiku_tfs_read(&fs,name,got,sizeof got,&length);
    return rc==TFS_OK && length==n && !memcmp(got,bytes,n);
}

/** @brief Require the old file, or absence during first creation. */
static void expect_old(int prior)
{
    if (prior) assert(matches("calibration",old,sizeof old));
    else assert(tiku_tfs_stat(&fs,"calibration",&(size_t){0})==TFS_ERR_NOTFOUND);
}

/** @brief Begin with no backend callback or synchronous write. */
static void start(tiku_tfs_async_t *op, model_t *m, size_t length)
{
    memset(op,0,sizeof *op); memset(m,0,sizeof *m);
    m->fail_call=m->reject_call=~0u;
    unsigned before=sync_calls;
    assert(tiku_tfs_async_begin(&fs,op,"calibration",payload,length,&ops,m)==TFS_OK);
    assert(!m->callbacks && fs.wr_open && before==sync_calls);
}

/** @brief Bound a step to one callback and preserve ownership while a request is pending. */
static int step(tiku_tfs_async_t *op, model_t *m)
{
    unsigned before=m->callbacks;
    int rc=tiku_tfs_async_step(op);
    assert(rc>=0 && m->callbacks-before<=1);
    if (m->source) assert(rc==1 && fs.wr_open && op->writer.active && op->pending);
    if (!rc) assert(!m->source && !fs.wr_open && !op->writer.active && !op->data);
    return rc;
}

/** @brief Drain a bounded fixture operation. */
static unsigned finish(tiku_tfs_async_t *op, model_t *m)
{
    unsigned turns=0;
    while (step(op,m)) assert(++turns<1000);
    return turns+1;
}

/** @brief Check size boundaries, visibility, writer exclusion and slot reclamation. */
static void sizes_and_ownership(void)
{
    static const size_t sizes[]={0,1,32,255,256,257,4092,4093,4096,8193};
    for (unsigned prior=0;prior<2;prior++) for (unsigned i=0;i<sizeof sizes/sizeof sizes[0];i++) {
        prepare(prior); tiku_tfs_async_t op={0}, other={0}; model_t m;
        start(&op,&m,sizes[i]);
        assert(tiku_tfs_async_begin(&fs,&op,"calibration",payload,32,&ops,&m)==TFS_ERR_BUSY);
        assert(tiku_tfs_async_begin(&fs,&other,"other",payload,32,&ops,&m)==TFS_ERR_BUSY);
        assert(tiku_tfs_write(&fs,"other",payload,32)==TFS_ERR_BUSY);
        assert(tiku_tfs_delete(&fs,"user-note")==TFS_ERR_BUSY);
        tiku_tfs_hold_t hold={0}; assert(tiku_tfs_hold(&fs,&hold)==TFS_ERR_BUSY);
        unsigned turns=0;
        do { expect_old(prior); assert(++turns<1000); } while (step(&op,&m));
        assert(op.result==TFS_OK && matches("calibration",payload,sizes[i]));
        assert(matches("user-note",neighbour,sizeof neighbour));
        size_t used=1+(sizes[i]+4+TIKU_TFS_SLOT_BYTES-1)/TIKU_TFS_SLOT_BYTES;
        assert(tiku_tfs_used_slots(&fs)==used);
        remount(); assert(tiku_tfs_used_slots(&fs)==used && matches("calibration",payload,sizes[i]));
    }
    prepare(1);
    for (unsigned i=0;i<100;i++) {
        tiku_tfs_async_t op={0}; model_t m; size_t n=i%2 ? 8193 : 32;
        start(&op,&m,n); finish(&op,&m);
        assert(op.result==TFS_OK && matches("calibration",payload,n));
        assert(tiku_tfs_used_slots(&fs)==1+(n+4+TIKU_TFS_SLOT_BYTES-1)/TIKU_TFS_SLOT_BYTES);
    }
}

/** @brief Cancel at each turn; drain borrowed memory and refuse cancellation after commit submission. */
static void cancellations(void)
{
    for (unsigned prior=0;prior<2;prior++) {
        prepare(prior); tiku_tfs_async_t op={0}; model_t m;
        start(&op,&m,8193); unsigned turns=finish(&op,&m);
        for (unsigned cut=0;cut<turns;cut++) {
            memcpy(disk,baseline,sizeof disk); remount(); start(&op,&m,8193);
            for (unsigned i=0;i<cut;i++) assert(step(&op,&m));
            int committing=op.committing;
            assert(tiku_tfs_async_cancel(&op)==(committing ? TFS_ERR_BUSY : TFS_OK));
            if (m.source) assert(fs.wr_open && op.writer.active);
            finish(&op,&m);
            assert(op.result==(committing ? TFS_OK : TIKU_TFS_ASYNC_CANCELLED));
            if (committing) assert(matches("calibration",payload,8193));
            else { expect_old(prior); assert(tiku_tfs_used_slots(&fs)==1+prior); }
            remount();
            if (committing) assert(matches("calibration",payload,8193)); else expect_old(prior);
            assert(matches("user-note",neighbour,sizeof neighbour)); ++cancel_cases;
        }
    }
}

/** @brief Fail every byte prefix except inside atomic words, then discard RAM and recover. */
static void interrupted_writes(void)
{
    for (unsigned prior=0;prior<2;prior++) {
        prepare(prior); tiku_tfs_async_t op={0}; model_t m;
        start(&op,&m,257); finish(&op,&m);
        size_t lengths[64]; memcpy(lengths,m.lengths,sizeof lengths);
        unsigned count=m.accepted;
        for (unsigned call=0;call<count;call++) for (size_t prefix=0;prefix<=lengths[call];prefix++) {
            if (lengths[call]==4 && prefix!=0 && prefix!=4) continue;
            memcpy(disk,baseline,sizeof disk); remount(); start(&op,&m,257);
            m.fail_call=call; m.fail_prefix=(unsigned)prefix; finish(&op,&m);
            assert(op.result==TFS_ERR_IO && !fs.mounted);
            assert(tiku_tfs_read(&fs,"calibration",got,sizeof got,NULL)==TFS_ERR_INVAL);
            remount();
            if (!matches("calibration",payload,257)) expect_old(prior);
            assert(matches("user-note",neighbour,sizeof neighbour)); ++fault_cases;
        }
        for (unsigned call=0;call<count;call++) {
            memcpy(disk,baseline,sizeof disk); remount(); start(&op,&m,257);
            m.reject_call=call; finish(&op,&m);
            assert(op.result==TFS_ERR_IO && !fs.mounted); remount(); expect_old(prior);
            assert(matches("user-note",neighbour,sizeof neighbour)); ++fault_cases;
        }
    }
}

/** @brief Reject invalid requests, pinned readers, oversized writes and exhausted stores before I/O. */
static void admission(void)
{
    prepare(1); tiku_tfs_async_t op={0}; model_t m={0};
    assert(tiku_tfs_async_begin(NULL,&op,"x",payload,32,&ops,&m)==TFS_ERR_INVAL);
    assert(tiku_tfs_async_begin(&fs,NULL,"x",payload,32,&ops,&m)==TFS_ERR_INVAL);
    assert(tiku_tfs_async_begin(&fs,&op,"x",NULL,32,&ops,&m)==TFS_ERR_INVAL);
    assert(tiku_tfs_async_begin(&fs,&op,"x",payload,32,NULL,&m)==TFS_ERR_INVAL);
    assert(tiku_tfs_async_begin(&fs,&op,"",payload,32,&ops,&m)==TFS_ERR_NAMELEN);
    assert(tiku_tfs_async_begin(&fs,&op,"x",payload,TIKU_TFS_FILE_MAX+1u,&ops,&m)==TFS_ERR_TOOBIG);
    assert(tiku_tfs_async_step(NULL)==TFS_ERR_INVAL);
    assert(!tiku_tfs_async_step(&op) && tiku_tfs_async_cancel(&op)==TFS_ERR_INVAL);
    tiku_tfs_hold_t hold={0}; assert(tiku_tfs_hold(&fs,&hold)==TFS_OK);
    assert(tiku_tfs_async_begin(&fs,&op,"x",payload,32,&ops,&m)==TFS_ERR_BUSY);
    assert(tiku_tfs_release(&hold)==TFS_OK);
    sync_allowed=1;
    size_t n=(fs.nslots-2u)*TIKU_TFS_SLOT_BYTES-4u;
    memset(baseline,0xa5,sizeof baseline);
    assert(tiku_tfs_write(&fs,"bulk",baseline,n)==TFS_OK); sync_allowed=0;
    assert(tiku_tfs_async_begin(&fs,&op,"x",payload,32,&ops,&m)==TFS_ERR_NOSPACE);
    assert(!fs.wr_open && !m.callbacks);
}

/** @brief Exercise real TFS with delayed completion, cancellation and lost acknowledgements. */
int main(void)
{
    for (unsigned i=0;i<sizeof payload;i++) payload[i]=(unsigned char)(i^(i>>8));
    if (TIKU_TFS_ROTATE) {
        prepare(1); tiku_tfs_async_t op={0}; model_t m={0};
        assert(tiku_tfs_async_begin(&fs,&op,"calibration",payload,32,&ops,&m)==TFS_ERR_INVAL);
        assert(!fs.wr_open && !m.callbacks); expect_old(1);
        puts("TFS async: rotating geometry refused"); return 0;
    }
    sizes_and_ownership(); cancellations(); interrupted_writes(); admission();
    prepare(1);
    tiku_tfs_async_t op={0}; model_t m={.fail_call=~0u,.reject_call=~0u};
    assert(tiku_tfs_async_begin(&fs,&op,"calibration",NULL,0,&ops,&m)==TFS_OK);
    finish(&op,&m); assert(op.result==TFS_OK && matches("calibration",payload,0));
    assert(tiku_tfs_async_begin(&fs,&op,"calibration",payload,32,&ops,&m)==TFS_OK);
    finish(&op,&m); assert(op.result==TFS_OK && matches("calibration",payload,32));
    for (unsigned prefix=0;prefix<=256;prefix+=256) {
        prepare(1); start(&op,&m,257);
        while (!m.source) assert(step(&op,&m));
        assert(tiku_tfs_async_cancel(&op)==TFS_OK);
        m.fail_call=0; m.fail_prefix=prefix; finish(&op,&m);
        assert(op.result==TFS_ERR_IO && !fs.mounted);
        remount(); expect_old(1); assert(matches("user-note",neighbour,sizeof neighbour)); ++fault_cases;
    }
    printf("TFS async: %u interrupted writes, %u cancellation boundaries, context %zu bytes\n",
           fault_cases,cancel_cases,sizeof(tiku_tfs_async_t));
    return 0;
}
