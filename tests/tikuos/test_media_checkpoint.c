/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_checkpoint.c - checkpoint pacing, lease retirement and recovery cuts.
 * SPDX-License-Identifier: Apache-2.0
 */
#define TIKU_MEDIA_NAMES_ENABLE 1
#define TIKU_MEDIA_ASYNC_ENABLE 1
#define main mfs_fixture_main
#include <TikuBench/tests/host/kernel/test_mfs.c>
#undef main
#include <services/storage/tiku_media_checkpoint.c>
#include <assert.h>
/** @brief Delayed transport retaining borrowed sectors through completion. */
typedef struct {
    const void *buffer;uint64_t lba;
    unsigned open,kind,delay,close_busy,calls,submitted,completed;
} transport_t;
/** @brief Acquire one simulated write session. */
static int acquire(void *context,void *backend)
{ transport_t *t=context;(void)backend;assert(!t->open);t->open=1;t->kind=3;t->delay=2;++t->calls;return 1; }
/** @brief Borrow one sector without reading it until poll completes. */
static int write_sector(void *context,uint64_t lba,const void *p)
{ transport_t *t=context;assert(t->open&&!t->kind);t->buffer=p;t->lba=lba;t->kind=1;t->delay=2;++t->calls;++t->submitted;return 1; }
/** @brief Queue an ordered barrier without performing it. */
static int sync_sector(void *context)
{ transport_t *t=context;assert(t->open&&!t->kind);t->kind=2;t->delay=2;++t->calls;return 1; }
/** @brief Retire at most one delayed I/O, closing failed sessions. */
static int poll(void *context)
{
    transport_t *t=context;assert(t->open&&t->kind);++t->calls;
    if(t->delay){--t->delay;return 1;}
    int rc=t->kind==1?disk_write(NULL,t->lba,1,t->buffer):t->kind==2?disk_sync(NULL):0;
    if(t->kind==1)++t->completed;
    t->kind=0;t->buffer=NULL;if(rc)t->open=0;
    return rc?TIKU_MFS_IO:0;
}
/** @brief Model cancellation that temporarily retains the borrowed sector. */
static int close_transport(void *context)
{
    transport_t *t=context;++t->calls;assert(t->open);
    if(t->close_busy){--t->close_busy;return TIKU_MFS_BUSY;}
    t->open=t->kind=0;t->buffer=NULL;return 0;
}
static const tiku_media_write_ops_t checkpoint_ops={acquire,write_sector,sync_sector,poll,close_transport};
/** @brief Bind a mounted filesystem without reinitializing the fixture volume. */
static tiku_media_store_t fixture_store(void)
{ return (tiku_media_store_t){.fs=&fs}; }
/** @brief Bound callback work and require exclusive filesystem ownership. */
static void step(tiku_media_checkpoint_t *a,tiku_media_store_t *s,transport_t *t)
{
    unsigned before=t->calls;assert(tiku_media_checkpoint_step(a,s)>=0);assert(t->calls-before<=1);
    if(a->phase)assert(s->running==3 && !a->request->done);
}
/** @brief Complete a queued checkpoint with a finite test deadline. */
static void drain(tiku_media_checkpoint_t *a,tiku_media_store_t *s,transport_t *t,tiku_media_request_t *r)
{ for(unsigned n=0;!r->done;++n){assert(n<20000);step(a,s,t);}assert(!t->open&&!t->buffer&&!s->running); }
/** @brief Publish multiple bitmap sectors and preserve namespace/data across remount. */
static void success(void)
{
    block.sectors=UINT64_C(1)<<24;fresh();uint64_t id;
    assert(!tiku_mfs_begin(&fs,"file",TIKU_MFS_STREAM,0,NULL,&id));assert(!tiku_mfs_append(&fs,"abc",3));assert(!tiku_mfs_finalize(&fs));
    tiku_media_store_t s=fixture_store();transport_t t={0};tiku_media_checkpoint_t a;tiku_media_checkpoint_init(&a,&checkpoint_ops,&t);
    tiku_media_request_t r={.op=TIKU_MEDIA_CHECKPOINT,.client=1,.permissions=TIKU_MEDIA_READ};
    assert(tiku_media_store_submit(&s,&r)==TIKU_MFS_ARG);r.permissions=TIKU_MEDIA_ADMIN;
    uint64_t sequence=fs.sequence,epoch=fs.epoch,version=fs.names_epoch;
    assert(!tiku_media_store_submit(&s,&r));step(&a,&s,&t);assert(!r.done&&s.running==3);
    assert(tiku_media_store_step(&s)==TIKU_MFS_BUSY);
    tiku_media_request_t later={.op=TIKU_MEDIA_STAT,.client=2,.permissions=TIKU_MEDIA_READ,.name="file"};
    assert(!tiku_media_store_submit(&s,&later));assert(!tiku_media_checkpoint_cancel(&a,&s,&later));
    drain(&a,&s,&t,&r);assert(!r.result && fs.epoch==epoch+1 && fs.sequence==sequence && fs.names_epoch==version && !fs.journal);
    assert(t.submitted==4 && t.completed==4);reboot();tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"file",&o)&&o.id==id&&o.length==3);
    block.sectors=BYTES/512;
}
/** @brief Interrupt every metadata write/barrier and recover the existing file. */
static void cuts(void)
{
    fresh();uint64_t id;assert(!tiku_mfs_begin(&fs,"kept",TIKU_MFS_STREAM,0,NULL,&id));assert(!tiku_mfs_finalize(&fs));
    memcpy(snapshot,stable,BYTES);unsigned count=0;
    for(unsigned mode=0;mode<3;++mode)for(unsigned cut=0;cut<=count;++cut){
        memcpy(stable,snapshot,BYTES);reboot();events=0;fail_mode=mode;fail_at=cut;
        tiku_media_store_t s=fixture_store();transport_t t={0};tiku_media_checkpoint_t a;tiku_media_checkpoint_init(&a,&checkpoint_ops,&t);
        tiku_media_request_t r={.op=TIKU_MEDIA_CHECKPOINT,.client=1,.permissions=TIKU_MEDIA_ADMIN};
        assert(!tiku_media_store_submit(&s,&r));drain(&a,&s,&t,&r);
        if(!cut){assert(!r.result);count=events;}else assert(r.result==TIKU_MFS_IO&&fs.failed&&s.failed);
        reboot();tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"kept",&o)&&o.id==id);
    }
    printf("Checkpoint: %u write/barrier cuts across three outcomes\n",count*3);
}
/** @brief Cancel at each owner turn while retaining in-flight sector storage. */
static void cancellations(void)
{
    fresh();memcpy(snapshot,stable,BYTES);
    for(unsigned stop=0;stop<70;++stop){
        memcpy(stable,snapshot,BYTES);reboot();tiku_media_store_t s=fixture_store();transport_t t={0};tiku_media_checkpoint_t a;
        tiku_media_checkpoint_init(&a,&checkpoint_ops,&t);tiku_media_request_t r={.op=TIKU_MEDIA_CHECKPOINT,.client=1,.permissions=TIKU_MEDIA_ADMIN};
        assert(!tiku_media_store_submit(&s,&r));for(unsigned i=0;i<stop&&!r.done;++i)step(&a,&s,&t);
        if(!r.done){
            if(t.open){t.close_busy=1;assert(tiku_media_checkpoint_cancel(&a,&s,&r)==TIKU_MFS_BUSY&&!r.done&&s.running==3);}
            unsigned changed=a.checkpoint.changed,was_done=a.checkpoint.done;
            assert(!tiku_media_checkpoint_cancel(&a,&s,&r)&&r.done&&r.result==TIKU_MFS_STALE);
            if(changed&&!was_done)assert(fs.failed);
        }
        assert(!t.open&&!t.buffer&&!s.running);reboot();
    }
}
int main(void)
{
    stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);assert(stable&&cache&&snapshot);
    success();cuts();cancellations();free(stable);free(cache);free(snapshot);
    puts("Checkpoint: bounded callbacks, multi-sector bitmap, queue ownership, cancellation and recovery passed");return 0;
}
