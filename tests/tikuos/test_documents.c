/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_documents.c - Tracker type routing, modal choice and retained document ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main media_fixture_main
#include "tiku_event.h"
#include "test_media.c"
#undef main
/** @brief Create a completed document in the private media fixture. */
static void document(const char *name,const char *bytes,size_t length)
{uint64_t id;assert(!tiku_mfs_begin(&fs,name,TIKU_MFS_STREAM,0,NULL,&id));if(length)assert(!tiku_mfs_append(&fs,bytes,length));assert(!tiku_mfs_finalize(&fs));}
/** @brief Deliver one application or modal key. */
static void key(surface_t *s,unsigned code,unsigned modifiers)
{tiku_input_event_t event={.type=TIKU_EVENT_KEY_DOWN,.key=code,.modifiers=modifiers};assert(surface_post(s,&event));surface_step(s);}
/** @brief Settle queued storage and copy the resulting application presentation. */
static surface_view_t view(surface_t *s,surface_media_t *media)
{settle(s,media);surface_view_t v;surface_view(s,&v);return v;}
/** @brief Select a file by its visible name after refreshing Tracker's current epoch. */
static surface_view_t select_file(surface_t *s,surface_media_t *media,const char *name)
{
    assert(surface_launch(s,SURFACE_BROWSER));settle(s,media);file_tap(s,media,70,410);
    surface_view_t v=view(s,media);unsigned row;
    for(row=0;row<v.row_count&&strcmp(v.rows[row],name);++row){}
    assert(row<v.row_count);file_tap(s,media,70,130+44*(int)row);return view(s,media);
}
/** @brief Compare complete rendering with clipped two-row updates. */
static void painting(surface_t *s)
{
    static uint32_t whole[800*480],sliced[800*480];surface_view_t v;surface_view(s,&v);
    tiku_surface_t a={.px=whole,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=sliced;
    surface_paint(&a,&v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}assert(!memcmp(whole,sliced,sizeof whole));
}
#ifndef SURFACE_DOCUMENTS_MAIN
#define SURFACE_DOCUMENTS_MAIN main
#endif
int SURFACE_DOCUMENTS_MAIN(void)
{
    assert(!surface_document_type("/docs.md/file"));assert(!surface_document_type("/unknown"));
    assert(!strcmp(surface_document_type("/a.MARKDOWN"),"text/markdown"));
    assert(!strcmp(surface_document_type("/a.Txt"),"text/plain"));
    surface_app_t apps[3];unsigned relations[3];
    assert(surface_document_choices("/a.html",apps,relations)==3&&apps[0]==SURFACE_READER_APP&&apps[1]==SURFACE_EDITOR_APP&&apps[2]==SURFACE_HEX_APP);
    assert(surface_document_choices("/a.txt",apps,relations)==2&&apps[0]==SURFACE_EDITOR_APP&&apps[1]==SURFACE_HEX_APP);
    assert(surface_document_choices("/a.bin",apps,relations)==1&&apps[0]==SURFACE_HEX_APP&&relations[0]==3);
#if SURFACE_IMAGE
    assert(surface_document_choices("/a.png",apps,relations)==2&&apps[0]==SURFACE_IMAGE_APP&&apps[1]==SURFACE_HEX_APP&&relations[0]==3&&relations[1]==1);
#else
    assert(surface_document_choices("/a.png",apps,relations)==1&&relations[0]==1);
#endif
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();
    document("a.Txt","hello",5);document("b.HTML","<h1>Guide</h1>",14);document("c.bin","\x00\xff",2);document("d.odd","unknown",7);document("e.png","\x89PNG",4);
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    cfg.storage=surface_media_init(&media,bridge);cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s);surface_view_t v=select_file(s,&media,"a.Txt");
    assert(v.document_default==SURFACE_EDITOR_APP);painting(s);file_capture(s,&host,"tracker-open");
    file_tap(s,&media,500,128);v=view(s,&media);assert(v.foreground==SURFACE_EDITOR_APP&&!strcmp(v.editor_path,"/a.Txt")&&v.editor_bytes==5);
    key(s,'x',0);v=view(s,&media);assert(v.editor_dirty);
    v=select_file(s,&media,"a.Txt");key(s,'o',TIKU_MOD_CMD);surface_view(s,&v);
#if SURFACE_EDITOR_DOCUMENTS
    assert(v.foreground==SURFACE_EDITOR_APP&&v.editor_dirty&&v.editor_bytes==6);
    assert(surface_launch(s,SURFACE_BROWSER));v=view(s,&media);
#else
    assert(v.foreground==SURFACE_BROWSER&&v.document_error==SURFACE_EDITOR_APP&&surface_editor_modified(s->background[SURFACE_EDITOR_APP].state));
#endif
    v=select_file(s,&media,"b.HTML");key(s,'o',TIKU_MOD_CMD|TIKU_MOD_SHIFT);surface_view(s,&v);
    assert(v.document_menu&&v.document_count==3&&!surface_launch(s,SURFACE_HOME)&&surface_close(s)==SURFACE_CLOSE_BUSY);
    painting(s);file_capture(s,&host,"open-with");key(s,TIKU_KEY_ESCAPE,0);surface_view(s,&v);assert(!v.document_menu&&v.foreground==SURFACE_BROWSER);
    file_tap(s,&media,680,128);key(s,'2',0);surface_view(s,&v);
#if SURFACE_EDITOR_DOCUMENTS
    v=view(s,&media);assert(v.foreground==SURFACE_EDITOR_APP&&!strcmp(v.editor_path,"/b.HTML"));
    v=select_file(s,&media,"b.HTML");
#else
    assert(!v.document_menu&&v.document_error==SURFACE_EDITOR_APP);
#endif
    file_tap(s,&media,500,128);v=view(s,&media);assert(v.foreground==SURFACE_READER_APP&&v.reader_page&&!v.reader_error&&!strcmp(v.reader_path,"/b.HTML"));
    const void *page=v.reader_page;v=select_file(s,&media,"b.HTML");
    surface_app_state_t *reader=s->background[SURFACE_READER_APP].state;
    assert(surface_reader_open(reader,"/b.HTML",s->active.state->epoch,s->active.state->node));
    assert(surface_document_action(s,ACTION_DOCUMENT_OPEN));surface_view(s,&v);assert(v.foreground==SURFACE_BROWSER&&v.document_error==SURFACE_READER_APP);
    settle(s,&media);
    v=select_file(s,&media,"d.odd");file_tap(s,&media,500,128);surface_view(s,&v);
    assert(v.document_menu&&!v.document_default&&v.document_count==1);file_capture(s,&host,"unknown-type");
    file_tap(s,&media,600,390);surface_view(s,&v);assert(!v.document_menu);
    file_tap(s,&media,500,128);file_tap(s,&media,300,180);v=view(s,&media);
    assert(v.foreground==SURFACE_HEX_APP&&v.byte_size==7&&!strcmp(v.byte_path,"/d.odd"));
    v=select_file(s,&media,"c.bin");key(s,'o',TIKU_MOD_CMD);v=view(s,&media);assert(v.foreground==SURFACE_HEX_APP&&v.byte_size==2&&v.byte_cells[1]==255);
    assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);settle(s,&media);
    v=select_file(s,&media,"c.bin");host.memory.fail_at=host.memory.calls+1;file_tap(s,&media,500,128);surface_view(s,&v);
    assert(v.foreground==SURFACE_HOME&&v.document_error==SURFACE_HEX_APP&&!s->open_path[0]&&s->document_target==SURFACE_HOME);
    host.memory.fail_at=0;assert(surface_launch(s,SURFACE_READER_APP));v=view(s,&media);assert(!strcmp(v.reader_path,"/b.HTML")&&v.reader_page);(void)page;
    assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);assert(surface_close(s)==SURFACE_CLOSE_CONFIRM);assert(surface_resolve_close(s,1));settle(s,&media);
    v=select_file(s,&media,"a.Txt");file_tap(s,&media,500,410);file_name(s,"/a.md");settle(s,&media);
    v=select_file(s,&media,"a.md");assert(v.document_default==SURFACE_READER_APP);file_tap(s,&media,500,128);v=view(s,&media);assert(v.foreground==SURFACE_READER_APP&&!strcmp(v.reader_path,"/a.md"));
    v=select_file(s,&media,"a.md");document("foreign","x",1);file_tap(s,&media,500,128);v=view(s,&media);
    assert(v.foreground==SURFACE_READER_APP&&v.reader_error&&!strcmp(v.reader_path,"/a.md"));
    v=select_file(s,&media,"e.png");file_tap(s,&media,500,128);
#if SURFACE_IMAGE
    v=view(s,&media);assert(v.foreground==SURFACE_IMAGE_APP&&v.image_error&&!v.image_frame);
#else
    surface_view(s,&v);assert(v.document_menu&&!v.document_default&&!strcmp(v.document_type,"image/png"));
#endif
    surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(&media);}assert(!s&&!host.memory.live&&!store.count&&!store.running);
    free(bridge);free(disk);puts("Documents: type routing, sorted Open with, retained targets, modal keys, stale epochs, rename and failed-start cleanup passed");return 0;
}
