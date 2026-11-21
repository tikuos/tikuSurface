/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_tracker_directories.c - persistent folders through the Tracker controls.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main media_fixture_main
#include "tiku_event.h"
#include "test_media.c"
#undef main
/** @brief Dispatch a key and complete its owner turn. */
static void key(surface_t *s,unsigned value,unsigned modifiers)
{tiku_input_event_t e={.type=TIKU_EVENT_KEY_DOWN,.key=value,.modifiers=modifiers};assert(surface_post(s,&e));surface_step(s);}
/** @brief Replace a filename using complete individual character events. */
static void enter(surface_t *s,const char *text)
{key(s,'a',TIKU_MOD_CMD);while(*text)key(s,(unsigned char)*text++,0);key(s,TIKU_KEY_RETURN,0);}
/** @brief Read the current Tracker presentation after storage retirement. */
static surface_view_t observed(surface_t *s,surface_media_t *m)
{settle(s,m);surface_view_t v;surface_view(s,&v);return v;}
/** @brief Create a folder through the visible path dialog. */
static surface_view_t mkdir_ui(surface_t *s,surface_media_t *m,const char *path)
{file_tap(s,m,350,410);surface_view_t v;surface_view(s,&v);assert(v.text_entry&&v.file_dialog==4);enter(s,path);return observed(s,m);}
/** @brief Close the application and retire any filesystem or transport lease. */
static void cleanup(surface_t **s,surface_media_t *m,surface_host_t *h)
{
    surface_stop(*s);for(unsigned i=0;i<30000&&!surface_destroy(s);++i){surface_step(*s);surface_media_poll(m);}
    assert(!*s&&!h->memory.live&&!store.count&&!store.running);
}
#ifndef SURFACE_TREE_COPY_FIXTURE
#define TRACKER_DIRECTORIES_MAIN main
#else
#define TRACKER_DIRECTORIES_MAIN tracker_directories_fixture_main
#endif
int TRACKER_DIRECTORIES_MAIN(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};
    reset();memset(disk,0,DISK_BYTES);tiku_mfs_init(&fs);assert(!tiku_mfs_format_directories(&fs,&block,uuid,used,mark,sizeof used));
    surface_host_t h;surface_config_t cfg=surface_host_config(&h);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);cfg.storage=surface_media_init(&media,bridge);
    cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));surface_view_t v=observed(s,&media);
    v=mkdir_ui(s,&media,"/empty");assert(v.status==TIKU_RESOURCE_OK&&v.row_count==1&&v.directory[0]&&!strcmp(v.rows[0],"empty"));
    tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"empty",&o)&&o.policy==TIKU_MFS_DIRECTORY);
    file_tap(s,&media,70,130);v=observed(s,&media);assert(!strcmp(v.path,"/empty")&&!v.row_count&&v.status==TIKU_RESOURCE_OK);
    key(s,TIKU_KEY_ESCAPE,0);v=observed(s,&media);assert(!strcmp(v.path,"/"));
    file_tap(s,&media,170,410);file_tap(s,&media,70,130);v=observed(s,&media);
    assert(v.reading&&v.file_directory&&v.file_actions);
#if SURFACE_EDITOR
    assert(!v.file_open);
#endif
    file_capture(s,&h,"folder-selection");
    file_tap(s,&media,460,410);enter(s,"/moved");v=observed(s,&media);assert(v.row_count==1&&!strcmp(v.rows[0],"moved")&&v.directory[0]);
    assert(tiku_mfs_stat(&fs,"empty",&o)==TIKU_MFS_NOENT&&!tiku_mfs_stat(&fs,"moved",&o)&&o.policy==TIKU_MFS_DIRECTORY);
    file_tap(s,&media,170,410);file_tap(s,&media,70,130);file_tap(s,&media,660,410);surface_view(s,&v);assert(v.close_confirmation==2);
    assert(surface_resolve_close(s,0));assert(!tiku_mfs_stat(&fs,"moved",&o));
    file_tap(s,&media,660,410);assert(surface_resolve_close(s,1));v=observed(s,&media);assert(!v.row_count);
    v=mkdir_ui(s,&media,"/a");assert(v.directory[0]);file_tap(s,&media,70,130);v=observed(s,&media);
    file_tap(s,&media,260,410);enter(s,"/a/file");v=observed(s,&media);assert(v.row_count==1&&!v.directory[0]);
    key(s,TIKU_KEY_ESCAPE,0);v=observed(s,&media);assert(v.row_count==1&&v.directory[0]);
    file_tap(s,&media,170,410);file_tap(s,&media,70,130);file_tap(s,&media,660,410);assert(surface_resolve_close(s,1));v=observed(s,&media);
    assert(v.status==TIKU_RESOURCE_OK&&tiku_mfs_stat(&fs,"a/file",&o)==TIKU_MFS_NOENT);
    v=mkdir_ui(s,&media,"/a");file_tap(s,&media,70,130);v=observed(s,&media);
    file_tap(s,&media,260,410);enter(s,"/a/file");v=observed(s,&media);key(s,TIKU_KEY_ESCAPE,0);v=observed(s,&media);
    assert(!v.reading);v=mkdir_ui(s,&media,"/a/file/child");assert(v.status==TIKU_RESOURCE_BUSY);
    file_tap(s,&media,70,410);v=observed(s,&media);char longest[65];longest[0]='/';memset(longest+1,'z',63);longest[64]=0;
    v=mkdir_ui(s,&media,longest);assert(v.status==TIKU_RESOURCE_OK&&v.row_count==2&&v.directory[1]);
    file_tap(s,&media,70,174);v=observed(s,&media);assert(!strcmp(v.path,longest)&&!v.row_count&&v.status==TIKU_RESOURCE_OK);
    key(s,TIKU_KEY_ESCAPE,0);v=observed(s,&media);assert(v.row_count==2);file_capture(s,&h,"persistent-folders");
    assert(!tiku_mfs_checkpoint(&fs));cleanup(&s,&media,&h);assert(!tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used));
    assert(!tiku_mfs_stat(&fs,longest+1,&o)&&o.policy==TIKU_MFS_DIRECTORY);
    cfg.caller.scopes=TIKU_RESOURCE_READ;surface_os_media_init(bridge);cfg.storage=surface_media_init(&media,bridge);
    s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));v=observed(s,&media);assert(!v.file_create&&!v.media_writable);
    file_tap(s,&media,350,410);surface_view(s,&v);assert(!v.text_entry);cleanup(&s,&media,&h);
    reset();file("kept");surface_os_media_init(bridge);cfg.storage=surface_media_init(&media,bridge);cfg.caller.scopes|=TIKU_RESOURCE_WRITE;
    s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));v=observed(s,&media);v=mkdir_ui(s,&media,"/nope");
    assert(v.status==TIKU_RESOURCE_UNSUPPORTED&&!tiku_mfs_stat(&fs,"kept",&o)&&tiku_mfs_stat(&fs,"nope",&o)==TIKU_MFS_NOENT);
    cleanup(&s,&media,&h);reset();file("old/a");file("old/deep/b");file("old-sibling");
    surface_os_media_init(bridge);cfg.storage=surface_media_init(&media,bridge);
    s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));v=observed(s,&media);
    unsigned row=0;while(row<v.row_count&&strcmp(v.rows[row],"old"))++row;assert(row<v.row_count);
    file_tap(s,&media,170,410);file_tap(s,&media,70,130+44*row);v=observed(s,&media);assert(v.file_directory);
    file_tap(s,&media,460,410);enter(s,"/new");v=observed(s,&media);assert(v.status==TIKU_RESOURCE_OK);
    assert(tiku_mfs_stat(&fs,"old/a",&o)==TIKU_MFS_NOENT&&!tiku_mfs_stat(&fs,"new/deep/b",&o));
    row=0;while(row<v.row_count&&strcmp(v.rows[row],"new"))++row;assert(row<v.row_count);
    file_tap(s,&media,170,410);file_tap(s,&media,70,130+44*row);file_tap(s,&media,660,410);file("concurrent");
    assert(surface_resolve_close(s,1));v=observed(s,&media);assert(v.status==TIKU_RESOURCE_STALE_CURSOR&&!tiku_mfs_stat(&fs,"new/a",&o));
    file_tap(s,&media,70,410);v=observed(s,&media);row=0;while(row<v.row_count&&strcmp(v.rows[row],"new"))++row;assert(row<v.row_count);
    file_tap(s,&media,170,410);file_tap(s,&media,70,130+44*row);file_tap(s,&media,660,410);assert(surface_resolve_close(s,0));
    assert(!tiku_mfs_stat(&fs,"new/a",&o));file_tap(s,&media,660,410);assert(surface_resolve_close(s,1));v=observed(s,&media);
    assert(v.status==TIKU_RESOURCE_OK&&tiku_mfs_stat(&fs,"new/a",&o)==TIKU_MFS_NOENT&&tiku_mfs_stat(&fs,"new/deep/b",&o)==TIKU_MFS_NOENT);
    assert(!tiku_mfs_stat(&fs,"old-sibling",&o)&&!tiku_mfs_stat(&fs,"concurrent",&o));
    cleanup(&s,&media,&h);
    for(unsigned stop=0;stop<60;++stop){
        reset();file("cancel/a");file("cancel/deep/b");surface_os_media_init(bridge);cfg.storage=surface_media_init(&media,bridge);
        tiku_media_request_t denied={.op=TIKU_MEDIA_TREE_DELETE,.client=1,.permissions=TIKU_MEDIA_READ,.name="cancel",.names_epoch=fs.names_epoch};
        assert(tiku_media_store_submit(&store,&denied)==TIKU_MFS_ARG&&!store.count);
        s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));v=observed(s,&media);
        file_tap(s,&media,170,410);file_tap(s,&media,70,130);file_tap(s,&media,460,410);enter(s,"/done");
        for(unsigned i=0;i<stop*29;++i){surface_step(s);surface_media_poll(&media);}
        cleanup(&s,&media,&h);assert(!tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used));
        int old=tiku_mfs_stat(&fs,"cancel/a",&o),now=tiku_mfs_stat(&fs,"done/a",&o);
        assert((!old&&now==TIKU_MFS_NOENT)||(old==TIKU_MFS_NOENT&&!now));
        assert(tiku_mfs_stat(&fs,"cancel/deep/b",&o)==old&&tiku_mfs_stat(&fs,"done/deep/b",&o)==now);
    }
    free(bridge);free(disk);
    puts("Tracker folders: create, typed enumeration, enter/select, rename/delete, recursive deletion, implicit subtree rename/delete, stale epochs, long paths, remount, permissions and legacy-volume preservation passed");return 0;
}
