/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_editor_indent.c - retained indentation, touch commands and saved text.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_EDITOR_LINES_MAIN line_fixture_main
#include "test_editor_lines.c"
int main(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    cfg.storage=surface_media_init(&media,bridge);cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_EDITOR_APP));settle(s,&media);surface_view_t v;
    draft(s,"alpha\n\nbeta");key(s,'a',TIKU_MOD_CMD);file_tap(s,&media,600,250);surface_view(s,&v);
    assert(v.editor_tools==2&&surface_editor_hit(&v,100,252)==ACTION_EDITOR_INDENT&&surface_editor_hit(&v,500,252)==ACTION_EDITOR_UNINDENT);
    painting(s);file_capture(s,&host,"editor-indent-tools");file_tap(s,&media,100,252);content(s,"    alpha\n\n    beta");
    key(s,'z',TIKU_MOD_CMD);content(s,"alpha\n\nbeta");key(s,'z',TIKU_MOD_CMD|TIKU_MOD_SHIFT);content(s,"    alpha\n\n    beta");
    file_tap(s,&media,600,250);file_tap(s,&media,500,252);content(s,"alpha\n\nbeta");
    draft(s,"a\nb");key(s,'a',TIKU_MOD_CMD);key(s,TIKU_KEY_TAB,0);content(s,"    a\n    b");key(s,TIKU_KEY_TAB,TIKU_MOD_SHIFT);content(s,"a\nb");
    draft(s,"abc");surface_editor_action(s->active.state,ACTION_EDITOR_CELL+1);key(s,TIKU_KEY_TAB,0);content(s,"a    bc");key(s,'z',TIKU_MOD_CMD);content(s,"abc");
    draft(s,"\ta\n  b");key(s,'a',TIKU_MOD_CMD);surface_editor_action(s->active.state,ACTION_EDITOR_UNINDENT);content(s,"a\nb");
    draft(s,"\n\n");key(s,'a',TIKU_MOD_CMD);surface_editor_action(s->active.state,ACTION_EDITOR_INDENT);surface_view(s,&v);assert(v.editor_error==TIKU_EDIT_NOTHING);content(s,"\n\n");
    draft(s,"");surface_editor_action(s->active.state,ACTION_EDITOR_INDENT);content(s,"    ");key(s,'z',TIKU_MOD_CMD);content(s,"");
    static char full[8193];memset(full,'x',8192);full[8192]=0;draft(s,full);surface_editor_action(s->active.state,ACTION_EDITOR_INDENT);surface_view(s,&v);assert(v.editor_error==TIKU_EDIT_LIMIT);content(s,full);
    draft(s,"save\nthis");key(s,'a',TIKU_MOD_CMD);surface_editor_action(s->active.state,ACTION_EDITOR_INDENT);content(s,"    save\n    this");
    file_tap(s,&media,190,338);file_name(s,"/indent");settle(s,&media);stored("indent","    save\n    this");
    assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);settle(s,&media);assert(surface_launch(s,SURFACE_EDITOR_APP));settle(s,&media);
    tiku_mfs_object_t object;uint64_t rank,epoch;assert(!tiku_mfs_enumerate(&fs,"indent",0,0,&object,&rank,&epoch));assert(surface_editor_open(s->active.state,"/indent",epoch,rank+1));settle(s,&media);content(s,"    save\n    this");
    surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(&media);}assert(!s&&!host.memory.live&&!store.count&&!store.running);
    free(bridge);free(disk);puts("Editor indentation: touch commands, blank lines, tabs, history, capacity and saved files passed");return 0;
}
