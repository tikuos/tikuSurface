/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_diff.c - retained comparison snapshots over queued private media.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main media_fixture_main
#include "test_media.c"
#undef main
#include "tiku_diff.h"
static tiku_storage_provider_t backing;
static unsigned file_reads;
/** @brief Count read submissions while preserving the actual queued media provider. */
static tiku_resource_error_t counted_submit(void *ctx,tiku_paged_storage_t *owner,const tiku_storage_request_t *r)
{file_reads+=r->operation==TIKU_STORAGE_READ;return backing.ops->submit(backing.context,owner,r);}
/** @brief Retain the provider's asynchronous cancellation lifetime. */
static void counted_cancel(void *ctx,tiku_paged_storage_t *owner,uint64_t token)
{backing.ops->cancel(backing.context,owner,token);}
static const tiku_storage_provider_ops_t counted_ops={counted_submit,counted_cancel};
/** @brief Create exact fixture content without a physical device. */
static void document(const char *name,const void *bytes,size_t n)
{uint64_t id;assert(!tiku_mfs_begin(&fs,name,TIKU_MFS_STREAM,0,NULL,&id));if(n)assert(!tiku_mfs_append(&fs,bytes,n));assert(!tiku_mfs_finalize(&fs));}
/** @brief Finish queued storage and CPU progression before reading the view. */
static surface_view_t view(surface_t *s,surface_media_t *m)
{settle(s,m);surface_view_t v;surface_view(s,&v);return v;}
/** @brief Route a key through the regular application queue. */
static void key(surface_t *s,unsigned value,unsigned modifiers)
{tiku_input_event_t e={.type=TIKU_EVENT_KEY_DOWN,.key=value,.modifiers=modifiers};assert(surface_post(s,&e));surface_step(s);}
/** @brief Compare complete and clipped painting and capture the current view. */
static void painting(surface_t *s,surface_host_t *host,const char *name)
{
    static uint32_t full[800*480],rows[800*480];surface_view_t v;surface_view(s,&v);
    tiku_surface_t a={.px=full,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=rows;
    surface_paint(&a,&v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}assert(!memcmp(full,rows,sizeof full));file_capture(s,host,name);
}
/** @brief Drain application owners, media readers and the firmware queue. */
static void destroy(surface_t *s,surface_host_t *host,surface_media_t *m)
{surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(m);}assert(!s&&!host->memory.live&&!store.count&&!store.running);}
/** @brief Each admitted constructor allocation can fail without retaining resources. */
static void failures(void)
{
    unsigned allocations=0;
    for(unsigned fail=0;fail<=allocations;++fail){surface_host_t host;surface_config_t cfg=surface_host_config(&host);
        surface_t *s=surface_create(&cfg);assert(s);unsigned before=(unsigned)host.memory.calls;host.memory.fail_at=fail?before+fail:0;
        assert(surface_launch(s,SURFACE_DIFF_APP));surface_step(s);surface_view_t v;surface_view(s,&v);
        if(!fail){assert(v.foreground==SURFACE_DIFF_APP);allocations=(unsigned)host.memory.calls-before;assert(allocations>=7);}
        else assert(v.foreground!=SURFACE_DIFF_APP);
        surface_stop(s);for(unsigned i=0;i<30&&!surface_destroy(&s);++i){surface_step(s);surface_host_poll(&host);}assert(!s&&!host.memory.live);
    }
}
#ifndef SURFACE_DIFF_MAIN
#define SURFACE_DIFF_MAIN main
#endif
int SURFACE_DIFF_MAIN(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();
    const char left[]="first\nleft\nlast\n",right[]="first\nright\nlast\n";
    document("left",left,sizeof left-1);document("right",right,sizeof right-1);
    document("space","first\nleft  \nlast\n",18);document("cr","first\r\nleft\r\nlast\r\n",19);
    document("wide","01234567890123456789012345678901WIDE-TEXT",41);
    document("empty",NULL,0);document("binary","a\0b",3);document("binary2","a\0c",3);
    char long_text[8193];memset(long_text,'x',sizeof long_text);document("large",long_text,sizeof long_text);
    memset(long_text,'\n',257);document("lines",long_text,257);document("max-lines",long_text,256);
    for(unsigned i=0;i<sizeof long_text;++i)long_text[i]=i%32==31?'\n':'x';
    document("long",long_text,8192);
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    cfg.storage=surface_media_init(&media,bridge);backing=cfg.storage;cfg.storage.ops=&counted_ops;
    cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s);painting(s,&host,"diff-launcher");
    file_tap(s,&media,500,365);surface_view_t v=view(s,&media);assert(v.foreground==SURFACE_DIFF_APP&&!v.diff_loaded);
    file_tap(s,&media,100,110);surface_view(s,&v);assert(v.text_entry&&v.diff_dialog==1);file_name(s,"/left");v=view(s,&media);assert(v.diff_loaded==1&&!v.diff_ready&&!strcmp(v.diff_path[0],"/left"));
    key(s,'o',TIKU_MOD_CMD|TIKU_MOD_SHIFT);surface_view(s,&v);assert(v.text_entry&&v.diff_dialog==2);file_name(s,"/right");v=view(s,&media);
    assert(v.diff_ready&&!v.diff_equal&&v.diff_removed==1&&v.diff_added==1&&v.diff_rows==4);
    assert(v.diff_kind[1]==TIKU_DIFF_DEL&&!strcmp(v.diff_text[0][1],"left")&&v.diff_kind[2]==TIKU_DIFF_ADD&&!strcmp(v.diff_text[1][2],"right"));painting(s,&host,"diff-changes");
    file_tap(s,&media,720,410);v=view(s,&media);assert(!strcmp(v.diff_path[0],"/right")&&!strcmp(v.diff_path[1],"/left")&&v.diff_ready);
    file_tap(s,&media,720,410);v=view(s,&media);assert(!strcmp(v.diff_path[0],"/left")&&!strcmp(v.diff_path[1],"/right")&&v.diff_ready);
    key(s,TIKU_KEY_RIGHT,0);v=view(s,&media);assert(v.diff_first==1);key(s,TIKU_KEY_RIGHT,0);v=view(s,&media);assert(v.diff_first==2);key(s,TIKU_KEY_LEFT,0);assert(view(s,&media).diff_first==1);
    assert(surface_launch(s,SURFACE_HOME));surface_step(s);assert(surface_launch(s,SURFACE_DIFF_APP));v=view(s,&media);assert(v.diff_first==1&&v.diff_ready);
    assert(surface_diff_open(s->active.state,1,"/cr"));v=view(s,&media);assert(!v.diff_equal);file_tap(s,&media,680,110);v=view(s,&media);assert(v.diff_equal&&(v.diff_flags&TIKU_DIFF_CR));painting(s,&host,"diff-identical");
    assert(surface_diff_open(s->active.state,1,"/space"));v=view(s,&media);assert(!v.diff_equal);
    file_tap(s,&media,300,110);v=view(s,&media);assert(v.diff_equal&&(v.diff_flags&TIKU_DIFF_SPACE));
    file_tap(s,&media,300,110);v=view(s,&media);assert(!v.diff_equal);
    assert(surface_diff_open(s->active.state,1,"/wide"));v=view(s,&media);
    key(s,TIKU_KEY_RIGHT,TIKU_MOD_SHIFT);v=view(s,&media);assert(v.diff_column==32);
    int found=0;for(unsigned row=0;row<v.diff_rows;++row)if(!strncmp(v.diff_text[1][row],"WIDE-TEXT",9))found=1;assert(found);
    key(s,TIKU_KEY_LEFT,TIKU_MOD_SHIFT);assert(!view(s,&media).diff_column);
    assert(surface_diff_open(s->active.state,1,"/cr"));v=view(s,&media);
    assert(!surface_diff_open(s->active.state,2,"/left")&&!surface_diff_open(s->active.state,0,"/../left"));
    file_reads=0;assert(surface_diff_open(s->active.state,1,"/large"));v=view(s,&media);assert(file_reads==1&&v.status==TIKU_RESOURCE_LIMIT&&!strcmp(v.diff_path[1],"/cr")&&v.diff_ready);
    assert(surface_diff_open(s->active.state,1,"/lines"));v=view(s,&media);assert(v.status==TIKU_RESOURCE_LIMIT&&!strcmp(v.diff_path[1],"/cr"));
    assert(surface_diff_open(s->active.state,1,"/missing"));v=view(s,&media);assert(v.status==TIKU_RESOURCE_NOT_FOUND&&!strcmp(v.diff_path[1],"/cr"));
    assert(surface_diff_open(s->active.state,1,"/max-lines"));v=view(s,&media);assert(v.diff_ready&&v.diff_added==256);
    assert(surface_diff_open(s->active.state,0,"/long"));v=view(s,&media);assert(v.diff_ready&&v.diff_removed==256);key(s,TIKU_KEY_PAGE_DOWN,0);assert(view(s,&media).diff_first==8);key(s,TIKU_KEY_PAGE_UP,0);assert(!view(s,&media).diff_first);
    assert(surface_diff_open(s->active.state,1,"/long"));v=view(s,&media);assert(v.diff_ready&&v.diff_equal&&v.diff_total==256);
    surface_diff_action(s->active.state,ACTION_DIFF_SPACE);surface_view(s,&v);assert(v.diff_busy&&!v.diff_ready);surface_diff_action(s->active.state,ACTION_DIFF_CANCEL);v=view(s,&media);assert(v.status==TIKU_RESOURCE_CANCELLED&&!v.diff_ready);
    surface_diff_action(s->active.state,ACTION_DIFF_SPACE);v=view(s,&media);assert(v.diff_ready&&v.diff_equal);
    assert(surface_diff_open(s->active.state,1,"/right"));surface_step(s);assert(media.request);surface_diff_action(s->active.state,ACTION_DIFF_CANCEL);v=view(s,&media);assert(v.status==TIKU_RESOURCE_CANCELLED&&!strcmp(v.diff_path[1],"/long")&&v.diff_ready);
    file_reads=0;assert(surface_diff_open(s->active.state,1,"/long"));unsigned turn;
    for(turn=0;turn<30000;++turn){surface_step(s);surface_media_poll(&media);if(file_reads==1&&!media.request&&!store.running)break;}
    assert(turn<30000);document("foreign","x",1);v=view(s,&media);assert(v.status==TIKU_RESOURCE_STALE_CURSOR&&!strcmp(v.diff_path[1],"/long"));
    assert(surface_diff_open(s->active.state,0,"/binary"));v=view(s,&media);assert(v.diff_binary&&!v.diff_equal);
    assert(surface_diff_open(s->active.state,1,"/binary"));v=view(s,&media);assert(v.diff_binary&&v.diff_equal);
    assert(surface_diff_open(s->active.state,1,"/binary2"));v=view(s,&media);assert(v.diff_binary&&!v.diff_equal);painting(s,&host,"diff-binary");
    assert(surface_diff_open(s->active.state,0,"/empty"));v=view(s,&media);assert(surface_diff_open(s->active.state,1,"/empty"));v=view(s,&media);assert(v.diff_ready&&v.diff_equal&&!v.diff_rows);
    file_tap(s,&media,590,32);surface_view(s,&v);assert(v.applications_menu);painting(s,&host,"diff-applications");file_tap(s,&media,160,405);assert(view(s,&media).foreground==SURFACE_DIFF_APP);
    assert(surface_diff_open(s->active.state,1,"/long"));surface_step(s);assert(media.request);assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);settle(s,&media);assert(!view(s,&media).retiring);
    destroy(s,&host,&media);free(bridge);free(disk);failures();puts("Diff application: versioned snapshots, shared line results, flags, navigation, binary identity, cancellation, stale/limit preservation and allocation failures passed");return 0;
}
