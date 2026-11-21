/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_tracker_directory_upgrade.c - informed volume-feature consent and scoped progression.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main media_fixture_main
#include "tiku_event.h"
#include "test_media.c"
#undef main
/** @brief Replace the folder-name draft with individual owner-dispatched keys. */
static void name_input(surface_t *s,const char *name)
{
    tiku_input_event_t e={.type=TIKU_EVENT_KEY_DOWN,.key='a',.modifiers=TIKU_MOD_CMD};assert(surface_post(s,&e));surface_step(s);
    for(;*name;++name){e=(tiku_input_event_t){.type=TIKU_EVENT_KEY_DOWN,.key=(unsigned char)*name};assert(surface_post(s,&e));surface_step(s);}
    e=(tiku_input_event_t){.type=TIKU_EVENT_KEY_DOWN,.key=TIKU_KEY_RETURN};assert(surface_post(s,&e));surface_step(s);
}
/** @brief Observe a retired storage operation. */
static surface_view_t observe(surface_t *s,surface_media_t *m)
{settle(s,m);surface_view_t v;surface_view(s,&v);return v;}
/** @brief Request a folder and observe the legacy-volume feature refusal. */
static surface_view_t request_folder(surface_t *s,surface_media_t *m)
{file_tap(s,m,350,410);name_input(s,"/new-folder");return observe(s,m);}
/** @brief Drain teardown without abandoning a transport-owned sector. */
static void shutdown_ui(surface_t **s,surface_media_t *m,surface_host_t *h)
{
    surface_stop(*s);for(unsigned i=0;i<30000&&!surface_destroy(s);++i){surface_step(*s);surface_media_poll(m);}
    assert(!*s&&!h->memory.live&&!store.count&&!store.running);
}
int main(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();file("kept");
    surface_host_t h;surface_config_t cfg=surface_host_config(&h);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);cfg.storage=surface_media_init(&media,bridge);
    cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE|TIKU_RESOURCE_HOST_CONTROL};
    surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));surface_view_t v=observe(s,&media);
    v=request_folder(s,&media);assert(v.status==TIKU_RESOURCE_UNSUPPORTED&&v.file_upgrade&&!v.file_create);
    tiku_mfs_object_t o;assert(!tiku_mfs_has_directories(&fs)&&!tiku_mfs_stat(&fs,"kept",&o));
    file_tap(s,&media,350,410);surface_view(s,&v);assert(v.close_confirmation==4);file_capture(s,&h,"enable-folders");
    assert(!surface_launch(s,SURFACE_HOME));assert(surface_resolve_close(s,0));assert(!tiku_mfs_has_directories(&fs));
    file_tap(s,&media,350,410);file("concurrent");assert(surface_resolve_close(s,1));v=observe(s,&media);
    assert(v.status==TIKU_RESOURCE_STALE_CURSOR&&!tiku_mfs_has_directories(&fs)&&!tiku_mfs_stat(&fs,"kept",&o));
    file_tap(s,&media,70,410);v=observe(s,&media);assert(v.file_upgrade);
    file_tap(s,&media,350,410);assert(surface_resolve_close(s,1));assert(surface_launch(s,SURFACE_HOME));settle(s,&media);
    assert(tiku_mfs_has_directories(&fs));assert(surface_launch(s,SURFACE_BROWSER));v=observe(s,&media);assert(!v.file_upgrade&&v.row_count==2);
    v=request_folder(s,&media);assert(v.status==TIKU_RESOURCE_OK&&!tiku_mfs_stat(&fs,"new-folder",&o)&&o.policy==TIKU_MFS_DIRECTORY);
    assert(!tiku_mfs_stat(&fs,"kept",&o)&&!tiku_mfs_stat(&fs,"concurrent",&o));shutdown_ui(&s,&media,&h);
    reset();file("kept");cfg.caller.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE;
    surface_os_media_init(bridge);cfg.storage=surface_media_init(&media,bridge);s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));v=observe(s,&media);
    v=request_folder(s,&media);assert(v.status==TIKU_RESOURCE_UNSUPPORTED&&!v.file_upgrade);file_tap(s,&media,350,410);surface_view(s,&v);assert(!v.close_confirmation);
    assert(tiku_paged_storage_directory_upgrade(s->active.state->store,fs.names_epoch)==TIKU_RESOURCE_DENIED);
    tiku_media_request_t denied={.op=TIKU_MEDIA_DIRECTORY_UPGRADE,.client=1,.permissions=TIKU_MEDIA_WRITE,.names_epoch=fs.names_epoch};
    assert(tiku_media_store_submit(&store,&denied)==TIKU_MFS_ARG&&!store.count&&!tiku_mfs_has_directories(&fs));shutdown_ui(&s,&media,&h);
    cfg.caller.scopes|=TIKU_RESOURCE_HOST_CONTROL;
    for(unsigned stop=0;stop<40;++stop){
        reset();file("kept");surface_os_media_init(bridge);cfg.storage=surface_media_init(&media,bridge);
        s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));v=observe(s,&media);v=request_folder(s,&media);assert(v.file_upgrade);
        file_tap(s,&media,350,410);assert(surface_resolve_close(s,1));
        for(unsigned i=0;i<stop*3;++i){surface_step(s);surface_media_poll(&media);}
        shutdown_ui(&s,&media,&h);assert(!tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used));assert(!tiku_mfs_stat(&fs,"kept",&o));store.failed=0;
        surface_os_media_init(bridge);cfg.storage=surface_media_init(&media,bridge);s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));v=observe(s,&media);
        if(!tiku_mfs_has_directories(&fs)){v=request_folder(s,&media);assert(v.file_upgrade);file_tap(s,&media,350,410);assert(surface_resolve_close(s,1));v=observe(s,&media);assert(v.status==TIKU_RESOURCE_OK);}
        assert(tiku_mfs_has_directories(&fs)&&!tiku_mfs_stat(&fs,"kept",&o));shutdown_ui(&s,&media,&h);
    }
    free(bridge);free(disk);puts("Tracker migration: consent, Keep, stale catalog, background ownership, host/admin scope, preserved files and 40 interrupted/resumed UI positions passed");return 0;
}
