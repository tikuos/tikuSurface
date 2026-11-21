/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_hex.c - binary publication, stale identity, draft lifetime and touch input.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main media_fixture_main
#include "tiku_event.h"
#include "test_media.c"
#undef main
static tiku_storage_provider_t backing;
static unsigned file_reads;
/** @brief Count admitted read slices while retaining the real queued media provider. */
static tiku_resource_error_t counted_submit(void *ctx,tiku_paged_storage_t *owner,const tiku_storage_request_t *r)
{file_reads+=r->operation==TIKU_STORAGE_READ;return backing.ops->submit(backing.context,owner,r);}
/** @brief Delegate cancellation without shortening the request lifetime. */
static void counted_cancel(void *ctx,tiku_paged_storage_t *owner,uint64_t token)
{backing.ops->cancel(backing.context,owner,token);}
static const tiku_storage_provider_ops_t counted_ops={counted_submit,counted_cancel};
/** @brief Create exact binary source bytes, including an empty document. */
static void document(const char *name,const unsigned char *bytes,size_t length)
{
    uint64_t id;assert(!tiku_mfs_begin(&fs,name,TIKU_MFS_STREAM,0,NULL,&id));
    if(length)assert(!tiku_mfs_append(&fs,bytes,length));
    assert(!tiku_mfs_finalize(&fs));
}
/** @brief Independently compare persisted bytes through a retained reader. */
static void stored(const char *name,const unsigned char *bytes,size_t length)
{
    tiku_mfs_object_t object;uint64_t reader;unsigned char out[8192];
    assert(!tiku_mfs_stat(&fs,name,&object)&&object.length==length);
    assert(!tiku_mfs_open_read(&fs,name,&reader));
    assert(tiku_mfs_read_at(&fs,reader,0,out,sizeof out)==(int)length&&!memcmp(out,bytes,length));
    assert(!tiku_mfs_close_read(&fs,reader));
}
/** @brief Copy presentation state after queued media and application turns. */
static surface_view_t view(surface_t *s,surface_media_t *media)
{settle(s,media);surface_view_t v;surface_view(s,&v);return v;}
/** @brief Open one text control and deliver its accepted draft through input events. */
static void input(surface_t *s,int action,const char *text)
{
    surface_hex_action(s->active.state,action);surface_view_t v;surface_view(s,&v);assert(v.text_entry);
    tiku_input_event_t event={.type=TIKU_EVENT_KEY_DOWN,.key='a',.modifiers=TIKU_MOD_CMD};assert(surface_post(s,&event));surface_step(s);
    while(*text){event=(tiku_input_event_t){.type=TIKU_EVENT_KEY_DOWN};size_t n=strlen(text);if(n>7)n=7;memcpy(event.text,text,n);assert(surface_post(s,&event));surface_step(s);text+=n;}
    event=(tiku_input_event_t){.type=TIKU_EVENT_KEY_DOWN,.key=TIKU_KEY_RETURN};assert(surface_post(s,&event));surface_step(s);
    surface_view(s,&v);assert(!v.text_entry);
}
/** @brief Compare whole-frame painting with clipped row presentation. */
static void painting(surface_t *s)
{
    static uint32_t whole[800*480],sliced[800*480];surface_view_t v;surface_view(s,&v);
    tiku_surface_t a={.px=whole,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=sliced;
    surface_paint(&a,&v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}
    assert(!memcmp(whole,sliced,sizeof whole));
}
int main(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};
    reset();static unsigned char bytes[8193];for(unsigned i=0;i<sizeof bytes;++i)bytes[i]=(unsigned char)i;
    document("binary",bytes,8192);
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    cfg.storage=surface_media_init(&media,bridge);backing=cfg.storage;cfg.storage.ops=&counted_ops;cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));settle(s,&media);
    file_tap(s,&media,70,130);settle(s,&media);file_tap(s,&media,670,360);
    surface_view_t v=view(s,&media);assert(v.foreground==SURFACE_HEX_APP&&v.byte_size==8192&&!v.byte_dirty&&!strcmp(v.byte_path,"/binary"));
    assert(!memcmp(v.byte_cells,bytes,128));painting(s);file_capture(s,&host,"hex-binary");
    file_tap(s,&media,146,132);v=view(s,&media);assert(v.byte_cursor==1);
    input(s,ACTION_BYTE_EDIT,"FE");v=view(s,&media);assert(v.byte_dirty&&v.byte_cells[1]==254);
    surface_hex_action(s->active.state,ACTION_BYTE_UNDO);v=view(s,&media);assert(!v.byte_dirty&&v.byte_cells[1]==1);
    surface_hex_action(s->active.state,ACTION_BYTE_REDO);v=view(s,&media);assert(v.byte_dirty);
    assert(!surface_hex_open(s->active.state,"/other",0,0));
    assert(surface_close(s)==SURFACE_CLOSE_CONFIRM);assert(surface_resolve_close(s,0));
    assert(surface_launch(s,SURFACE_HOME));surface_step(s);file_capture(s,&host,"hex-launcher");file_tap(s,&media,300,365);v=view(s,&media);assert(v.foreground==SURFACE_HEX_APP&&v.byte_dirty);
    surface_hex_action(s->active.state,ACTION_BYTE_SAVE);v=view(s,&media);bytes[1]=254;
    assert(!v.byte_dirty&&v.status==TIKU_RESOURCE_OK);stored("binary",bytes,8192);
    surface_hex_action(s->active.state,ACTION_BYTE_UNDO);v=view(s,&media);assert(v.byte_dirty&&v.byte_cells[1]==1);
    surface_hex_action(s->active.state,ACTION_BYTE_REDO);v=view(s,&media);assert(!v.byte_dirty);
    surface_hex_action(s->active.state,ACTION_BYTE_UNDO);surface_hex_action(s->active.state,ACTION_BYTE_CELL+2);
    input(s,ACTION_BYTE_EDIT,"12");v=view(s,&media);assert(v.byte_dirty);
    surface_hex_action(s->active.state,ACTION_BYTE_UNDO);surface_hex_action(s->active.state,ACTION_BYTE_CELL+1);
    input(s,ACTION_BYTE_EDIT,"FE");v=view(s,&media);assert(!v.byte_dirty);
    input(s,ACTION_BYTE_GOTO,"0x1fff");v=view(s,&media);assert(v.byte_cursor==8191&&v.byte_first==8064);
    input(s,ACTION_BYTE_EDIT,"00");bytes[8191]=0;
    input(s,ACTION_BYTE_SAVE_AS,"/copy");v=view(s,&media);assert(!v.byte_dirty&&!strcmp(v.byte_path,"/copy"));stored("copy",bytes,8192);
    input(s,ACTION_BYTE_GOTO,"0");input(s,ACTION_BYTE_FIND,"FE FF");
    for(unsigned i=0;i<140;++i)surface_step(s);
    v=view(s,&media);assert(v.byte_cursor==254&&v.byte_search==2);
    surface_hex_action(s->active.state,ACTION_BYTE_COPY);assert(!strcmp(s->clipboard,"FE"));
    input(s,ACTION_BYTE_EDIT,"12");document("foreign",bytes,1);
    surface_hex_action(s->active.state,ACTION_BYTE_SAVE);v=view(s,&media);assert(v.byte_dirty&&v.status==TIKU_RESOURCE_STALE_CURSOR);stored("copy",bytes,8192);
    input(s,ACTION_BYTE_SAVE_AS,"/binary");v=view(s,&media);assert(v.byte_dirty&&v.status==TIKU_RESOURCE_BUSY);
    input(s,ACTION_BYTE_SAVE_AS,"/saved");v=view(s,&media);bytes[254]=0x12;stored("saved",bytes,8192);assert(!v.byte_dirty);
    document("large",bytes,8193);file_reads=0;assert(surface_hex_open(s->active.state,"/large",0,0));v=view(s,&media);
    assert(file_reads==1&&v.status==TIKU_RESOURCE_LIMIT&&!strcmp(v.byte_path,"/saved")&&v.byte_size==8192);
    assert(surface_hex_open(s->active.state,"/missing",0,0));v=view(s,&media);assert(v.status==TIKU_RESOURCE_NOT_FOUND&&!strcmp(v.byte_path,"/saved"));
    document("empty",bytes,0);assert(surface_hex_open(s->active.state,"/empty",0,0));v=view(s,&media);assert(!v.byte_size&&v.byte_loaded);
    input(s,ACTION_BYTE_SAVE_AS,"/empty-copy");v=view(s,&media);assert(!v.byte_dirty&&v.status==TIKU_RESOURCE_OK);stored("empty-copy",bytes,0);
    assert(surface_hex_open(s->active.state,"/saved",0,0));v=view(s,&media);assert(v.byte_size==8192);
    input(s,ACTION_BYTE_EDIT,"34");input(s,ACTION_BYTE_SAVE_AS,"/cancelled");
    unsigned n;for(n=0;n<30000;++n){surface_step(s);surface_media_poll(&media);if(media.request&&media.request->operation==TIKU_STORAGE_REPLACE&&store.running==4)break;}
    assert(n<30000);surface_hex_action(s->active.state,ACTION_BYTE_CANCEL);v=view(s,&media);assert(v.byte_dirty&&!strcmp(v.byte_path,"/saved"));
    if(fs.failed)assert(!tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used));
    stored("saved",bytes,8192);
    assert(surface_close(s)==SURFACE_CLOSE_CONFIRM);assert(surface_resolve_close(s,1));settle(s,&media);
    assert(surface_launch(s,SURFACE_HEX_APP));surface_step(s);assert(surface_hex_open(s->active.state,"/saved",0,0));surface_step(s);
    close_refusals=3;assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);settle(s,&media);
    surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(&media);}assert(!s&&!host.memory.live);
    cfg.storage.capabilities&=~TIKU_RESOURCE_FEATURE_CHANGE;cfg.caller.scopes=TIKU_RESOURCE_READ;
    s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_HEX_APP));surface_step(s);assert(surface_hex_open(s->active.state,"/saved",0,0));v=view(s,&media);assert(!v.byte_writable);
    input(s,ACTION_BYTE_EDIT,"56");v=view(s,&media);assert(v.byte_dirty&&surface_hex_hit(&v,180,335)==0);
    surface_hex_action(s->active.state,ACTION_BYTE_SAVE);v=view(s,&media);assert(v.byte_dirty&&v.status==TIKU_RESOURCE_DENIED);stored("saved",bytes,8192);
    surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(&media);}assert(!s&&!host.memory.live);
    for(unsigned failure=1;failure<12;++failure){
        s=surface_create(&cfg);assert(s);host.memory.calls=0;host.memory.fail_at=failure;
        assert(surface_launch(s,SURFACE_HEX_APP));surface_step(s);host.memory.fail_at=0;
        surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(&media);}assert(!s&&!host.memory.live);
    }
    free(bridge);free(disk);puts("Hex: binary round trips, touch, search, saved history, conflicts, dirty close, cancellation and allocation failures passed");return 0;
}
