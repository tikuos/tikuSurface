/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_stream_queue.c - writer identity and transport retirement during uploads.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_STREAM_QUEUE_FIXTURE 1
#include "test_media_mutation_queue.c"
/** @brief Submit a writer request and retire its transport lease. */
static int streamed(tiku_media_mutation_t *a,tiku_media_store_t *s,queue_transport_t *t,tiku_media_request_t *r)
{
    int rc=tiku_media_store_submit(s,r);if(rc)return rc;
    queue_drain(a,s,t,r);return r->result;
}
/** @brief Preserve writer identity and FIFO order across multi-request uploads. */
static void stream_queue(void)
{
    fresh();tiku_media_store_t s={.fs=&fs};queue_transport_t t={.busy=2,.poll_arg=2};
    tiku_media_mutation_t a;tiku_media_mutation_init(&a,mutation,&queue_ops,&t);tiku_mfs_object_t info;
    tiku_media_request_t r={.op=TIKU_MEDIA_BEGIN,.client=7,.permissions=TIKU_MEDIA_WRITE,
        .name="upload",.policy=TIKU_MFS_STREAM,.info=&info};
    t.close_busy=2;assert(!streamed(&a,&s,&t,&r)&&r.id&&r.id==info.id&&s.writer==7&&fs.writing);
    uint64_t id=r.id;
    r=(tiku_media_request_t){.op=TIKU_MEDIA_APPEND,.client=8,.permissions=TIKU_MEDIA_WRITE,.buffer=payload,.length=4097};
    assert(streamed(&a,&s,&t,&r)==TIKU_MFS_BUSY&&fs.draft.length==0&&s.writer==7);
    r.client=7;assert(!tiku_media_store_submit(&s,&r));queue_step(&a,&s,&t);
    tiku_media_request_t later={.op=TIKU_MEDIA_BEGIN,.client=8,.permissions=TIKU_MEDIA_WRITE,.name="other",.policy=TIKU_MFS_STREAM};
    assert(!tiku_media_store_submit(&s,&later));queue_drain(&a,&s,&t,&r);assert(!r.result&&s.count==1&&s.writer==7);
    queue_drain(&a,&s,&t,&later);assert(later.result==TIKU_MFS_BUSY&&fs.draft.length==4097);
    r.op=TIKU_MEDIA_SYNC;assert(!streamed(&a,&s,&t,&r));r.op=TIKU_MEDIA_RELEASE;
    assert(!streamed(&a,&s,&t,&r)&&!s.writer);
    r.op=TIKU_MEDIA_RESUME;r.id=id;r.info=&info;assert(!streamed(&a,&s,&t,&r)&&s.writer==7&&info.length==4097);
    r.op=TIKU_MEDIA_FINALIZE;assert(!streamed(&a,&s,&t,&r)&&!s.writer&&!fs.writing);
    reboot();assert(!tiku_mfs_stat(&fs,"upload",&info)&&info.complete&&info.length==4097);
}
/** @brief Retain completed writer transitions when cancellation arrives during transport close. */
static void stream_late_cancel(void)
{
    fresh();tiku_media_store_t s={.fs=&fs};queue_transport_t t={0};
    tiku_media_mutation_t a;tiku_media_mutation_init(&a,mutation,&queue_ops,&t);
    tiku_media_request_t r={.op=TIKU_MEDIA_BEGIN,.client=7,.permissions=TIKU_MEDIA_WRITE,.name="upload",.policy=TIKU_MFS_STREAM};
    for(unsigned operation=0;operation<3;++operation){
        assert(!tiku_media_store_submit(&s,&r));
        for(unsigned n=0;!a.begun||!mutation->done;++n){assert(n<2000);queue_step(&a,&s,&t);}
        assert(!r.done&&t.open);t.close_busy=2;
        assert(tiku_media_mutation_cancel(&a,&s,&r)==TIKU_MFS_BUSY&&!r.done);
        assert(tiku_media_mutation_cancel(&a,&s,&r)==TIKU_MFS_BUSY&&!r.done);
        assert(!tiku_media_mutation_cancel(&a,&s,&r)&&r.done&&r.result==TIKU_MFS_STALE);
        assert(s.writer==(operation==2?0:7)&&fs.writing==(operation!=2));
        if(!operation){assert(!tiku_mfs_release(&fs));s.writer=0;r.op=TIKU_MEDIA_RESUME;}
        else r.op=TIKU_MEDIA_FINALIZE;
    }
}
int main(void)
{
    stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);
    void *memory=malloc(tiku_mfs_mutation_size());assert(stable&&cache&&snapshot&&memory);
    mutation=tiku_mfs_mutation_init(memory,tiku_mfs_mutation_size());assert(mutation);
    for(unsigned i=0;i<sizeof payload;++i)payload[i]=(uint8_t)(i*37+11);
    stream_queue();stream_late_cancel();
    free(memory);free(stable);free(cache);free(snapshot);
    puts("Streaming queue: writer ownership, FIFO, resume and cancellation after publication passed");return 0;
}
