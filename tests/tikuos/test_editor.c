/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_editor.c - retained editing and complete file saves through native media.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main media_fixture_main
#include "test_media.c"
#undef main
/** @brief Create a completed source file in private media. */
static void document(const char *name,const char *text,size_t bytes)
{
    uint64_t id;assert(!tiku_mfs_begin(&fs,name,TIKU_MFS_STREAM,0,NULL,&id));
    if(bytes)assert(!tiku_mfs_append(&fs,text,bytes));
    assert(!tiku_mfs_finalize(&fs));
}
/** @brief Deliver one physical key through the application event queue. */
static void key(surface_t *s,unsigned k,unsigned modifiers)
{ tiku_input_event_t e={.type=TIKU_EVENT_KEY_DOWN,.key=k,.modifiers=modifiers};assert(surface_post(s,&e));surface_step(s); }
/** @brief Deliver committed text in bounded complete ASCII chunks. */
static void type(surface_t *s,const char *text)
{
    while(*text){tiku_input_event_t e={.type=TIKU_EVENT_TEXT_INPUT};size_t n=strlen(text);if(n>7)n=7;
        memcpy(e.text,text,n);assert(surface_post(s,&e));surface_step(s);text+=n;}
}
/** @brief Compare the entire persisted file through a retained reader. */
static void stored(const char *name,const char *expected)
{
    tiku_mfs_object_t object;uint64_t reader;char out[128];size_t n=strlen(expected);
    assert(!tiku_mfs_stat(&fs,name,&object)&&object.length==n);
    assert(!tiku_mfs_open_read(&fs,name,&reader));assert(tiku_mfs_read_at(&fs,reader,0,out,sizeof out)==(int)n&&!memcmp(out,expected,n));
    assert(!tiku_mfs_close_read(&fs,reader));
}
#ifndef SURFACE_EDITOR_MAIN
#define SURFACE_EDITOR_MAIN main
#endif
int SURFACE_EDITOR_MAIN(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};
    reset();document("note","hello\nworld",11);
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    cfg.storage=surface_media_init(&media,bridge);cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));settle(s,&media);
    file_tap(s,&media,70,130);settle(s,&media);file_tap(s,&media,70,360);settle(s,&media);
    surface_view_t v;surface_view(s,&v);assert(v.foreground==SURFACE_EDITOR_APP&&v.editor_bytes==11&&!v.editor_dirty&&!strcmp(v.editor_grid.rows[0],"hello"));
    key(s,'a',TIKU_MOD_CMD);type(s,"first\nsecond");surface_view(s,&v);assert(v.editor_dirty&&v.editor_bytes==12);
    key(s,'z',TIKU_MOD_CMD);key(s,'z',TIKU_MOD_CMD);surface_view(s,&v);assert(v.editor_bytes==11&&!v.editor_dirty);
    key(s,'z',TIKU_MOD_CMD|TIKU_MOD_SHIFT);key(s,'z',TIKU_MOD_CMD|TIKU_MOD_SHIFT);
    file_capture(s,&host,"editor");
    file_tap(s,&media,70,338);settle(s,&media);stored("note","first\nsecond");surface_view(s,&v);assert(!v.editor_dirty&&!v.editor_busy);
    type(s,"!");file_tap(s,&media,70,338);settle(s,&media);stored("note","first\nsecond!");
    file_tap(s,&media,440,338);surface_view(s,&v);assert(v.text_entry&&v.editor_dialog==3);
    file_name(s,"first");surface_view(s,&v);assert(!v.text_entry&&v.editor_grid.selected[0]==31);
    key(s,'c',TIKU_MOD_CMD);assert(!strcmp(s->clipboard,"first"));key(s,'x',TIKU_MOD_CMD);
    key(s,'v',TIKU_MOD_CMD);surface_view(s,&v);assert(v.editor_dirty);
    assert(surface_close(s)==SURFACE_CLOSE_CONFIRM);surface_view(s,&v);assert(v.close_confirmation==3);
    file_capture(s,&host,"editor-unsaved");assert(surface_resolve_close(s,0));
    assert(surface_launch(s,SURFACE_ARITHMETIC_APP));surface_step(s);assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);surface_view(s,&v);assert(v.editor_dirty);
    file_tap(s,&media,190,338);surface_view(s,&v);assert(v.text_entry&&v.editor_dialog==2);file_capture(s,&host,"editor-save-as");
    file_name(s,"/copy");settle(s,&media);stored("copy","first\nsecond!");surface_view(s,&v);assert(!strcmp(v.editor_path,"/copy")&&!v.editor_dirty);
    type(s,"?");file("concurrent");file_tap(s,&media,70,338);settle(s,&media);surface_view(s,&v);
    assert(v.status==TIKU_RESOURCE_STALE_CURSOR&&v.editor_dirty);stored("copy","first\nsecond!");
    file_tap(s,&media,190,338);file_name(s,"/note");settle(s,&media);surface_view(s,&v);assert(v.status==TIKU_RESOURCE_BUSY&&v.editor_dirty&&!strcmp(v.editor_path,"/copy"));stored("note","first\nsecond!");
    file_tap(s,&media,190,338);file_name(s,"/saved");settle(s,&media);surface_view(s,&v);assert(v.status==TIKU_RESOURCE_OK&&!v.editor_dirty);
    stored("saved","first?\nsecond!");
    assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);settle(s,&media);
    assert(surface_launch(s,SURFACE_BROWSER));settle(s,&media);
    surface_app_action(s->active.state,ACTION_REFRESH);settle(s,&media);
    surface_view(s,&v);unsigned row;for(row=0;row<6&&strcmp(v.rows[row],"saved");++row){}
    assert(row<6);file_tap(s,&media,70,130+44*row);settle(s,&media);
    file_tap(s,&media,70,360);settle(s,&media);surface_view(s,&v);
    assert(v.foreground==SURFACE_EDITOR_APP&&!strcmp(v.editor_path,"/saved")&&!strcmp(v.editor_grid.rows[0],"first?")&&!v.editor_dirty);
    static char large[8193];memset(large,'a',sizeof large);document("large",large,sizeof large);
    document("invalid","\xc0\x80",2);
    tiku_mfs_object_t source;uint64_t rank,epoch;assert(!tiku_mfs_enumerate(&fs,"large",0,0,&source,&rank,&epoch));
    assert(surface_editor_open(s->active.state,"/large",epoch,rank+1));settle(s,&media);surface_view(s,&v);
    assert(v.status==TIKU_RESOURCE_LIMIT&&!strcmp(v.editor_path,"/saved")&&!strcmp(v.editor_grid.rows[0],"first?"));
    assert(!tiku_mfs_enumerate(&fs,"invalid",0,0,&source,&rank,&epoch));
    assert(surface_editor_open(s->active.state,"/invalid",epoch,rank+1));settle(s,&media);surface_view(s,&v);
    assert(v.editor_error==TIKU_EDIT_INVALID_TEXT&&!strcmp(v.editor_path,"/saved")&&!strcmp(v.editor_grid.rows[0],"first?"));
    assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);settle(s,&media);
    for(unsigned i=0;i<100;++i){assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);surface_view(s,&v);assert(!v.editor_bytes&&!v.editor_dirty);assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);settle(s,&media);}
    assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);type(s,"borrowed");
    file_tap(s,&media,190,338);file_name(s,"/cancel");
    unsigned n;for(n=0;n<30000;++n){surface_step(s);surface_media_poll(&media);if(media.request&&media.request->operation==TIKU_STORAGE_REPLACE&&store.running==4)break;}
    assert(n<30000);close_refusals=3;assert(surface_close(s)==SURFACE_CLOSE_CONFIRM);assert(surface_resolve_close(s,1));surface_view(s,&v);assert(v.retiring);
    settle(s,&media);if(fs.failed)assert(!tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used));
    tiku_mfs_object_t object;assert(tiku_mfs_stat(&fs,"cancel",&object)==TIKU_MFS_NOENT);stored("saved","first?\nsecond!");
    surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(&media);}
    assert(!s&&!host.memory.live&&!store.count&&!store.running);
    cfg.storage.capabilities&=~TIKU_RESOURCE_FEATURE_CHANGE;cfg.caller.scopes=TIKU_RESOURCE_READ;
    s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));settle(s,&media);surface_view(s,&v);
    for(row=0;row<6&&strcmp(v.rows[row],"saved");++row){}assert(row<6);
    file_tap(s,&media,70,130+44*row);settle(s,&media);file_tap(s,&media,70,360);settle(s,&media);surface_view(s,&v);
    assert(v.foreground==SURFACE_EDITOR_APP&&!strcmp(v.editor_path,"/saved"));
    type(s,"readonly");file_tap(s,&media,70,338);settle(s,&media);surface_view(s,&v);
    assert(v.status!=TIKU_RESOURCE_OK&&v.editor_dirty);stored("saved","first?\nsecond!");
    surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(&media);}
    assert(!s&&!host.memory.live&&!store.count&&!store.running);free(bridge);free(disk);
    puts("Editor: open, UTF-8 model, edits, save/save-as, reopen lifetime, find, clipboard, conflicts and cancellation passed");return 0;
}
