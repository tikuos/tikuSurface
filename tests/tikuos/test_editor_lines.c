/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_editor_lines.c - native line commands through retained Editor controls.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_EDITOR_MAIN editor_fixture_main
#include "test_editor.c"
/** @brief Compare the full retained text without changing its selection. */
static void content(surface_t *s,const char *expected)
{
    size_t length=0;const char *text=surface_editor_program(s->active.state,&length);
    assert(length==strlen(expected));if(length)assert(text&&!memcmp(text,expected,length));
}
/** @brief Replace the selected document through ordinary key input. */
static void draft(surface_t *s,const char *text)
{key(s,'a',TIKU_MOD_CMD);key(s,TIKU_KEY_BACKSPACE,0);type(s,text);content(s,text);}
/** @brief Full and clipped rendering produce the same tool controls. */
static void painting(surface_t *s)
{
    static uint32_t whole[800*480],sliced[800*480];surface_view_t v;surface_view(s,&v);
    tiku_surface_t a={.px=whole,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=sliced;
    surface_paint(&a,&v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}assert(!memcmp(whole,sliced,sizeof whole));
}
#ifndef SURFACE_EDITOR_LINES_MAIN
#define SURFACE_EDITOR_LINES_MAIN main
#endif
int SURFACE_EDITOR_LINES_MAIN(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    cfg.storage=surface_media_init(&media,bridge);cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_EDITOR_APP));settle(s,&media);surface_view_t v;
    draft(s,"a\nb");surface_editor_action(s->active.state,ACTION_EDITOR_CELL);
    file_tap(s,&media,600,250);surface_view(s,&v);assert(v.editor_tools==2);painting(s);file_capture(s,&host,"editor-line-tools");
    assert(surface_editor_hit(&v,100,140)==ACTION_EDITOR_DUPLICATE);file_tap(s,&media,100,140);content(s,"a\na\nb");
    key(s,'z',TIKU_MOD_CMD);content(s,"a\nb");key(s,'z',TIKU_MOD_CMD|TIKU_MOD_SHIFT);content(s,"a\na\nb");
    draft(s,"a\nb");surface_editor_action(s->active.state,ACTION_EDITOR_CELL);surface_editor_action(s->active.state,ACTION_EDITOR_LINES);
    surface_view(s,&v);assert(surface_editor_hit(&v,100,196)==ACTION_EDITOR_MOVE_DOWN);file_tap(s,&media,100,196);content(s,"b\na");
    surface_editor_action(s->active.state,ACTION_EDITOR_LINES);file_tap(s,&media,500,140);content(s,"a\nb");
    surface_editor_action(s->active.state,ACTION_EDITOR_LINES);file_tap(s,&media,500,196);content(s,"b");key(s,'z',TIKU_MOD_CMD);content(s,"a\nb");
    surface_editor_action(s->active.state,ACTION_EDITOR_LINES);file_tap(s,&media,500,140);surface_view(s,&v);assert(v.editor_error==TIKU_EDIT_NOTHING&&v.editor_tools==2);content(s,"a\nb");

#if SURFACE_EDITOR_DOCUMENTS
    surface_editor_action(s->active.state,ACTION_EDITOR_SESSIONS);surface_view(s,&v);assert(v.editor_list&&surface_editor_hit(&v,100,130)==ACTION_EDITOR_SESSION_ROW);file_tap(s,&media,100,130);
#endif
    key(s,TIKU_KEY_ESCAPE,0);surface_view(s,&v);assert(!v.editor_tools);
#if SURFACE_EDITOR_DOCUMENTS
    assert(!v.editor_list);
#endif
    static char full[8193];memset(full,'x',8192);full[8192]=0;draft(s,full);surface_editor_action(s->active.state,ACTION_EDITOR_DUPLICATE);surface_view(s,&v);assert(v.editor_error==TIKU_EDIT_LIMIT);content(s,full);
    draft(s,"saved\nlines");surface_editor_action(s->active.state,ACTION_EDITOR_CELL+60);surface_editor_action(s->active.state,ACTION_EDITOR_DUPLICATE);content(s,"saved\nlines\nlines");
    file_tap(s,&media,190,338);file_name(s,"/lines");settle(s,&media);stored("lines","saved\nlines\nlines");
    assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);settle(s,&media);assert(surface_launch(s,SURFACE_EDITOR_APP));settle(s,&media);
    tiku_mfs_object_t object;uint64_t rank,epoch;assert(!tiku_mfs_enumerate(&fs,"lines",0,0,&object,&rank,&epoch));assert(surface_editor_open(s->active.state,"/lines",epoch,rank+1));settle(s,&media);content(s,"saved\nlines\nlines");
    surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(&media);}assert(!s&&!host.memory.live&&!store.count&&!store.running);
    free(bridge);free(disk);puts("Editor line tools: touch controls, retained documents, one-step history, capacity and saved files passed");return 0;
}
