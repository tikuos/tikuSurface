/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media.c - queued browser pagination under interleaved media writes.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "media.h"
#include "host.h"
#if SURFACE_CONTENT_JOBS
#include "surface_internal.h"
#endif
#include "tiku_event.h"
#include <services/storage/tiku_media_card.h>
#include <services/storage/tiku_media_async.h>
#include <services/storage/tiku_media_mutation.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DISK_BYTES (256u * 1024u * 1024u)
static unsigned char *disk;
static unsigned char used[4096], mark[4096];
static tiku_mfs_t fs;
static tiku_media_store_t store;
static unsigned reads, offline;
static const uint8_t uuid[16] = {1};
static tiku_block_t block;
/** @brief Copy bounded emulated sectors and count metadata work. */
static int read_disk(void *ctx, uint64_t lba, uint32_t n, void *out)
{ (void)ctx; ++reads; if (offline || lba + n > DISK_BYTES / 512) return -1; memcpy(out,disk+lba*512,n*512); return 0; }
/** @brief Write only the fixture's private in-memory media. */
static int write_disk(void *ctx, uint64_t lba, uint32_t n, const void *in)
{ (void)ctx; if (offline || lba + n > DISK_BYTES / 512) return -1; memcpy(disk+lba*512,in,n*512); return 0; }
/** @brief Model an ordered successful persistence barrier. */
static int sync_disk(void *ctx) { (void)ctx; return offline ? -1 : 0; }
int surface_os_in_owner(void) { return 1; }
tiku_media_store_t *tiku_media_card_store(void) { return fs.mounted ? &store : NULL; }
/** @brief Polled in-memory transport keeps ownership across delayed sector completion. */
typedef struct {
    tiku_media_async_t queue;
    uint64_t lba;
    void *output;
    unsigned opened, pending, wait;
} read_state_t;
static unsigned close_refusals, maximum_turns;
/** @brief Accept a session without doing any synchronous filesystem work. */
static int acquire_read(void *ctx,void *backend)
{ (void)backend;read_state_t *r=ctx;assert(!r->opened);r->opened=1;r->pending=1;r->wait=1;return 1; }
/** @brief Borrow one sector destination until a later poll. */
static int start_read(void *ctx,uint64_t lba,void *output)
{ read_state_t *r=ctx;assert(r->opened&&!r->pending);r->lba=lba;r->output=output;r->pending=2;r->wait=1;return 1; }
/** @brief Complete at most one sector and close the simulated session on failure. */
static int poll_read(void *ctx)
{
    read_state_t *r=ctx;assert(r->opened&&r->pending);
    if(r->wait-- > 0)return 1;
    int rc=r->pending==2?read_disk(NULL,r->lba,1,r->output):0;
    r->pending=0;if(rc)r->opened=0;return rc?TIKU_MFS_IO:0;
}
/** @brief Model a temporarily refused close before releasing the borrowed buffer. */
static int close_read(void *ctx)
{
    read_state_t *r=ctx;assert(r->opened);
    if(close_refusals){--close_refusals;return TIKU_MFS_BUSY;}
    r->opened=r->pending=0;return 0;
}
size_t tiku_media_card_read_size(void) { return sizeof(read_state_t); }
void tiku_media_card_read_init(void *ctx)
{
    static const tiku_media_async_ops_t ops={acquire_read,start_read,poll_read,close_read};
    read_state_t *r=ctx;memset(r,0,sizeof *r);tiku_media_async_init(&r->queue,&ops,r);
}
int tiku_media_card_read_step(void *ctx,tiku_media_store_t *s)
{
    read_state_t *r=ctx;unsigned before=reads;
    int rc=tiku_media_async_step(&r->queue,s);assert(reads-before<=1);return rc;
}
int tiku_media_card_read_idle(const void *ctx) { return !((const read_state_t *)ctx)->queue.phase; }
int tiku_media_card_read_cancel(void *ctx,tiku_media_store_t *s,tiku_media_request_t *request)
{ return tiku_media_async_cancel(&((read_state_t *)ctx)->queue,s,request); }
/** @brief Delayed mutation transport and caller-owned opaque core state. */
typedef struct {
    tiku_media_mutation_t queue;
    uint64_t lba;
    void *buffer;
    unsigned opened, pending, wait;
} write_state_t;
/** @brief Acquire a delayed simulated write session. */
static int acquire_write(void *ctx,void *backend)
{ (void)backend;write_state_t *w=ctx;assert(!w->opened);w->opened=1;w->pending=1;w->wait=1;return 1; }
/** @brief Borrow one read/write sector until a later completion. */
static int write_io(write_state_t *w,uint64_t lba,void *p,unsigned kind)
{ assert(w->opened&&!w->pending);w->lba=lba;w->buffer=p;w->pending=kind;w->wait=1;return 1; }
/** @brief Queue a read under the mutation session. */
static int mutation_read_sector(void *ctx,uint64_t lba,void *p){return write_io(ctx,lba,p,2);}
/** @brief Queue a write under the mutation session. */
static int mutation_write_sector(void *ctx,uint64_t lba,const void *p){return write_io(ctx,lba,(void *)p,3);}
/** @brief Queue an ordered metadata barrier. */
static int mutation_sync_sector(void *ctx){return write_io(ctx,0,NULL,4);}
/** @brief Retire one delayed operation and release failed sessions. */
static int poll_write(void *ctx)
{
    write_state_t *w=ctx;assert(w->opened&&w->pending);
    if(w->wait-- > 0)return 1;
    int rc=w->pending==2?read_disk(NULL,w->lba,1,w->buffer):
        w->pending==3?write_disk(NULL,w->lba,1,w->buffer):w->pending==4?sync_disk(NULL):0;
    w->pending=0;w->buffer=NULL;if(rc)w->opened=0;return rc?TIKU_MFS_IO:0;
}
/** @brief Preserve the write owner through refused cancellation. */
static int close_write(void *ctx)
{
    write_state_t *w=ctx;assert(w->opened);
    if(close_refusals){--close_refusals;return TIKU_MFS_BUSY;}
    w->opened=w->pending=0;w->buffer=NULL;return 0;
}
/** @brief Align trailing core state independently of the host ABI. */
static size_t write_offset(void){return (sizeof(write_state_t)+7u)&~(size_t)7u;}
size_t tiku_media_card_mutation_size(void){return write_offset()+tiku_mfs_mutation_size();}
void tiku_media_card_mutation_init(void *ctx)
{
    static const tiku_media_mutation_ops_t ops={acquire_write,mutation_read_sector,mutation_write_sector,
        mutation_sync_sector,poll_write,close_write};
    write_state_t *w=ctx;memset(w,0,sizeof *w);
    tiku_mfs_mutation_t *m=tiku_mfs_mutation_init((unsigned char *)ctx+write_offset(),tiku_mfs_mutation_size());assert(m);
    tiku_media_mutation_init(&w->queue,m,&ops,w);
}
int tiku_media_card_mutation_step(void *ctx,tiku_media_store_t *s)
{ write_state_t *w=ctx;unsigned before=reads;int rc=tiku_media_mutation_step(&w->queue,s);assert(reads-before<=1);return rc; }
int tiku_media_card_mutation_idle(const void *ctx){return !((const write_state_t *)ctx)->queue.phase;}
int tiku_media_card_mutation_cancel(void *ctx,tiku_media_store_t *s,tiku_media_request_t *r)
{ return tiku_media_mutation_cancel(&((write_state_t *)ctx)->queue,s,r); }
/** @brief Settle application/provider work under a fixed queue-step ceiling. */
static void settle(surface_t *shell,surface_media_t *media)
{
    for(unsigned i=0;i<30000;i++){
        surface_step(shell);surface_media_poll(media);
        surface_view_t v;surface_view(shell,&v);
        if(!media->request&&!v.retiring&&v.status!=TIKU_RESOURCE_PENDING)return;
    }
    assert(0);
}
/** @brief Advance through snapshot acquisition without completing its payload read. */
static void opened_reader(surface_media_t *m)
{
    for(unsigned i=0;i<30000&&!m->io.reader;i++)surface_media_poll(m);
    assert(m->io.reader);
}
/** @brief Format independent fixture media without a real device. */
static void reset(void)
{
    memset(disk,0,DISK_BYTES);
    assert(!tiku_media_store_init(&store,&fs,&block,uuid,used,mark,sizeof used,NULL,NULL,NULL));
    assert(!tiku_mfs_format(&fs,&block,uuid,used,mark,sizeof used));
}
/** @brief Add a visible completed empty object. */
static void file(const char *name)
{
    uint64_t id;
    assert(!tiku_mfs_begin(&fs,name,TIKU_MFS_STREAM,0,NULL,&id));
    assert(!tiku_mfs_finalize(&fs));
}
/** @brief Interleave a committed content change without changing visible names. */
static void content_sync(void)
{
    assert(!tiku_mfs_append(&fs,"x",1));
    assert(!tiku_mfs_sync(&fs));
}
/** @brief Drain a bounded provider request while another owner syncs between queue turns. */
static unsigned drain(surface_media_t *m, tiku_paged_storage_t *owner, int interleave)
{
    unsigned turns = 0;
    while (tiku_paged_storage_pending(owner)) {
        assert(++turns < 30000);
        if (interleave && !store.running) content_sync();
        surface_media_poll(m);
    }
    if(turns>maximum_turns)maximum_turns=turns;
    return turns;
}
/** @brief Empty media completes with a version and no entries or retained requests. */
static void empty_catalog(void)
{
    surface_host_t host; surface_config_t config=surface_host_config(&host);
    surface_media_t media; void *bridge=calloc(1,surface_os_media_size()); assert(bridge);
    surface_os_media_init(bridge);
    tiku_storage_provider_t provider=surface_media_init(&media,bridge);
    tiku_resource_caller_t caller={0}; strcpy(caller.principal,"surface-local");
    tiku_paged_storage_t *owner=tiku_paged_storage_create(config.allocator,provider,&caller,16,1024); assert(owner);
    assert(tiku_paged_storage_list(owner,"/",0,0)==TIKU_RESOURCE_PENDING);
    drain(&media,owner,0);
    size_t count; uint64_t epoch; int eof;
    assert(tiku_paged_storage_result(owner,&count,&epoch,&eof) && !count && epoch && eof);
    assert(!media.request && !store.count);
    tiku_paged_storage_close(owner); assert(tiku_paged_storage_destroy(&owner));
    assert(!host.memory.live); free(bridge);
}
/** @brief Calculate reflected CRC32C for independently resealed metadata inputs. */
static uint32_t fixture_crc(const unsigned char *bytes,unsigned length)
{
    uint32_t crc=UINT32_MAX;
    while(length--){crc^=*bytes++;for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^((crc&1)?0x82f63b78u:0);}
    return ~crc;
}
/** @brief Drive a query with one supplied sector, optionally corrupting a selected wire input. */
static int query_drain(tiku_mfs_query_t *q,const char *corrupt)
{
    int changed=0;
    for(unsigned turns=0;turns<4096&&!q->done;turns++){
        if(q->waiting){
            assert(!read_disk(NULL,q->sector_lba,1,q->sector));
            if(corrupt&&!changed&&(!strcmp(corrupt,"payload")?q->slice_length!=0:!memcmp(q->sector,corrupt,4))){
                q->sector[0]^=1;changed=1;
            }
            if(corrupt&&!changed&&!strcmp(corrupt,"descriptor")&&!memcmp(q->sector,"MFO1",4)){
                q->sector[128]^=1;
                uint32_t crc=fixture_crc(q->sector,508);
                for(unsigned i=0;i<4;i++)q->sector[508+i]=(unsigned char)(crc>>(i*8));
                changed=1;
            }
            assert(!tiku_mfs_query_supply(q,0));
            assert(tiku_mfs_query_supply(q,0)==TIKU_MFS_ARG);
        }
        (void)tiku_mfs_query_step(&fs,q);
    }
    assert(q->done && (!corrupt||changed));return q->result;
}
/** @brief Check page bindings, slice CRC, boundary reads and pinned version lifetime. */
static void query_contract(void)
{
    assert(fixture_crc((const unsigned char *)"123456789",9)==0xe3069283u);
    unsigned char *data=malloc(70000),*out=malloc(65536);assert(data&&out);
    for(unsigned i=0;i<70000;i++)data[i]=(unsigned char)(i*29+i/251);
    uint64_t id,token;
    assert(!tiku_mfs_begin(&fs,"data",TIKU_MFS_STREAM,0,NULL,&id));
    assert(!tiku_mfs_append(&fs,data,65536)&&!tiku_mfs_append(&fs,data+65536,4464)&&!tiku_mfs_sync(&fs));
    tiku_mfs_query_t q;
    assert(!tiku_mfs_query_enumerate(&fs,&q,"data",0,0)&&!query_drain(&q,NULL));
    assert(q.rank==0&&q.object.length==70000&&!strcmp(q.object.name,"data"));
    assert(!tiku_mfs_query_open(&fs,&q,&token));
    assert(tiku_mfs_query_open(&fs,&q,&id)==TIKU_MFS_ARG);
    const uint64_t offsets[]={0,511,4000,4095,65530,69998,70000,UINT64_MAX};
    for(unsigned i=0;i<sizeof offsets/sizeof *offsets;i++){
        unsigned n=i?1024:65536;memset(out,0xaa,65536);
        assert(!tiku_mfs_query_read(&fs,&q,token,offsets[i],out,n));
        int got=query_drain(&q,NULL);
        unsigned expected=offsets[i]>=70000?0:(unsigned)(70000-offsets[i]);if(expected>n)expected=n;
        assert(got==(int)expected && (!got||!memcmp(out,data+(size_t)offsets[i],expected)));
    }
    const char *corrupt[]={"MFN1","MFD1","MFE1","payload"};
    for(unsigned i=0;i<4;i++){
        memset(out,0xaa,1024);
        assert(!tiku_mfs_query_read(&fs,&q,token,0,out,1024));
        assert(query_drain(&q,corrupt[i])==TIKU_MFS_CORRUPT);
        for(unsigned n=0;n<1024;n++)assert(out[n]==0xaa);
    }
    assert(!tiku_mfs_query_enumerate(&fs,&q,"data",0,0));
    assert(query_drain(&q,"MFI2")==TIKU_MFS_CORRUPT);
    assert(!tiku_mfs_query_enumerate(&fs,&q,"data",0,0));
    assert(query_drain(&q,"descriptor")==TIKU_MFS_CORRUPT);
    assert(!tiku_mfs_query_enumerate(&fs,&q,"data",0,0));
    (void)tiku_mfs_query_step(&fs,&q);content_sync();
    assert(!tiku_mfs_query_step(&fs,&q) && q.result==TIKU_MFS_STALE);
    assert(!tiku_mfs_query_read(&fs,&q,token,69998,out,16));
    assert(query_drain(&q,NULL)==2 && out[0]==data[69998] && out[1]==data[69999]);
    assert(!tiku_mfs_close_read(&fs,token)&&!tiku_mfs_finalize(&fs));
    free(out);free(data);
}
/** @brief Preserve unsupported FIFO work and retain buffers while close is refused. */
static void queue_contract(void)
{
    file("one");read_state_t state;tiku_media_card_read_init(&state);
    tiku_mfs_object_t info;
    tiku_media_request_t writer={.op=TIKU_MEDIA_BEGIN,.client=77,.permissions=TIKU_MEDIA_WRITE,
        .name="two",.policy=TIKU_MFS_STREAM};
    tiku_media_request_t request={.op=TIKU_MEDIA_ENUMERATE,.client=88,.permissions=TIKU_MEDIA_READ,.name="",.info=&info};
    assert(!tiku_media_store_submit(&store,&writer)&&!tiku_media_store_submit(&store,&request));
    unsigned before=reads;
    for(unsigned i=0;i<8;i++)assert(tiku_media_card_read_step(&state,&store)==2);
    assert(reads==before&&!writer.done&&!request.done&&!store.running);
    assert(tiku_media_store_step(&store)==1&&writer.done&&!writer.result);
    for(unsigned i=0;i<30000&&!request.done;i++){
        assert(tiku_media_card_read_step(&state,&store)==1);
        if(store.running)assert(tiku_media_store_step(&store)==TIKU_MFS_BUSY);
    }
    assert(request.done&&!request.result&&!strcmp(info.name,"one")&&!state.opened);
    writer=(tiku_media_request_t){.op=TIKU_MEDIA_FINALIZE,.client=77,.permissions=TIKU_MEDIA_WRITE};
    assert(!tiku_media_store_submit(&store,&writer)&&tiku_media_store_step(&store)==1&&!writer.result);
    request=(tiku_media_request_t){.op=TIKU_MEDIA_OPEN_VERSION,.client=88,.permissions=TIKU_MEDIA_READ,
        .name="one",.rank=0,.names_epoch=tiku_mfs_names_epoch(&fs),.info=&info};
    assert(!tiku_media_store_submit(&store,&request));
    while(state.queue.phase!=5)assert(tiku_media_card_read_step(&state,&store)==1);
    assert(!request.done&&state.opened);
    for(unsigned i=0;i<TIKU_MFS_READERS;i++)assert(!store.readers[i].token);
    close_refusals=3;
    for(unsigned i=0;i<3;i++){
        assert(tiku_media_card_read_cancel(&state,&store,&request)==TIKU_MFS_BUSY);
        assert(!request.done&&state.opened&&store.running==2);
    }
    assert(!tiku_media_card_read_cancel(&state,&store,&request));
    assert(request.done&&request.result==TIKU_MFS_STALE&&!state.opened&&!store.running);
}
/** @brief A cancelled browser drains a borrowed older query before its bridge is freed. */
static void cancel_behind(int queued)
{
    surface_host_t host;surface_config_t config=surface_host_config(&host);
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    surface_media_t media;tiku_storage_provider_t provider=surface_media_init(&media,bridge);
    tiku_resource_caller_t caller={0};strcpy(caller.principal,"surface-local");
    tiku_paged_storage_t *owner=tiku_paged_storage_create(config.allocator,provider,&caller,16,1024);assert(owner);
    tiku_media_request_t older[4];tiku_mfs_object_t info[4];
    for(unsigned i=0;i<4;i++){
        older[i]=(tiku_media_request_t){.op=TIKU_MEDIA_ENUMERATE,.client=99,.permissions=TIKU_MEDIA_READ,.name="",.info=&info[i]};
        assert(!tiku_media_store_submit(&store,&older[i]));
    }
    assert(tiku_paged_storage_list(owner,"/",0,0)==TIKU_RESOURCE_PENDING);
    surface_media_poll(&media);assert(store.running==2&&!media.pending);
    if(queued){
        unsigned i;for(i=0;i<30000 && !(media.pending&&store.running==2);i++)surface_media_poll(&media);
        assert(i<30000 && store.queue[0]->client==99);
    }
    tiku_paged_storage_cancel(owner);drain(&media,owner,0);
    assert(!store.running&&!media.request&&older[0].done);
    tiku_paged_storage_close(owner);assert(tiku_paged_storage_destroy(&owner)&&!host.memory.live);free(bridge);
    while(store.count)assert(tiku_media_store_step(&store)==1);
}
/** @brief Show separate SEEK/LIST starvation, then stable atomic enumeration progress. */
static void epoch_contract(void)
{
    file("a"); file("b"); uint64_t id;
    assert(!tiku_mfs_begin(&fs,"writer",TIKU_MFS_STREAM,0,NULL,&id));
    uint64_t version = tiku_mfs_names_epoch(&fs);
    tiku_mfs_object_t info;
    for (unsigned i = 0; i < 8; ++i) {
        tiku_mfs_cursor_t c = {0};
        assert(!tiku_mfs_list_seek(&fs,"b",&c)); content_sync();
        assert(tiku_mfs_list(&fs,&c,&info) == TIKU_MFS_STALE);
        tiku_media_request_t r = {.op=TIKU_MEDIA_ENUMERATE,.client=9,.permissions=TIKU_MEDIA_READ,
            .name="b",.info=&info,.names_epoch=version};
        assert(!tiku_media_store_submit(&store,&r)); content_sync();
        assert(tiku_media_store_step(&store)==1 && r.done && !r.result);
        assert(!strcmp(info.name,"b") && r.rank==1 && r.names_epoch==version);
    }
    assert(!tiku_mfs_finalize(&fs));
    uint64_t rank, epoch;
    assert(tiku_mfs_enumerate(&fs,"b",0,version,&info,&rank,&epoch)==TIKU_MFS_STALE);
    version = tiku_mfs_names_epoch(&fs);
    assert(!tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used));
    assert(tiku_mfs_names_epoch(&fs)!=version);
    version = tiku_mfs_names_epoch(&fs);
    reset(); assert(tiku_mfs_names_epoch(&fs)!=version);
}
/** @brief Exercise 4096 descendants, bounded skipping, page progress, reads and cancellation. */
/** @brief Complete a neutral bridge request without a synchronous store step. */
static void bridge_drain(void *bridge)
{ for(unsigned n=0;!surface_os_media_poll(bridge);++n)assert(n<30000); }
/** @brief Resolve file IDs, rename/delete and retire cancelled product requests. */
static void bridge_mutations(void)
{
    reset();file("before");void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    surface_media_io_t io={.operation=SURFACE_MEDIA_ENUMERATE};
    assert(surface_os_media_submit(bridge,&io)==SURFACE_MEDIA_OK);bridge_drain(bridge);
    assert(io.result==SURFACE_MEDIA_OK&&io.id&&!strcmp(io.found,"before"));uint64_t id=io.id;
    io=(surface_media_io_t){.operation=SURFACE_MEDIA_RENAME,.id=id};strcpy(io.name,"before");strcpy(io.destination,"after");
    assert(surface_os_media_submit(bridge,&io)==SURFACE_MEDIA_OK);bridge_drain(bridge);assert(io.result==SURFACE_MEDIA_OK);
    assert(surface_os_media_submit(bridge,&io)==SURFACE_MEDIA_OK);bridge_drain(bridge);assert(io.result==SURFACE_MEDIA_STALE);
    io.operation=SURFACE_MEDIA_DELETE;strcpy(io.name,"after");
    assert(surface_os_media_submit(bridge,&io)==SURFACE_MEDIA_OK);bridge_drain(bridge);assert(io.result==SURFACE_MEDIA_OK);
    tiku_mfs_object_t info;assert(tiku_mfs_stat(&fs,"after",&info)==TIKU_MFS_NOENT);
    file("cancel");assert(!tiku_mfs_stat(&fs,"cancel",&info));io.id=info.id;io.epoch=0;strcpy(io.name,"cancel");
    assert(surface_os_media_submit(bridge,&io)==SURFACE_MEDIA_OK);
    for(unsigned n=0;n<12;++n)assert(!surface_os_media_poll(bridge));
    close_refusals=2;assert(!surface_os_media_cancel(bridge));assert(!surface_os_media_cancel(bridge));
    assert(surface_os_media_cancel(bridge));bridge_drain(bridge);assert(io.result==SURFACE_MEDIA_STALE&&!store.count&&!store.running);
    assert(!fs.failed&&!tiku_mfs_stat(&fs,"cancel",&info));free(bridge);
}

#if SURFACE_FILE_JOBS
/** @brief Send one pointer click through the shell's ordinary input queue. */
static void file_tap(surface_t *shell,surface_media_t *media,int x,int y)
{
    tiku_input_event_t e={.type=TIKU_EVENT_POINTER_DOWN,.button=1,.x=x,.y=y};assert(surface_post(shell,&e));
    e.type=TIKU_EVENT_POINTER_UP;assert(surface_post(shell,&e));surface_step(shell);surface_media_poll(media);
}
/** @brief Replace a filename draft using shared field selection and commit input. */
static void file_name(surface_t *shell,const char *name)
{
    tiku_input_event_t e={.type=TIKU_EVENT_KEY_DOWN,.key='a',.modifiers=TIKU_MOD_CMD};assert(surface_post(shell,&e));
    e=(tiku_input_event_t){.type=TIKU_EVENT_KEY_DOWN};assert(strlen(name)<sizeof e.text);strcpy(e.text,name);assert(surface_post(shell,&e));
    e=(tiku_input_event_t){.type=TIKU_EVENT_KEY_DOWN,.key=TIKU_KEY_RETURN};assert(surface_post(shell,&e));surface_step(shell);
}
/** @brief Capture an accepted candidate frame when an evidence directory is supplied. */
static void file_capture(surface_t *shell,surface_host_t *host,const char *name)
{
    const char *directory=getenv("SURFACE_MEDIA_CAPTURE_DIR");if(!directory)return;
    char path[512];assert(snprintf(path,sizeof path,"%s/%s.ppm",directory,name)<(int)sizeof path);
    assert(surface_render(shell,surface_host_presenter(host))==TIKU_PRESENT_OK);
    assert(surface_host_capture(host,path));host->now=host->frame_due;surface_host_poll(host);
}
/** @brief Run rename and confirmed deletion through visible browser controls. */
static void tracker_actions(void)
{
    reset();file("before");surface_host_t host;surface_config_t cfg=surface_host_config(&host);
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);surface_media_t media;
    cfg.storage=surface_media_init(&media,bridge);
    cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *shell=surface_create(&cfg);assert(shell&&surface_launch(shell,SURFACE_BROWSER));settle(shell,&media);
    file_tap(shell,&media,70,130);settle(shell,&media);surface_view_t v;surface_view(shell,&v);assert(v.reading&&v.file_actions);file_capture(shell,&host,"preview");
    file_tap(shell,&media,460,410);surface_view(shell,&v);assert(v.text_entry&&!strcmp(v.text_value,"/before"));file_capture(shell,&host,"rename");
    assert(!surface_launch(shell,SURFACE_HOME));file_name(shell,"/../bad");surface_view(shell,&v);assert(v.text_entry&&v.text_error);
    file_name(shell,"/after");settle(shell,&media);surface_view(shell,&v);
    assert(!v.reading&&!v.text_entry&&v.row_count==1&&!strcmp(v.rows[0],"after"));
    file_tap(shell,&media,70,130);settle(shell,&media);file_tap(shell,&media,460,410);file_name(shell,"/stop");
    assert(surface_close(shell)==SURFACE_CLOSE_ACCEPTED);settle(shell,&media);surface_view(shell,&v);assert(!v.retiring);
    tiku_mfs_object_t kept;assert(!tiku_mfs_stat(&fs,"after",&kept));
    assert(surface_launch(shell,SURFACE_BROWSER));settle(shell,&media);
    file_tap(shell,&media,70,130);settle(shell,&media);file_tap(shell,&media,660,410);surface_view(shell,&v);
    assert(v.close_confirmation==2&&!surface_launch(shell,SURFACE_HOME)&&surface_close(shell)==SURFACE_CLOSE_CONFIRM);file_capture(shell,&host,"delete");
    assert(surface_resolve_close(shell,0));tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"after",&o));
    file_tap(shell,&media,660,410);assert(surface_resolve_close(shell,1));settle(shell,&media);surface_view(shell,&v);
    assert(v.foreground==SURFACE_BROWSER&&!v.reading&&!v.row_count&&!v.close_confirmation);
    assert(tiku_mfs_stat(&fs,"after",&o)==TIKU_MFS_NOENT);
    surface_stop(shell);for(unsigned i=0;i<30000&&!surface_destroy(&shell);++i){surface_step(shell);surface_media_poll(&media);}
    assert(!shell&&!host.memory.live&&!store.count&&!store.running);free(bridge);
}
/** @brief Exercise shared SDK writes, destination conflicts and stale resolved identities. */
static void sdk_mutations(void)
{
    reset();file("a");file("b");surface_host_t host;surface_config_t cfg=surface_host_config(&host);
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);surface_media_t media;
    tiku_storage_provider_t provider=surface_media_init(&media,bridge);
    tiku_resource_caller_t caller={.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_WRITE|TIKU_RESOURCE_READ};
    tiku_paged_storage_t *owner=tiku_paged_storage_create(cfg.allocator,provider,&caller,16,1024);assert(owner);
    assert(tiku_paged_storage_list(owner,"/",0,0)==TIKU_RESOURCE_PENDING);drain(&media,owner,0);
    size_t count;uint64_t epoch;int eof;const tiku_storage_request_t *r=tiku_paged_storage_result(owner,&count,&epoch,&eof);
    assert(r&&count==2);uint64_t node=r->entries[0].node_id;
    assert(tiku_paged_storage_rename(owner,"/a",epoch,node,"/c")==TIKU_RESOURCE_PENDING);drain(&media,owner,0);
    assert(tiku_paged_storage_status(owner)==TIKU_RESOURCE_OK);tiku_mfs_object_t o;
    assert(!tiku_mfs_stat(&fs,"c",&o)&&tiku_mfs_stat(&fs,"a",&o)==TIKU_MFS_NOENT);
    assert(tiku_paged_storage_delete(owner,"/c",epoch,node)==TIKU_RESOURCE_PENDING);drain(&media,owner,0);
    assert(tiku_paged_storage_status(owner)==TIKU_RESOURCE_STALE_CURSOR);
    assert(tiku_paged_storage_list(owner,"/",0,0)==TIKU_RESOURCE_PENDING);drain(&media,owner,0);
    r=tiku_paged_storage_result(owner,&count,&epoch,&eof);assert(r&&count==2);node=r->entries[1].node_id;
    assert(tiku_paged_storage_rename(owner,"/c",epoch,node,"/b")==TIKU_RESOURCE_PENDING);drain(&media,owner,0);
    assert(tiku_paged_storage_status(owner)==TIKU_RESOURCE_BUSY);
    assert(tiku_paged_storage_rename(owner,"/c",epoch,node,"/../bad")==TIKU_RESOURCE_DENIED);
    assert(tiku_paged_storage_delete(owner,"/c",epoch,node)==TIKU_RESOURCE_PENDING);
    while(media.phase!=5)surface_media_poll(&media);
    file("changed");drain(&media,owner,0);assert(tiku_paged_storage_status(owner)==TIKU_RESOURCE_STALE_CURSOR);
    assert(!tiku_mfs_stat(&fs,"c",&o));
    assert(tiku_paged_storage_list(owner,"/",0,0)==TIKU_RESOURCE_PENDING);drain(&media,owner,0);
    r=tiku_paged_storage_result(owner,&count,&epoch,&eof);assert(r&&count==3);node=r->entries[1].node_id;
    assert(!strcmp(r->entries[1].path,"/c"));
    assert(tiku_paged_storage_delete(owner,"/c",epoch,node)==TIKU_RESOURCE_PENDING);drain(&media,owner,0);
    assert(tiku_paged_storage_status(owner)==TIKU_RESOURCE_OK&&tiku_mfs_stat(&fs,"c",&o)==TIKU_MFS_NOENT);
    tiku_paged_storage_close(owner);assert(tiku_paged_storage_destroy(&owner));caller.scopes=TIKU_RESOURCE_READ;
    owner=tiku_paged_storage_create(cfg.allocator,provider,&caller,1,1);assert(owner);
    assert(tiku_paged_storage_delete(owner,"/b",fs.names_epoch,1)==TIKU_RESOURCE_DENIED);
    tiku_paged_storage_close(owner);assert(tiku_paged_storage_destroy(&owner));assert(!host.memory.live);free(bridge);
}
#endif

#if SURFACE_CONTENT_JOBS
/** @brief Create and replace bytes through the SDK, preserving guards and leases. */
static void sdk_contents(void)
{
    reset();surface_host_t host;surface_config_t cfg=surface_host_config(&host);
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);surface_media_t media;
    tiku_storage_provider_t provider=surface_media_init(&media,bridge);
    tiku_resource_caller_t caller={.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_WRITE|TIKU_RESOURCE_READ};
    tiku_paged_storage_t *owner=tiku_paged_storage_create(cfg.allocator,provider,&caller,16,1024);assert(owner);
    assert(tiku_paged_storage_list(owner,"/",0,0)==TIKU_RESOURCE_PENDING);drain(&media,owner,0);
    size_t count;uint64_t epoch;int eof;assert(tiku_paged_storage_result(owner,&count,&epoch,&eof)&&!count&&epoch);
    static unsigned char data[65536],readback[65536];for(unsigned i=0;i<sizeof data;++i)data[i]=(unsigned char)(i*29+1);
    assert(tiku_paged_storage_replace(owner,"/new",epoch,0,data,65537)==TIKU_RESOURCE_LIMIT);
    assert(tiku_paged_storage_replace(owner,"/new",epoch,0,data,sizeof data)==TIKU_RESOURCE_PENDING);drain(&media,owner,0);
    assert(tiku_paged_storage_result(owner,&count,&epoch,&eof)&&count==sizeof data&&eof);
    uint64_t reader;assert(!tiku_mfs_open_read(&fs,"new",&reader));
    assert(tiku_paged_storage_replace(owner,"/new",epoch,0,"x",1)==TIKU_RESOURCE_PENDING);drain(&media,owner,0);
    assert(tiku_paged_storage_status(owner)==TIKU_RESOURCE_BUSY);
    assert(tiku_paged_storage_replace(owner,"/new",epoch,1,"abc",3)==TIKU_RESOURCE_PENDING);drain(&media,owner,0);
    assert(tiku_paged_storage_result(owner,&count,&epoch,&eof)&&count==3);
    assert(tiku_mfs_read_at(&fs,reader,0,readback,sizeof readback)==sizeof readback&&!memcmp(data,readback,sizeof data));assert(!tiku_mfs_close_read(&fs,reader));
    assert(tiku_paged_storage_replace(owner,"/new",epoch,1,"late",4)==TIKU_RESOURCE_PENDING);
    for(unsigned n=0;media.phase!=5 || media.pending;++n){assert(n<30000);surface_media_poll(&media);}
    file("other");drain(&media,owner,0);assert(tiku_paged_storage_status(owner)==TIKU_RESOURCE_STALE_CURSOR);
    assert(tiku_paged_storage_replace(owner,"/new",fs.names_epoch,1,data,sizeof data)==TIKU_RESOURCE_PENDING);
    for(unsigned i=0;i<40;++i)surface_media_poll(&media);
    close_refusals=3;tiku_paged_storage_cancel(owner);assert(tiku_paged_storage_pending(owner));
    tiku_paged_storage_close(owner);assert(!tiku_paged_storage_destroy(&owner));drain(&media,owner,0);
    assert(tiku_paged_storage_destroy(&owner));close_refusals=0;
    tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"new",&o)&&o.length==3);
    caller.scopes=TIKU_RESOURCE_READ;owner=tiku_paged_storage_create(cfg.allocator,provider,&caller,1,1);assert(owner);
    assert(tiku_paged_storage_replace(owner,"/deny",fs.names_epoch,0,"x",1)==TIKU_RESOURCE_DENIED);
    tiku_paged_storage_close(owner);assert(tiku_paged_storage_destroy(&owner)&&!host.memory.live);free(bridge);
}
/** @brief Create an empty file from the visible folder and reject collisions. */
static void tracker_create(void)
{
    reset();surface_host_t host;surface_config_t cfg=surface_host_config(&host);
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);surface_media_t media;
    cfg.storage=surface_media_init(&media,bridge);
    cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *shell=surface_create(&cfg);assert(shell&&surface_launch(shell,SURFACE_BROWSER));settle(shell,&media);
    surface_view_t v;surface_view(shell,&v);assert(v.file_create&&!v.row_count);
    file_tap(shell,&media,300,410);surface_view(shell,&v);assert(v.text_entry&&v.file_dialog==2&&!strcmp(v.text_value,"/new"));
    file_capture(shell,&host,"new-file");file_name(shell,"/../bad");surface_view(shell,&v);assert(v.text_entry&&v.text_error);
    file_name(shell,"/note");settle(shell,&media);surface_view(shell,&v);assert(v.row_count==1&&!strcmp(v.rows[0],"note")&&v.file_create);
    tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"note",&o)&&!o.length);uint64_t id=o.id;
    file_tap(shell,&media,300,410);file_name(shell,"/note");settle(shell,&media);surface_view(shell,&v);assert(v.status==TIKU_RESOURCE_BUSY);
    assert(!tiku_mfs_stat(&fs,"note",&o)&&o.id==id);
    file_tap(shell,&media,70,410);settle(shell,&media);file_tap(shell,&media,300,410);file_name(shell,"/stop");
    assert(surface_close(shell)==SURFACE_CLOSE_ACCEPTED);settle(shell,&media);assert(tiku_mfs_stat(&fs,"stop",&o)==TIKU_MFS_NOENT);
    surface_stop(shell);for(unsigned i=0;i<30000&&!surface_destroy(&shell);++i){surface_step(shell);surface_media_poll(&media);}
    assert(!shell&&!host.memory.live&&!store.count&&!store.running);free(bridge);
}
/** @brief Copy binary content, refuse oversized inputs and preserve cancellation ownership. */
static void tracker_copy(void)
{
    static unsigned char payload[65537],out[65536];
    for(unsigned i=0;i<sizeof payload;++i)payload[i]=(unsigned char)(i*19+i/251);
    const unsigned sizes[]={0,1,1024,4097,65536,65537};
    for(unsigned scenario=0;scenario<13;++scenario){
        unsigned bytes=scenario<6?sizes[scenario]:4097;
        reset();uint64_t id;assert(!tiku_mfs_begin(&fs,"source",TIKU_MFS_STREAM,0,NULL,&id));
        if(bytes)assert(!tiku_mfs_append(&fs,payload,bytes>65536?65536:bytes));
        if(bytes>65536)assert(!tiku_mfs_append(&fs,payload+65536,bytes-65536));
        assert(!tiku_mfs_finalize(&fs));
        if(scenario==12)file("target");
        surface_host_t host;surface_config_t cfg=surface_host_config(&host);
        void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);surface_media_t media;
        cfg.storage=surface_media_init(&media,bridge);
        cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
        surface_t *shell=surface_create(&cfg);assert(shell&&surface_launch(shell,SURFACE_BROWSER));settle(shell,&media);
        file_tap(shell,&media,70,130);settle(shell,&media);surface_view_t v;surface_view(shell,&v);assert(v.reading&&v.file_actions);
        size_t before=host.memory.live;
        file_tap(shell,&media,300,410);surface_view(shell,&v);assert(v.text_entry&&v.file_dialog==3);
        if(!scenario)file_capture(shell,&host,"copy-file");
        file_name(shell,"/source");surface_view(shell,&v);assert(v.text_entry&&v.text_error);
        if(scenario==6)host.memory.fail_at=host.memory.calls+1;
        file_name(shell,"/target");
        if(scenario==6){
            surface_view(shell,&v);assert(v.text_entry&&v.text_error&&v.status==TIKU_RESOURCE_LIMIT);
            assert(host.memory.live==before);host.memory.fail_at=0;file_name(shell,"/target");
        }
        if(scenario==7||scenario==8||scenario==9||scenario==10){
            unsigned n;
            for(n=0;n<30000;++n){
                surface_step(shell);surface_media_poll(&media);
                if(scenario==7 && media.request && media.request->operation==TIKU_STORAGE_READ && media.request->offset)break;
                if((scenario==8||scenario==9) && shell->active.state->file_job==7&&!media.request)break;
                if(scenario==10 && media.request && media.request->operation==TIKU_STORAGE_REPLACE && store.running==4)break;
            }
            assert(n<30000);surface_view(shell,&v);assert(v.copy_bytes>0&&v.copy_bytes<=bytes);if(scenario!=7)assert(v.copy_bytes==bytes);
            if(scenario==8)file_capture(shell,&host,"copy-publish");
            if(scenario==7||scenario==8)file("changed");
            if(scenario==9)file("target");
            if(scenario==10){
                assert(host.memory.live==before+1);close_refusals=3;
                assert(surface_close(shell)==SURFACE_CLOSE_ACCEPTED);
                surface_view(shell,&v);assert(v.retiring&&host.memory.live>=before+1);
            }
        }
        if(scenario==11){
            assert(shell->active.state->copy_buffer.data);
            assert(surface_app_back(shell->active.state));
        }
        settle(shell,&media);surface_view(shell,&v);
        tiku_mfs_object_t object;
        if(scenario==5)assert(v.status==TIKU_RESOURCE_LIMIT&&tiku_mfs_stat(&fs,"target",&object)==TIKU_MFS_NOENT);
        else if(scenario==7||scenario==8||scenario==9){
            assert(v.status==TIKU_RESOURCE_STALE_CURSOR);
            if(scenario==9)assert(!tiku_mfs_stat(&fs,"target",&object)&&object.length==0);
            else assert(tiku_mfs_stat(&fs,"target",&object)==TIKU_MFS_NOENT);
        } else if(scenario==12){
            assert(v.status==TIKU_RESOURCE_BUSY&&!tiku_mfs_stat(&fs,"target",&object)&&!object.length);
        } else if(scenario>=10){
            if(fs.failed)assert(!tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used));
            assert(tiku_mfs_stat(&fs,"target",&object)==TIKU_MFS_NOENT);
        } else {
            assert(v.status==TIKU_RESOURCE_OK&&!v.reading&&!v.file_job&&v.row_count==2);
            assert(!tiku_mfs_stat(&fs,"target",&object)&&object.length==bytes&&object.id!=id);
            uint64_t reader;assert(!tiku_mfs_open_read(&fs,"target",&reader));
            if(bytes)assert(tiku_mfs_read_at(&fs,reader,0,out,bytes)==(int)bytes&&!memcmp(payload,out,bytes));
            assert(!tiku_mfs_close_read(&fs,reader));
        }
        assert(!tiku_mfs_stat(&fs,"source",&object)&&object.id==id&&object.length==bytes);
        if(scenario!=10)assert(host.memory.live==before);
        surface_stop(shell);for(unsigned i=0;i<30000&&!surface_destroy(&shell);++i){surface_step(shell);surface_media_poll(&media);}
        assert(!shell&&!host.memory.live&&!store.count&&!store.running);free(bridge);
    }
}

#endif

int main(void)
{
    disk = calloc(1,DISK_BYTES); assert(disk);
    block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};
    reset(); empty_catalog(); query_contract(); reset(); queue_contract(); cancel_behind(0); cancel_behind(1); reset(); epoch_contract();
    char name[64];
    for (unsigned i=0;i<4096;++i) { snprintf(name,sizeof name,"a/%04u",i); file(name); }
    for (unsigned i=0;i<40;++i) { snprintf(name,sizeof name,"f%04u",i); file(name); }
    uint64_t id; assert(!tiku_mfs_begin(&fs,"writer",TIKU_MFS_STREAM,0,NULL,&id));
    surface_host_t host; surface_config_t config=surface_host_config(&host);
    surface_media_t media; void *bridge=calloc(1,surface_os_media_size()); assert(bridge);
    surface_os_media_init(bridge);
    tiku_storage_provider_t provider=surface_media_init(&media,bridge);
    tiku_resource_caller_t caller={0}; strcpy(caller.principal,"surface-local");
    tiku_paged_storage_t *owner=tiku_paged_storage_create(config.allocator,provider,&caller,16,1024); assert(owner);
    tiku_media_request_t older[4]; tiku_mfs_object_t older_info[4];
    for (unsigned i=0;i<4;++i) {
        older[i]=(tiku_media_request_t){.op=TIKU_MEDIA_ENUMERATE,.client=33,.permissions=TIKU_MEDIA_READ,.name="",.info=&older_info[i]};
        assert(!tiku_media_store_submit(&store,&older[i]));
    }
    assert(tiku_paged_storage_list(owner,"/",0,0)==TIKU_RESOURCE_PENDING);
    reads=0; drain(&media,owner,0); assert(reads<2500);
    for (unsigned i=0;i<4;++i) assert(older[i].done && !older[i].result);
    size_t count; uint64_t epoch; int eof;
    const tiku_storage_request_t *r=tiku_paged_storage_result(owner,&count,&epoch,&eof);
    assert(r && count==16 && !eof && !strcmp(r->entries[0].path,"/a"));
    assert(r->entries[0].kind==TIKU_RESOURCE_DIRECTORY && r->entries[0].node_id==1);
    assert(!strcmp(r->entries[1].path,"/f0000") && r->entries[1].node_id==4097);
    uint64_t after=r->entries[15].node_id;
    assert(tiku_paged_storage_list(owner,"/",epoch,after)==TIKU_RESOURCE_PENDING);
    drain(&media,owner,1);
    r=tiku_paged_storage_result(owner,&count,&epoch,&eof);
    assert(r && count==16 && !strcmp(r->entries[0].path,"/f0015"));
    after=r->entries[15].node_id;
    assert(tiku_paged_storage_list(owner,"/",epoch,after)==TIKU_RESOURCE_PENDING); drain(&media,owner,1);
    r=tiku_paged_storage_result(owner,&count,&epoch,&eof);
    assert(r && count==10 && eof && !strcmp(r->entries[9].path,"/writer"));
    uint64_t writer_node=r->entries[9].node_id;
    assert(tiku_paged_storage_read(owner,"/writer",epoch,writer_node,0)==TIKU_RESOURCE_PENDING);
    drain(&media,owner,0); r=tiku_paged_storage_result(owner,&count,&epoch,&eof);
    assert(r && count && r->bytes[0]=='x');
    for (unsigned i=0;i<TIKU_MFS_READERS;++i) assert(!store.readers[i].token);
    assert(tiku_paged_storage_read(owner,"/writer",epoch,writer_node+1,0)==TIKU_RESOURCE_PENDING);
    drain(&media,owner,0); assert(tiku_paged_storage_status(owner)==TIKU_RESOURCE_STALE_CURSOR);
    assert(tiku_paged_storage_list(owner,"/a",0,0)==TIKU_RESOURCE_PENDING);
    drain(&media,owner,0); r=tiku_paged_storage_result(owner,&count,&epoch,&eof); assert(r&&count==16&&!eof);
    assert(!strcmp(r->entries[0].path,"/a/0000"));
    unsigned seen = 0;
    for (;;) {
        for (size_t i=0;i<count;++i) {
            snprintf(name,sizeof name,"/a/%04u",seen++);
            assert(!strcmp(r->entries[i].path,name) && r->entries[i].node_id==seen);
        }
        if (eof) break;
        after=r->entries[count-1].node_id;
        assert(tiku_paged_storage_list(owner,"/a",epoch,after)==TIKU_RESOURCE_PENDING);
        reads=0; drain(&media,owner,0); assert(reads<2500);
        r=tiku_paged_storage_result(owner,&count,&epoch,&eof); assert(r);
    }
    assert(seen==4096);
    for (unsigned stop=0;stop<7;++stop) {
        assert(tiku_paged_storage_read(owner,"/writer",epoch,writer_node,0)==TIKU_RESOURCE_PENDING);
        for (unsigned turn=0;turn<stop;++turn) surface_media_poll(&media);
        tiku_paged_storage_cancel(owner); drain(&media,owner,0);
        assert(!media.request && !store.count);
        for (unsigned i=0;i<TIKU_MFS_READERS;++i) assert(!store.readers[i].token);
    }
    assert(tiku_paged_storage_read(owner,"/writer",epoch,writer_node,0)==TIKU_RESOURCE_PENDING);
    opened_reader(&media);
    tiku_paged_storage_cancel(owner); drain(&media,owner,0);
    for (unsigned i=0;i<TIKU_MFS_READERS;++i) assert(!store.readers[i].token);
    assert(tiku_paged_storage_read(owner,"/writer",epoch,writer_node,0)==TIKU_RESOURCE_PENDING);
    opened_reader(&media);
    offline=1;
    surface_media_poll(&media); surface_media_poll(&media);
    store.failed=1;
    drain(&media,owner,0); offline=0;
    assert(tiku_paged_storage_status(owner)==TIKU_RESOURCE_UNAVAILABLE);
    for (unsigned i=0;i<TIKU_MFS_READERS;++i) assert(!store.readers[i].token);
    fs.failed=store.failed=0;
    assert(tiku_paged_storage_list(owner,"/",0,0)==TIKU_RESOURCE_PENDING);
    surface_media_poll(&media); tiku_paged_storage_cancel(owner); drain(&media,owner,0);
    assert(!tiku_paged_storage_result(owner,&count,&epoch,&eof));
    assert(tiku_paged_storage_list(owner,"/",0,0)==TIKU_RESOURCE_PENDING);
    surface_media_poll(&media); assert(!tiku_mfs_finalize(&fs)); drain(&media,owner,0);
    assert(tiku_paged_storage_status(owner)==TIKU_RESOURCE_OK);
    assert(tiku_paged_storage_list(owner,"/a",0,0)==TIKU_RESOURCE_PENDING);
    surface_media_poll(&media); surface_media_poll(&media);
    file("new-visible-name"); drain(&media,owner,0);
    assert(tiku_paged_storage_status(owner)==TIKU_RESOURCE_STALE_CURSOR);
    assert(tiku_paged_storage_list(owner,"/a",0,0)==TIKU_RESOURCE_PENDING);
    surface_media_poll(&media); surface_media_poll(&media);
    assert(!tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used));
    drain(&media,owner,0);
    assert(tiku_paged_storage_status(owner)==TIKU_RESOURCE_STALE_CURSOR);
    tiku_paged_storage_close(owner); assert(tiku_paged_storage_destroy(&owner));
    char longest[64]; memset(longest, 'z', 63); longest[63]=0; file(longest);
    file("\303\251-file");
    uint64_t raw_id;
    const unsigned char binary[] = {0,255,65,10};
    assert(!tiku_mfs_begin(&fs,"0-binary",TIKU_MFS_STREAM,0,NULL,&raw_id));
    assert(!tiku_mfs_append(&fs,binary,sizeof binary) && !tiku_mfs_finalize(&fs));
    surface_os_media_init(bridge);
    config.storage=surface_media_init(&media,bridge); config.caller=caller;
    surface_t *shell=surface_create(&config); assert(shell);
    assert(surface_launch(shell,SURFACE_BROWSER));
    settle(shell,&media);
    surface_view_t view; surface_view(shell,&view);
    assert(view.row_count && !strcmp(view.rows[0],"0-binary"));
    tiku_input_event_t tap={.type=TIKU_EVENT_POINTER_DOWN,.button=1,.x=70,.y=130};
    assert(surface_post(shell,&tap)); tap.type=TIKU_EVENT_POINTER_UP; assert(surface_post(shell,&tap));
    settle(shell,&media);
    surface_view(shell,&view); assert(view.reading && view.preview_hex && view.preview_bytes==4 && !strcmp(view.preview,"00 FF 41 0A "));
    for (unsigned cycle=0;cycle<100;++cycle) {
        assert(surface_launch(shell,SURFACE_BROWSER)); surface_step(shell); surface_media_poll(&media);
        assert(surface_launch(shell,SURFACE_HOME));
        settle(shell,&media);
        surface_view(shell,&view); assert(!view.retiring && view.foreground==SURFACE_HOME);
        for (unsigned i=0;i<TIKU_MFS_READERS;++i) assert(!store.readers[i].token);
    }
    surface_stop(shell);
    for (unsigned i=0;i<8 && !surface_destroy(&shell);++i) { surface_media_poll(&media); surface_step(shell); }
    assert(!shell);
    surface_os_media_init(bridge); provider=surface_media_init(&media,bridge);
    owner=tiku_paged_storage_create(config.allocator,provider,&caller,16,1024); assert(owner);
    char absolute[65]; absolute[0]='/'; memcpy(absolute+1,longest,64);
    tiku_mfs_object_t info; uint64_t rank;
    assert(!tiku_mfs_enumerate(&fs,longest,0,0,&info,&rank,&epoch));
    assert(tiku_paged_storage_read(owner,absolute,epoch,rank+1,0)==TIKU_RESOURCE_PENDING);
    drain(&media,owner,0); r=tiku_paged_storage_result(owner,&count,&epoch,&eof); assert(r && !count && eof);
    tiku_paged_storage_close(owner); assert(tiku_paged_storage_destroy(&owner));
    caller.principal[0]='x';
    owner=tiku_paged_storage_create(config.allocator,provider,&caller,16,1024); assert(owner);
    assert(tiku_paged_storage_list(owner,"/",0,0)==TIKU_RESOURCE_DENIED);
    tiku_paged_storage_close(owner); assert(tiku_paged_storage_destroy(&owner));
    assert(!host.memory.live);
    free(bridge); bridge_mutations();
#if SURFACE_FILE_JOBS
    sdk_mutations();tracker_actions();
#endif
#if SURFACE_CONTENT_JOBS
    sdk_contents();tracker_create();tracker_copy();
#endif
    free(disk);
    printf("media: 4096 descendants, atomic enumeration, sync progress, reads and cancellation pass; max turns=%u, state=%zu\n",maximum_turns,sizeof(read_state_t));
    return 0;
}
