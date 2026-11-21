/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_editor_documents.c - retained drafts across file handoff and close.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_EDITOR_MAIN editor_fixture_main
#include "test_editor.c"
/** @brief Open a file through the selected filesystem identity. */
static int open_document(surface_t *s,surface_media_t *media,const char *name)
{
    tiku_mfs_object_t object;uint64_t rank,epoch;char path[65];snprintf(path,sizeof path,"/%s",name);
    assert(!tiku_mfs_enumerate(&fs,name,0,0,&object,&rank,&epoch));
    int result=surface_editor_open(s->active.state,path,epoch,rank+1);settle(s,media);return result;
}
int main(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();
    for(unsigned i=0;i<9;++i){char name[16];snprintf(name,sizeof name,"doc%u",i);document(name,name,strlen(name));}
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    cfg.storage=surface_media_init(&media,bridge);cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_EDITOR_APP));settle(s,&media);
    surface_view_t v;
    for(unsigned i=0;i<8;++i){char name[16];snprintf(name,sizeof name,"doc%u",i);assert(open_document(s,&media,name));key(s,'a',TIKU_MOD_CMD);type(s,"draft");surface_view(s,&v);assert(v.editor_dirty&&v.editor_bytes==5&&v.editor_count==i+1);}
    assert(!surface_editor_can_open(s->active.state,"/doc8")&&!open_document(s,&media,"doc8"));
    assert(open_document(s,&media,"doc0"));surface_view(s,&v);assert(v.editor_dirty&&!strcmp(v.editor_grid.rows[0],"draft")&&v.editor_count==8);
    key(s,'z',TIKU_MOD_CMD);surface_view(s,&v);assert(!v.editor_dirty&&!strcmp(v.editor_grid.rows[0],"doc0"));
    assert(surface_close(s)==SURFACE_CLOSE_CONFIRM);assert(surface_resolve_close(s,0));
    assert(open_document(s,&media,"doc1"));surface_app_action(s->active.state,ACTION_EDITOR_SAVE);settle(s,&media);stored("doc1","draft");
    assert(open_document(s,&media,"doc2"));surface_app_action(s->active.state,ACTION_EDITOR_SESSIONS);surface_view(s,&v);assert(v.editor_list);file_capture(s,&host,"editor-documents");
    surface_app_action(s->active.state,ACTION_EDITOR_SESSION_CLOSE);surface_view(s,&v);assert(v.editor_confirm);
    surface_app_action(s->active.state,ACTION_EDITOR_KEEP);surface_view(s,&v);assert(!v.editor_confirm&&v.editor_count==8);
    surface_app_action(s->active.state,ACTION_EDITOR_SESSION_CLOSE);surface_app_action(s->active.state,ACTION_EDITOR_DISCARD);settle(s,&media);surface_view(s,&v);assert(v.editor_count==7);
    document("invalid","\xc0\x80",2);assert(open_document(s,&media,"invalid"));surface_view(s,&v);assert(v.editor_count==7&&v.editor_error==TIKU_EDIT_INVALID_TEXT);
    assert(open_document(s,&media,"doc8"));surface_view(s,&v);assert(v.editor_count==8&&!strcmp(v.editor_grid.rows[0],"doc8"));
    assert(surface_launch(s,SURFACE_BROWSER));settle(s,&media);file_tap(s,&media,70,262);settle(s,&media);
    file_tap(s,&media,70,360);settle(s,&media);surface_view(s,&v);assert(v.foreground==SURFACE_EDITOR_APP&&v.editor_dirty&&!strcmp(v.editor_path,"/doc3")&&!strcmp(v.editor_grid.rows[0],"draft"));
    surface_editor_dialog(s->active.state,2);assert(surface_editor_accept(s->active.state,"/cancel"));key(s,TIKU_KEY_ESCAPE,0);
    unsigned turn;for(turn=0;turn<30000;++turn){surface_step(s);surface_media_poll(&media);if(media.request&&media.request->operation==TIKU_STORAGE_REPLACE&&store.running==4)break;}
    assert(turn<30000);close_refusals=3;surface_view(s,&v);assert(!v.text_entry);
    assert(v.editor_busy&&surface_hit(&v,600,196)==ACTION_EDITOR_SESSIONS);
    file_tap(s,&media,600,196);surface_view(s,&v);assert(v.editor_list);
    file_tap(s,&media,300,410);surface_view(s,&v);assert(v.editor_confirm);
    file_tap(s,&media,500,272);
    surface_view(s,&v);assert(v.editor_count==8);settle(s,&media);surface_view(s,&v);assert(v.editor_count==7);
    if(fs.failed)assert(!tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used));
    tiku_mfs_object_t object;assert(tiku_mfs_stat(&fs,"cancel",&object)==TIKU_MFS_NOENT);stored("doc1","draft");
    assert(surface_close(s)==SURFACE_CLOSE_CONFIRM&&surface_resolve_close(s,1));settle(s,&media);
    assert(surface_launch(s,SURFACE_EDITOR_APP));settle(s,&media);
    for(unsigned fail=1;fail<=6;++fail){size_t live=host.memory.live;host.memory.fail_at=host.memory.calls+fail;
        surface_editor_action(s->active.state,ACTION_EDITOR_SESSION_NEW);host.memory.fail_at=0;surface_view(s,&v);
        if(v.editor_count==2){surface_editor_action(s->active.state,ACTION_EDITOR_SESSION_CLOSE);settle(s,&media);}
        surface_view(s,&v);assert(v.editor_count==1&&host.memory.live==live);
    }
    surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(&media);}assert(!s&&!host.memory.live&&!store.count&&!store.running);
    free(bridge);free(disk);puts("Editor documents: eight drafts, independent undo, existing-path focus, capacity, save, close and Tracker handoff passed");return 0;
}
