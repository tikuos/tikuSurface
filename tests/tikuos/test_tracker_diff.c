/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_tracker_diff.c - identity-bound comparison handoff from Tracker.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_DOCUMENTS_MAIN documents_fixture_main
#include "test_documents.c"
#undef SURFACE_DOCUMENTS_MAIN
/** @brief Enter the comparison side chooser from the file handler menu. */
static void choose(surface_t *s,surface_media_t *m,unsigned side)
{
    file_tap(s,m,680,128);file_tap(s,m,220,390);
    surface_view_t v;surface_view(s,&v);assert(v.document_menu==2);
    painting(s);key(s,'1'+side,0);
}
int main(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();
    document("a.txt","same\nleft\n",10);document("b.txt","same\nright\n",11);
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    cfg.storage=surface_media_init(&media,bridge);cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s);surface_view_t v=select_file(s,&media,"a.txt");
    choose(s,&media,0);v=view(s,&media);assert(v.foreground==SURFACE_DIFF_APP&&v.diff_loaded==1&&!strcmp(v.diff_path[0],"/a.txt"));
    v=select_file(s,&media,"b.txt");choose(s,&media,1);v=view(s,&media);
    assert(v.diff_ready&&!v.diff_equal&&v.diff_removed==1&&v.diff_added==1&&!strcmp(v.diff_path[1],"/b.txt"));
    v=select_file(s,&media,"a.txt");file_tap(s,&media,680,128);file_tap(s,&media,220,390);key(s,TIKU_KEY_ESCAPE,0);
    surface_view(s,&v);assert(!v.document_menu&&v.foreground==SURFACE_BROWSER);
    choose(s,&media,1);v=view(s,&media);assert(v.diff_equal&&!strcmp(v.diff_path[1],"/a.txt"));
    v=select_file(s,&media,"b.txt");document("foreign","changed",7);choose(s,&media,1);v=view(s,&media);
    assert(v.foreground==SURFACE_DIFF_APP&&v.status==TIKU_RESOURCE_STALE_CURSOR&&!strcmp(v.diff_path[1],"/a.txt")&&v.diff_equal);
    v=select_file(s,&media,"b.txt");surface_app_state_t *d=s->background[SURFACE_DIFF_APP].state;
    assert(surface_diff_open(d,1,"/b.txt"));
    assert(surface_document_action(s,ACTION_DOCUMENT_WITH));assert(surface_document_action(s,ACTION_DOCUMENT_COMPARE));
    assert(surface_document_action(s,ACTION_DOCUMENT_CHOICE));surface_view(s,&v);
    assert(v.foreground==SURFACE_BROWSER&&v.document_error==SURFACE_DIFF_APP&&!v.document_menu);settle(s,&media);
    assert(surface_launch(s,SURFACE_DIFF_APP));v=view(s,&media);assert(!strcmp(v.diff_path[0],"/a.txt")&&!strcmp(v.diff_path[1],"/b.txt"));
    assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);settle(s,&media);
    v=select_file(s,&media,"a.txt");host.memory.fail_at=host.memory.calls+1;choose(s,&media,0);v=view(s,&media);
    assert(v.foreground==SURFACE_HOME&&v.document_error==SURFACE_DIFF_APP&&!s->open_path[0]);host.memory.fail_at=0;
    surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(&media);}assert(!s&&!host.memory.live&&!store.count&&!store.running);
    free(bridge);free(disk);puts("Tracker Diff: side selection, retained pair, stale identity, busy target, cancel, clipped paint and failed launch passed");return 0;
}
