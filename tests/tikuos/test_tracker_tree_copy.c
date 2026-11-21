/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_tracker_tree_copy.c - folder copy controls, progress and retained cancellation.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_TREE_COPY_FIXTURE 1
#include "test_tracker_directories.c"
/** @brief Create source contents outside an active queued request. */
static void payload_file(const char *path)
{
    uint64_t id;assert(!tiku_mfs_begin(&fs,path,TIKU_MFS_STREAM,0,NULL,&id));
    assert(!tiku_mfs_append(&fs,"folder contents",15));assert(!tiku_mfs_finalize(&fs));
}
/** @brief Select a folder and open its copy destination control. */
static void select_copy(surface_t *s,surface_media_t *m,const char *name)
{
    surface_view_t v=observed(s,m);unsigned row=0;
    while(row<v.row_count&&strcmp(v.rows[row],name))++row;
    assert(row<v.row_count);
    file_tap(s,m,170,410);file_tap(s,m,70,130+44*row);v=observed(s,m);
    assert(v.reading&&v.file_directory&&v.file_actions);file_tap(s,m,300,410);surface_view(s,&v);
    assert(v.text_entry&&v.file_dialog==3);
}
/** @brief Require source preservation and atomic destination namespace after remount. */
static void copied(void)
{
    tiku_mfs_object_t a,b;assert(!tiku_mfs_stat(&fs,"source/a",&a));assert(!tiku_mfs_stat(&fs,"source/deep/b",&a));
    int rc=tiku_mfs_stat(&fs,"copy/a",&b);assert(!rc||rc==TIKU_MFS_NOENT);
    assert(tiku_mfs_stat(&fs,"copy/deep/b",&b)==rc);
    if(!rc){uint64_t reader;unsigned char bytes[15];assert(!tiku_mfs_open_read(&fs,"copy/deep/b",&reader));
        assert(tiku_mfs_read_at(&fs,reader,0,bytes,15)==15&&!memcmp(bytes,"folder contents",15));assert(!tiku_mfs_close_read(&fs,reader));}
}
int main(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};
    surface_host_t h;surface_config_t cfg=surface_host_config(&h);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);
    cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    for(unsigned typed=0;typed<2;++typed){reset();
        if(typed){memset(disk,0,DISK_BYTES);tiku_mfs_init(&fs);assert(!tiku_mfs_format_directories(&fs,&block,uuid,used,mark,sizeof used));}
        surface_os_media_init(bridge);cfg.storage=surface_media_init(&media,bridge);
        surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));observed(s,&media);
        if(typed){mkdir_ui(s,&media,"/source");mkdir_ui(s,&media,"/source/empty");}
        payload_file("source/a");payload_file("source/deep/b");file("source-sibling");
        file_tap(s,&media,70,410);observed(s,&media);select_copy(s,&media,"source");enter(s,"/copy");
        uint64_t prepared=0,total=0;unsigned progress_seen=0;surface_view_t v;
        for(unsigned i=0;i<30000;++i){surface_step(s);surface_media_poll(&media);surface_view(s,&v);
            if(v.file_job==10){assert(v.copy_prepared>=prepared&&v.copy_total>=total&&v.copy_prepared<=v.copy_total);
                prepared=v.copy_prepared;total=v.copy_total;
                if(prepared&&prepared<total&&!progress_seen){progress_seen=1;file_capture(s,&h,"folder-copy-progress");}}
            if(!media.request&&v.status!=TIKU_RESOURCE_PENDING)break;
            assert(i<29999);
        }
        assert(progress_seen&&total==30&&v.status==TIKU_RESOURCE_OK);copied();tiku_mfs_object_t o;
        assert(!tiku_mfs_stat(&fs,"copy/a",&o));if(typed)assert(!tiku_mfs_stat(&fs,"copy/empty",&o)&&o.policy==TIKU_MFS_DIRECTORY);
        select_copy(s,&media,"source");enter(s,"/copy");v=observed(s,&media);assert(v.status==TIKU_RESOURCE_BUSY);
        cleanup(&s,&media,&h);assert(!tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used));copied();
    }
    for(unsigned stop=0;stop<60;++stop){reset();payload_file("source/a");payload_file("source/deep/b");
        surface_os_media_init(bridge);cfg.storage=surface_media_init(&media,bridge);
        tiku_media_request_t denied={.op=TIKU_MEDIA_TREE_COPY,.client=1,.permissions=TIKU_MEDIA_WRITE,.name="source",.destination="copy",.names_epoch=fs.names_epoch};
        assert(tiku_media_store_submit(&store,&denied)==TIKU_MFS_ARG&&!store.count);denied.permissions=TIKU_MEDIA_READ;
        assert(tiku_media_store_submit(&store,&denied)==TIKU_MFS_ARG&&!store.count);
        surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));observed(s,&media);
        select_copy(s,&media,"source");enter(s,"/copy");
        for(unsigned i=0;i<stop*47;++i){surface_step(s);surface_media_poll(&media);}
        close_refusals=3;cleanup(&s,&media,&h);assert(!tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used));copied();
    }
    reset();payload_file("source/a");payload_file("source/deep/b");surface_os_media_init(bridge);cfg.storage=surface_media_init(&media,bridge);
    cfg.caller.scopes=TIKU_RESOURCE_WRITE;surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));
    observed(s,&media);select_copy(s,&media,"source");enter(s,"/copy");surface_view_t v=observed(s,&media);
    assert(v.status==TIKU_RESOURCE_DENIED);cleanup(&s,&media,&h);
    free(bridge);free(disk);puts("Tracker tree copy: typed/implicit contents, empty folders, progress, conflicts, permissions and 60 delayed-close positions passed");return 0;
}
