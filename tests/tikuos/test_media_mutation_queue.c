/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_mutation_queue.c - queued mutations and retained transport ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_MUTATION_QUEUE_FIXTURE 1
#include "test_media_mutation.c"
#include <services/storage/tiku_media_mutation.c>
/** @brief Delayed transport with independent admission, polling and close refusals. */
typedef struct {
    void *buffer;uint64_t lba;
    unsigned open,kind,delay,calls,busy,close_busy,poll_arg,fail;
} queue_transport_t;
/** @brief Acquire without touching disk or retaining a refused session. */
static int queue_acquire(void *context,void *backend)
{
    queue_transport_t *t=context;(void)backend;++t->calls;assert(!t->open);
    if(t->busy){--t->busy;return TIKU_MFS_BUSY;}
    t->open=1;t->kind=4;t->delay=2;return 1;
}
/** @brief Borrow a buffer only when admission succeeds. */
static int queue_io(queue_transport_t *t,unsigned kind,uint64_t lba,void *p)
{
    ++t->calls;assert(t->open&&!t->kind);
    if(t->busy){--t->busy;return TIKU_MFS_BUSY;}
    t->buffer=p;t->lba=lba;t->kind=kind;t->delay=2;return 1;
}
/** @brief Admit a delayed sector read. */
static int queue_read(void *t,uint64_t lba,void *p){return queue_io(t,1,lba,p);}
/** @brief Admit a delayed sector write. */
static int queue_write(void *t,uint64_t lba,const void *p){return queue_io(t,2,lba,(void *)p);}
/** @brief Admit a delayed ordered barrier. */
static int queue_sync(void *t){return queue_io(t,3,0,NULL);}
/** @brief Perform one retired operation or retain the lease through a refusal. */
static int queue_poll(void *context)
{
    queue_transport_t *t=context;++t->calls;assert(t->open&&t->kind);
    if(t->poll_arg){--t->poll_arg;return TIKU_MFS_ARG;}
    if(t->delay){--t->delay;return 1;}
    int rc=t->fail?TIKU_MFS_IO:t->kind==1?disk_read(NULL,t->lba,1,t->buffer):
        t->kind==2?disk_write(NULL,t->lba,1,t->buffer):t->kind==3?disk_sync(NULL):0;
    t->kind=0;t->buffer=NULL;if(rc)t->open=0;return rc?TIKU_MFS_IO:0;
}
/** @brief Release storage only after the simulated transport retires. */
static int queue_close(void *context)
{
    queue_transport_t *t=context;++t->calls;assert(t->open);
    if(t->close_busy){--t->close_busy;return TIKU_MFS_BUSY;}
    t->open=t->kind=0;t->buffer=NULL;return 0;
}
static const tiku_media_mutation_ops_t queue_ops={queue_acquire,queue_read,queue_write,queue_sync,queue_poll,queue_close};
/** @brief Assert one callback at most and no early request completion. */
static void queue_step(tiku_media_mutation_t *a,tiku_media_store_t *s,queue_transport_t *t)
{
    unsigned before=t->calls;assert(tiku_media_mutation_step(a,s)>=0);assert(t->calls-before<=1);
    if(a->phase)assert(s->running==4&&!a->request->done);
    if(!a->phase)assert(!t->open&&!t->buffer);
}
/** @brief Complete an accepted request with an explicit finite turn bound. */
static void queue_drain(tiku_media_mutation_t *a,tiku_media_store_t *s,queue_transport_t *t,tiku_media_request_t *r)
{ for(unsigned n=0;!r->done;++n){assert(n<20000);queue_step(a,s,t);}assert(!s->running&&!t->open&&!t->buffer); }
#ifndef SURFACE_STREAM_QUEUE_FIXTURE
/** @brief Exercise permissions, FIFO admission, refusals, retirement and owner reuse. */
static void queued_mutations(void)
{
    fresh();uint64_t id=create_file("before");tiku_media_store_t s={.fs=&fs};queue_transport_t t={.busy=2,.poll_arg=2};
    tiku_media_mutation_t a;tiku_media_mutation_init(&a,mutation,&queue_ops,&t);
    tiku_media_request_t r={.op=TIKU_MEDIA_RENAME,.client=1,.permissions=TIKU_MEDIA_READ,.id=id,.name="before",.buffer="after"};
    assert(tiku_media_store_submit(&s,&r)==TIKU_MFS_ARG);r.permissions=TIKU_MEDIA_WRITE;assert(!tiku_media_store_submit(&s,&r));
    queue_step(&a,&s,&t);assert(tiku_media_store_step(&s)==TIKU_MFS_BUSY);
    tiku_media_request_t later={.op=TIKU_MEDIA_DELETE_VERSION,.client=2,.permissions=TIKU_MEDIA_WRITE,.id=id,.name="after"};
    assert(!tiku_media_store_submit(&s,&later));
    tiku_media_mutation_t stranger;tiku_media_mutation_init(&stranger,mutation,&queue_ops,&t);
    assert(tiku_media_mutation_step(&stranger,&s)==TIKU_MFS_BUSY);
    assert(tiku_media_mutation_cancel(&stranger,&s,&r)==TIKU_MFS_BUSY);
    t.close_busy=2;queue_drain(&a,&s,&t,&r);assert(!r.result&&s.count==1&&s.queue[0]==&later);
    queue_drain(&a,&s,&t,&later);assert(!later.result&&!s.count);reboot();
    tiku_mfs_object_t o;assert(tiku_mfs_stat(&fs,"after",&o)==TIKU_MFS_NOENT);
    id=create_file("third");r.id=id;r.name="third";r.buffer="fourth";assert(!tiku_media_store_submit(&s,&r));
    t.fail=1;queue_drain(&a,&s,&t,&r);assert(r.result==TIKU_MFS_IO&&s.failed&&fs.failed);
    reboot();s.failed=0;t.fail=0;assert(!tiku_media_store_submit(&s,&r));queue_drain(&a,&s,&t,&r);assert(!r.result);
}
/** @brief Cancel throughout read, write, checkpoint and publication phases. */
static void queued_cancellations(void)
{
    fresh();uint64_t id=create_file("source");create_file("neighbor");memcpy(snapshot,stable,BYTES);
    for(unsigned stop=0;stop<600;++stop){
        memcpy(stable,snapshot,BYTES);reboot();tiku_media_store_t s={.fs=&fs};queue_transport_t t={0};
        tiku_media_mutation_t a;tiku_media_mutation_init(&a,mutation,&queue_ops,&t);
        tiku_media_request_t r={.op=TIKU_MEDIA_RENAME,.client=1,.permissions=TIKU_MEDIA_WRITE,.id=id,.name="source",.buffer="dest"};
        assert(!tiku_media_store_submit(&s,&r));for(unsigned i=0;i<stop&&!r.done;++i)queue_step(&a,&s,&t);
        if(!r.done){
            if(t.open){void *borrowed=t.buffer;t.close_busy=2;
                for(unsigned n=0;n<2;++n)assert(tiku_media_mutation_cancel(&a,&s,&r)==TIKU_MFS_BUSY&&t.buffer==borrowed&&!r.done&&s.running==4);}
            assert(!tiku_media_mutation_cancel(&a,&s,&r)&&r.done&&r.result==TIKU_MFS_STALE);
        }
        assert(!t.open&&!t.buffer&&!s.running);reboot();
        tiku_mfs_object_t x,y;int old=tiku_mfs_stat(&fs,"source",&x),new=tiku_mfs_stat(&fs,"dest",&y);
        assert((!old&&new==TIKU_MFS_NOENT)||(old==TIKU_MFS_NOENT&&!new));
    }
}
/** @brief Retain replacement payloads through queue ownership and transport close. */
static void queued_contents(void)
{
    fresh();tiku_media_store_t s={.fs=&fs};queue_transport_t t={0};
    tiku_media_mutation_t a;tiku_media_mutation_init(&a,mutation,&queue_ops,&t);
    tiku_media_request_t r={.op=TIKU_MEDIA_REPLACE,.client=1,.permissions=TIKU_MEDIA_READ,.name="document",.buffer="abc",.length=3};
    assert(tiku_media_store_submit(&s,&r)==TIKU_MFS_ARG);r.permissions=TIKU_MEDIA_WRITE;
    assert(!tiku_media_store_submit(&s,&r));assert(tiku_media_store_step(&s)==TIKU_MFS_BUSY&&!r.done&&s.count==1);
    t.close_busy=2;queue_drain(&a,&s,&t,&r);assert(!r.result&&r.id);
    uint64_t old=r.id;assert(!tiku_media_store_submit(&s,&r));queue_drain(&a,&s,&t,&r);
    assert(!r.result&&r.id!=old);tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"document",&o)&&o.id==r.id&&o.length==3);
    r.id=old;assert(!tiku_media_store_submit(&s,&r));queue_drain(&a,&s,&t,&r);assert(r.result==TIKU_MFS_STALE);
    r.id=o.id;r.buffer=NULL;r.length=0;assert(!tiku_media_store_submit(&s,&r));queue_drain(&a,&s,&t,&r);
    assert(!r.result&&!tiku_mfs_stat(&fs,"document",&o)&&!o.length);
}

int main(void)
{
    stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);void *memory=malloc(tiku_mfs_mutation_size());
    assert(stable&&cache&&snapshot&&memory);mutation=tiku_mfs_mutation_init(memory,tiku_mfs_mutation_size());assert(mutation);
    queued_contents();queued_mutations();queued_cancellations();free(memory);free(stable);free(cache);free(snapshot);
    puts("Mutation queue: permissions, bounded callbacks, FIFO, cancellation leases and failed-owner reuse passed");return 0;
}

#endif
