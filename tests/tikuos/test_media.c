/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media.c - queued browser pagination under interleaved media writes.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "media.h"
#include "host.h"
#include <services/storage/tiku_media_card.h>
#include <services/storage/tiku_media_async.h>
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
    free(bridge); free(disk);
    printf("media: 4096 descendants, atomic enumeration, sync progress, reads and cancellation pass; max turns=%u, state=%zu\n",maximum_turns,sizeof(read_state_t));
    return 0;
}
