/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_file_information.c - full file metadata beside a bounded preview.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main media_fixture_main
#include "test_media.c"
#undef main
int main(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();
    static unsigned char contents[1700];memset(contents,'x',sizeof contents);uint64_t id;
    assert(!tiku_mfs_begin(&fs,"document",TIKU_MFS_STREAM,0,NULL,&id));assert(!tiku_mfs_append(&fs,contents,sizeof contents));assert(!tiku_mfs_finalize(&fs));
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);cfg.storage=surface_media_init(&media,bridge);
    cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));settle(s,&media);file_tap(s,&media,70,130);settle(s,&media);
    surface_view_t v;surface_view(s,&v);assert(v.reading&&v.preview_bytes==1024&&v.file_info_known&&v.file_length==1700&&v.file_version==id);
    file_tap(s,&media,300,360);surface_view(s,&v);assert(v.file_details&&v.file_can_write&&v.file_epoch==tiku_mfs_names_epoch(&fs));
    file_capture(s,&host,"file-information");
    s->config.caller.scopes=TIKU_RESOURCE_READ;surface_view(s,&v);assert(!v.file_can_write&&!v.file_actions&&v.file_length==1700);
    file_tap(s,&media,300,360);surface_view(s,&v);assert(!v.file_details&&v.preview_bytes==1024);
    file("concurrent");file_tap(s,&media,70,410);settle(s,&media);surface_view(s,&v);
    assert(v.status==TIKU_RESOURCE_OK&&!v.reading&&!v.file_details&&!v.file_info_known&&v.row_count==2);
    surface_stop(s);for(unsigned n=0;n<30000&&!surface_destroy(&s);++n){surface_step(s);surface_media_poll(&media);}
    assert(!s&&!host.memory.live);free(bridge);free(disk);
    puts("Tracker information: full size beyond preview, content version, catalog epoch, scoped access and refresh invalidation passed");return 0;
}
