/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_note_io.c - immutable autosave snapshots and checked native note records.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_EDITOR_MAIN editor_fixture_main
#include "test_editor.c"
/** @brief Open a native note in a retained document session. */
static surface_app_state_t *note(surface_t *s,surface_media_t *media,const char *name)
{
    assert(surface_launch(s,SURFACE_EDITOR_APP));settle(s,media);surface_app_state_t *a=s->active.state;
    #if SURFACE_EDITOR_DOCUMENTS
    a=surface_editor_session(a);
#endif
    char path[65];snprintf(path,sizeof path,"/%s",name);assert(surface_editor_note(a,path));
    tiku_mfs_object_t object;uint64_t rank,epoch;
    assert(!tiku_mfs_enumerate(&fs,name,0,0,&object,&rank,&epoch));
    assert(surface_editor_open(a,path,epoch,rank+1));settle(s,media);return a;
}
int main(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();
    const char *native="# tiku-note paper=Blue important=1\nhello\n";document("note-001.txt",native,strlen(native));
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    cfg.storage=surface_media_init(&media,bridge);cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s);surface_app_state_t *a=note(s,&media,"note-001.txt");
    surface_note_info_t info;surface_editor_note_info(a,&info);assert(info.paper==2&&info.important&&info.exists&&!info.dirty&&info.bytes==5&&!strcmp(info.title,"hello"));
    key(s,'a',TIKU_MOD_CMD);type(s,"snapshot");surface_editor_note_metadata(a,3,0);
    file("unrelated");assert(surface_editor_note_save(a));
    type(s," later");surface_editor_note_metadata(a,1,1);settle(s,&media);
    stored("note-001.txt","# tiku-note paper=Pink important=0\nsnapshot\n");
    surface_editor_note_info(a,&info);assert(info.dirty&&info.bytes==14&&info.paper==1&&info.important);
    assert(surface_editor_note_save(a));settle(s,&media);stored("note-001.txt","# tiku-note paper=Green important=1\nsnapshot later\n");
    surface_editor_note_info(a,&info);assert(!info.dirty);
    assert(surface_editor_note_save(a));type(s,"!");settle(s,&media);surface_editor_note_info(a,&info);assert(info.dirty);
    assert(surface_editor_note_save(a));settle(s,&media);surface_editor_note_info(a,&info);assert(!info.dirty);
    assert(surface_editor_note_save(a));surface_editor_note_metadata(a,0,0);settle(s,&media);surface_editor_note_info(a,&info);assert(info.dirty);
    surface_editor_note_metadata(a,1,1);assert(surface_editor_note_save(a));settle(s,&media);
    key(s,TIKU_KEY_BACKSPACE,0);assert(surface_editor_note_save(a));settle(s,&media);
    assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);settle(s,&media);a=note(s,&media,"note-001.txt");
    surface_editor_note_info(a,&info);assert(info.paper==1&&info.important&&info.bytes==14&&!info.dirty);
    type(s,"local");native="# tiku-note paper=Green important=1\nforeign input!\n";tiku_mfs_object_t foreign;assert(!tiku_mfs_stat(&fs,"note-001.txt",&foreign));assert(!tiku_mfs_delete(&fs,foreign.id));document("note-001.txt",native,strlen(native));
    assert(surface_editor_note_save(a));settle(s,&media);assert(a->status==TIKU_RESOURCE_STALE_CURSOR&&surface_editor_modified(a));stored("note-001.txt",native);
    assert(surface_editor_note_delete(a));settle(s,&media);assert(a->status==TIKU_RESOURCE_STALE_CURSOR);stored("note-001.txt",native);
    assert(surface_close(s)==SURFACE_CLOSE_CONFIRM&&surface_resolve_close(s,1));settle(s,&media);
    a=note(s,&media,"note-001.txt");assert(surface_editor_note_delete(a));settle(s,&media);surface_editor_note_info(a,&info);assert(info.deleted&&!info.dirty);
    tiku_mfs_object_t obj;assert(tiku_mfs_stat(&fs,"note-001.txt",&obj)==TIKU_MFS_NOENT);
    assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);settle(s,&media);assert(surface_launch(s,SURFACE_EDITOR_APP));settle(s,&media);a=s->active.state;
#if SURFACE_EDITOR_DOCUMENTS
    a=surface_editor_session(a);
#endif
    assert(surface_editor_note(a,"/note-002.txt")&&surface_editor_modified(a));
    assert(surface_editor_accept(a,"new"));assert(surface_editor_note_save(a));settle(s,&media);stored("note-002.txt","# tiku-note paper=Yellow important=0\nnew\n");
    surface_editor_note_metadata(a,2,1);offline=1;assert(surface_editor_note_save(a));settle(s,&media);assert(surface_editor_modified(a));offline=0;assert(!tiku_media_store_init(&store,&fs,&block,uuid,used,mark,sizeof used,NULL,NULL,NULL));assert(!tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used));
    assert(surface_editor_note_save(a));settle(s,&media);assert(a->status==TIKU_RESOURCE_OK);stored("note-002.txt","# tiku-note paper=Blue important=1\nnew\n");
    surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(&media);}assert(!s&&!host.memory.live&&!store.count&&!store.running);
    free(bridge);free(disk);puts("Notes I/O: native metadata, retained typing, checked autosave, restart, conflict, deletion and offline retry passed");return 0;
}
