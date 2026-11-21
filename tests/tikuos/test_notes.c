/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_notes.c - retained notes, quiet autosave, catalog reload and close ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_EDITOR_MAIN editor_fixture_main
#include "test_editor.c"
/** @brief Drain queued note work while advancing the simulated quiet clock. */
static void notes_idle(surface_t *s,surface_host_t *host,surface_media_t *media,int save)
{
    for(unsigned i=0;i<200000;++i){
        host->now+=1000;surface_step(s);surface_media_poll(media);
        surface_view_t v;surface_view(s,&v);
        if(!media->request&&!v.notes_loading&&!v.retiring&&v.status!=TIKU_RESOURCE_PENDING&&(!save||!surface_notes_modified(s->active.state)))return;
    }
    assert(0);
}
/** @brief Invoke a note command through the retained outer app. */
static void action(surface_t *s,int id)
{surface_app_action(s->active.state,id);surface_step(s);}
/** @brief Close and drain the whole shell without abandoning borrowed buffers. */
static void shutdown_notes(surface_t **s,surface_media_t *media,surface_host_t *host)
{
    surface_stop(*s);for(unsigned i=0;i<200000&&!surface_destroy(s);++i){surface_step(*s);surface_media_poll(media);}assert(!*s&&!host->memory.live&&!store.count&&!store.running);
}
int main(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();
    memset(disk,0,DISK_BYTES);tiku_mfs_init(&fs);assert(!tiku_mfs_format_directories(&fs,&block,uuid,used,mark,sizeof used));
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    cfg.storage=surface_media_init(&media,bridge);cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_NOTES_APP));notes_idle(s,&host,&media,0);
    surface_view_t v;surface_view(s,&v);assert(v.foreground==SURFACE_NOTES_APP&&!v.notes_count&&v.notes_selected==-1);
    action(s,ACTION_NOTES_NEW);notes_idle(s,&host,&media,0);surface_view(s,&v);assert(v.notes_count==1&&v.notes_selected==0);
    type(s,"Groceries");surface_app_state_t *e=surface_notes_editor(s->active.state);assert(e&&surface_editor_modified(e));
    action(s,ACTION_EDITOR_RUN);action(s,ACTION_NOTES_PAPER+2);action(s,ACTION_NOTES_IMPORTANT);action(s,ACTION_NOTES_DOCUMENT);
    for(unsigned i=0;i<100;++i){surface_step(s);surface_media_poll(&media);}
    host.now+=499999;for(unsigned i=0;i<100;++i){surface_step(s);surface_media_poll(&media);}
    assert(!surface_editor_busy(e));tiku_mfs_object_t quiet;assert(tiku_mfs_stat(&fs,"Notes/note-001.txt",&quiet)==TIKU_MFS_NOENT);
    ++host.now;notes_idle(s,&host,&media,1);file_capture(s,&host,"notes-document");
    stored("Notes/note-001.txt","# tiku-note paper=Blue important=1\nGroceries\n");
    action(s,ACTION_EDITOR_SAVE_AS);surface_view(s,&v);assert(v.notes_selected==-1&&!strcmp(v.notes_titles[0],"Groceries")&&(v.notes_state[0]&4));file_capture(s,&host,"notes-overview");
    action(s,ACTION_NOTES_REFRESH);notes_idle(s,&host,&media,1);surface_view(s,&v);assert(v.notes_count==1);
    action(s,ACTION_NOTES_ROW);action(s,ACTION_EDITOR_TYPE);surface_view(s,&v);assert(v.text_entry);file_name(s," more");notes_idle(s,&host,&media,1);
    stored("Notes/note-001.txt","# tiku-note paper=Blue important=1\nGroceries more\n");
    host.memory.fail_at=host.memory.calls+1;action(s,ACTION_NOTES_NEW);host.memory.fail_at=0;surface_view(s,&v);assert(v.notes_count==1);
    for(unsigned i=1;i<24;++i){action(s,ACTION_NOTES_NEW);e=surface_notes_editor(s->active.state);assert(e);char body[64];snprintf(body,sizeof body,"Note %u",i+1);assert(surface_editor_accept(e,body));surface_editor_note_metadata(e,i%4,i%2);}
    surface_view(s,&v);assert(v.notes_count==24);action(s,ACTION_NOTES_NEW);surface_view(s,&v);assert(v.notes_count==24);
    action(s,ACTION_NOTES_SAVE_ALL);notes_idle(s,&host,&media,1);stored("Notes/note-024.txt","# tiku-note paper=Pink important=1\nNote 24\n");
    assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);type(s,"independent editor");assert(surface_launch(s,SURFACE_NOTES_APP));surface_step(s);
    type(s," dirty");assert(surface_close(s)==SURFACE_CLOSE_CONFIRM&&surface_resolve_close(s,0));
    action(s,ACTION_EDITOR_RUN);file_capture(s,&host,"notes-tools");action(s,ACTION_NOTES_DELETE);surface_view(s,&v);assert(v.notes_confirm);file_capture(s,&host,"notes-delete");action(s,ACTION_NOTES_KEEP);
    action(s,ACTION_NOTES_DELETE);action(s,ACTION_NOTES_DELETE_CONFIRMED);notes_idle(s,&host,&media,1);surface_view(s,&v);assert(v.notes_count==23&&v.notes_selected==-1);
    tiku_mfs_object_t obj;assert(tiku_mfs_stat(&fs,"Notes/note-024.txt",&obj)==TIKU_MFS_NOENT);
    action(s,ACTION_NOTES_NEW);type(s,"replacement");action(s,ACTION_NOTES_SAVE_ALL);notes_idle(s,&host,&media,1);stored("Notes/note-025.txt","# tiku-note paper=Yellow important=0\nreplacement\n");
    assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);notes_idle(s,&host,&media,0);assert(surface_launch(s,SURFACE_NOTES_APP));notes_idle(s,&host,&media,1);surface_view(s,&v);
    assert(v.notes_count==24&&!strcmp(v.notes_titles[0],"Groceries more"));action(s,ACTION_NOTES_NEXT);action(s,ACTION_NOTES_NEXT);action(s,ACTION_NOTES_NEXT);surface_view(s,&v);assert(!strcmp(v.notes_titles[5],"replacement"));
    action(s,ACTION_NOTES_ROW+5);key(s,TIKU_KEY_END,0);type(s," after reload");action(s,ACTION_NOTES_SAVE_ALL);notes_idle(s,&host,&media,1);
    stored("Notes/note-025.txt","# tiku-note paper=Yellow important=0\nreplacement after reload\n");
    printf("Notes host peak: %zu bytes, %zu live allocations\n",host.memory.peak,host.memory.live);
    action(s,ACTION_EDITOR_RUN);action(s,ACTION_NOTES_DELETE);action(s,ACTION_NOTES_DELETE_CONFIRMED);notes_idle(s,&host,&media,1);
    action(s,ACTION_NOTES_PREVIOUS);action(s,ACTION_NOTES_PREVIOUS);action(s,ACTION_NOTES_PREVIOUS);action(s,ACTION_NOTES_ROW);
    action(s,ACTION_EDITOR_RUN);action(s,ACTION_NOTES_DUPLICATE);surface_view(s,&v);assert(v.notes_count==24&&v.notes_selected==23);
    e=surface_notes_editor(s->active.state);surface_note_info_t info;surface_editor_note_info(e,&info);assert(info.paper==2&&info.important&&info.bytes==14&&info.dirty);
    action(s,ACTION_NOTES_SAVE_ALL);notes_idle(s,&host,&media,1);stored("Notes/note-026.txt","# tiku-note paper=Blue important=1\nGroceries more\n");
    shutdown_notes(&s,&media,&host);
    assert(!tiku_mfs_stat(&fs,"Notes/note-026.txt",&obj)&&!tiku_mfs_delete(&fs,obj.id));
    document("Notes/note-imported.txt","bad",3);
    s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_NOTES_APP));notes_idle(s,&host,&media,0);
    action(s,ACTION_NOTES_NEXT);action(s,ACTION_NOTES_NEXT);action(s,ACTION_NOTES_NEXT);surface_view(s,&v);
    assert(v.notes_count==24&&(v.notes_state[5]&8));
    assert(!tiku_mfs_stat(&fs,"Notes/note-imported.txt",&obj)&&!tiku_mfs_delete(&fs,obj.id));
    const char *recovered="# tiku-note paper=Green important=1\nRecovered\n";document("Notes/note-imported.txt",recovered,strlen(recovered));
    action(s,ACTION_NOTES_REFRESH);notes_idle(s,&host,&media,1);surface_view(s,&v);assert(v.notes_count==24&&!(v.notes_state[5]&8)&&!strcmp(v.notes_titles[5],"Recovered"));
    shutdown_notes(&s,&media,&host);
    reset();s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_NOTES_APP));notes_idle(s,&host,&media,0);action(s,ACTION_NOTES_NEW);notes_idle(s,&host,&media,0);surface_view(s,&v);assert(!v.notes_count&&!tiku_mfs_has_directories(&fs)&&v.status!=TIKU_RESOURCE_OK);shutdown_notes(&s,&media,&host);
    free(bridge);free(disk);puts("Notes: 24 retained documents, quiet autosave, metadata, dialog, history, deletion, restart and legacy refusal passed");return 0;
}
