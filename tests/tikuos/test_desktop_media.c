/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_desktop_media.c - desktop Tracker journeys on delayed writable media.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_TREE_COPY_FIXTURE 1
#include "test_tracker_directories.c"
#include "tiku_window.h"
/** @brief Current Tracker content geometry from its native frame. */
static tiku_rect_t body(surface_t *s)
{ tiku_window_t w={0};assert(surface_desktop_geometry(s,SURFACE_BROWSER,&w.frame,NULL));return tiku_window_content(&w); }
/** @brief Activate a content-relative control through pointer input. */
static void tap(surface_t *s,surface_media_t *m,int x,int y)
{ tiku_rect_t r=body(s);file_tap(s,m,r.x+x,r.y+y); }
/** @brief Select a visible named item and open its file-operation presentation. */
static surface_view_t selected(surface_t *s,surface_media_t *m,const char *name)
{
    surface_view_t v=observed(s,m);unsigned i=0;while(i<v.row_count&&strcmp(v.rows[i],name))++i;assert(i<v.row_count);
    tap(s,m,24,80+(int)i*28);tiku_rect_t r=body(s);tap(s,m,260,r.h-20);v=observed(s,m);
    assert(v.reading&&v.file_actions);return v;
}
/** @brief Create a persistent folder from the desktop Tracker toolbar. */
static surface_view_t folder(surface_t *s,surface_media_t *m,const char *path)
{ tap(s,m,270,18);surface_view_t v;surface_view(s,&v);assert(v.text_entry&&v.file_dialog==4);enter(s,path);return observed(s,m); }
/** @brief Copy a selected item using the destination field. */
static surface_view_t copy_to(surface_t *s,surface_media_t *m,const char *path)
{ tiku_rect_t r=body(s);tap(s,m,330,r.h-54);surface_view_t v;surface_view(s,&v);assert(v.text_entry&&v.file_dialog==3);enter(s,path);return observed(s,m); }
int main(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();
    memset(disk,0,DISK_BYTES);tiku_mfs_init(&fs);assert(!tiku_mfs_format_directories(&fs,&block,uuid,used,mark,sizeof used));
    surface_host_t h;surface_config_t cfg=surface_host_config(&h);surface_media_t m;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);cfg.storage=surface_media_init(&m,bridge);
    cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));surface_view_t v=observed(s,&m);assert(v.file_create);
    v=folder(s,&m,"/source");assert(v.status==TIKU_RESOURCE_OK);v=folder(s,&m,"/source/empty");assert(v.status==TIKU_RESOURCE_OK);
    uint64_t id;assert(!tiku_mfs_begin(&fs,"source/payload",TIKU_MFS_STREAM,0,NULL,&id));assert(!tiku_mfs_append(&fs,"desktop payload",15));assert(!tiku_mfs_finalize(&fs));
    tap(s,&m,100,18);v=observed(s,&m);assert(v.row_count==1);v=selected(s,&m,"source");assert(v.file_directory);
    v=copy_to(s,&m,"/copy");assert(v.status==TIKU_RESOURCE_OK);tiku_mfs_object_t o;
    assert(!tiku_mfs_stat(&fs,"copy/empty",&o)&&o.policy==TIKU_MFS_DIRECTORY);assert(!tiku_mfs_stat(&fs,"copy/payload",&o));
    uint64_t reader;char bytes[15];assert(!tiku_mfs_open_read(&fs,"copy/payload",&reader));assert(tiku_mfs_read_at(&fs,reader,0,bytes,15)==15&&!memcmp(bytes,"desktop payload",15));assert(!tiku_mfs_close_read(&fs,reader));
    v=selected(s,&m,"copy");tiku_rect_t r=body(s);tap(s,&m,50,r.h-54);surface_view(s,&v);assert(v.text_entry);enter(s,"/renamed");v=observed(s,&m);assert(v.status==TIKU_RESOURCE_OK);
    assert(tiku_mfs_stat(&fs,"copy/payload",&o)==TIKU_MFS_NOENT&&!tiku_mfs_stat(&fs,"renamed/payload",&o));
    v=selected(s,&m,"renamed");tap(s,&m,180,r.h-54);surface_view(s,&v);assert(v.close_confirmation==2);file_capture(s,&h,"core-delete-confirmation");
    file_tap(s,&m,270,290);assert(!tiku_mfs_stat(&fs,"renamed/payload",&o));
    tap(s,&m,180,r.h-54);file("concurrent");file_tap(s,&m,450,290);v=observed(s,&m);assert(v.status==TIKU_RESOURCE_STALE_CURSOR&&!tiku_mfs_stat(&fs,"renamed/payload",&o));
    tap(s,&m,100,18);v=observed(s,&m);v=selected(s,&m,"renamed");tap(s,&m,180,r.h-54);file_tap(s,&m,450,290);v=observed(s,&m);
    assert(v.status==TIKU_RESOURCE_OK&&tiku_mfs_stat(&fs,"renamed/payload",&o)==TIKU_MFS_NOENT&&!tiku_mfs_stat(&fs,"source/payload",&o));
    tap(s,&m,180,18);surface_view(s,&v);assert(v.text_entry&&v.file_dialog==2);enter(s,"/new.txt");v=observed(s,&m);assert(v.status==TIKU_RESOURCE_OK&&!tiku_mfs_stat(&fs,"new.txt",&o));
    file_capture(s,&h,"core-writable-tracker");
    cleanup(&s,&m,&h);assert(!tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used));assert(!tiku_mfs_stat(&fs,"new.txt",&o));
    surface_os_media_init(bridge);cfg.storage=surface_media_init(&m,bridge);cfg.caller.scopes=TIKU_RESOURCE_READ;
    s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));v=observed(s,&m);assert(!v.file_create);
    tap(s,&m,180,18);surface_view(s,&v);assert(!v.text_entry);cleanup(&s,&m,&h);
    free(bridge);free(disk);puts("PASS: desktop Tracker creates, recursively copies, moves, confirms deletion, rejects stale writes and remounts");return 0;
}
