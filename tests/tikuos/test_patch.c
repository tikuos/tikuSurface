/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_patch.c - native patch output, create-only publication and borrowed-buffer retirement.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_DIFF_MAIN diff_fixture_main
#include "test_diff.c"
/** @brief Read an entire persisted patch through a snapshot reader. */
static size_t read_patch(const char *name,char *out,size_t capacity)
{
    tiku_mfs_object_t object;uint64_t reader;assert(!tiku_mfs_stat(&fs,name,&object)&&object.length<capacity);
    assert(!tiku_mfs_open_read(&fs,name,&reader));int n=tiku_mfs_read_at(&fs,reader,0,out,capacity-1);assert(n==(int)object.length);out[n]=0;assert(!tiku_mfs_close_read(&fs,reader));return (size_t)n;
}
int main(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();
    document("left","first\nleft\nlast\n",16);document("right","first\nright\nlast\n",17);document("existing","preserved",9);
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    cfg.storage=surface_media_init(&media,bridge);cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_DIFF_APP));surface_step(s);
    file_tap(s,&media,100,110);key(s,TIKU_KEY_ESCAPE,0);assert(!surface_diff_busy(s->active.state));
    file_tap(s,&media,100,110);file_name(s,"/left");(void)view(s,&media);assert(!surface_diff_busy(s->active.state));
    assert(surface_diff_open(s->active.state,1,"/right"));surface_view_t v=view(s,&media);assert(v.diff_ready);
    const char *expected="--- /left\n+++ /right\n@@ -1,3 +1,3 @@\n first\n-left\n+right\n last\n";
    key(s,'c',TIKU_MOD_CMD);v=view(s,&media);assert(v.diff_patch_result==2&&!strcmp(s->clipboard,expected));
    file_tap(s,&media,680,366);surface_view(s,&v);assert(v.diff_tools);painting(s,&host,"patch-tools");
    file_tap(s,&media,100,150);surface_view(s,&v);assert(v.text_entry&&v.diff_dialog==3);painting(s,&host,"patch-save");file_name(s,"/patch");v=view(s,&media);assert(v.diff_patch_result==1&&v.diff_ready);
    char actual[32769];assert(read_patch("patch",actual,sizeof actual)==strlen(expected)&&!strcmp(actual,expected));
    assert(surface_app_back(s->active.state));surface_view(s,&v);assert(!v.diff_tools);
    assert(surface_diff_export(s->active.state,"/existing"));v=view(s,&media);assert(v.status!=TIKU_RESOURCE_OK&&v.diff_ready);assert(read_patch("existing",actual,sizeof actual)==9&&!strcmp(actual,"preserved"));
    host.memory.fail_at=host.memory.calls+1;assert(!surface_diff_export(s->active.state,"/memory.patch"));host.memory.fail_at=0;surface_view(s,&v);assert(v.diff_ready&&!v.diff_busy);
    char left[8192],right[8192];for(unsigned i=0;i<8192;++i){left[i]=i%32==31?'\n':'x';right[i]=i%32==31?'\n':'y';}
    document("big-left",left,sizeof left);document("big-right",right,sizeof right);
    assert(surface_diff_open(s->active.state,0,"/big-left"));(void)view(s,&media);assert(surface_diff_open(s->active.state,1,"/big-right"));v=view(s,&media);assert(v.diff_ready);
    strcpy(s->clipboard,"keep this");assert(surface_diff_export(s->active.state,NULL));v=view(s,&media);assert(v.diff_patch_result==3&&v.status==TIKU_RESOURCE_LIMIT&&!strcmp(s->clipboard,"keep this"));
    assert(surface_diff_export(s->active.state,"/big.patch"));assert(!surface_diff_open(s->active.state,0,"/left")&&!surface_diff_export(s->active.state,"/overlap"));
    v=view(s,&media);assert(v.diff_patch_result==1);size_t bytes=read_patch("big.patch",actual,sizeof actual);assert(bytes>16384&&bytes<32768&&!strncmp(actual,"--- /big-left\n+++ /big-right\n",28));
    assert(surface_diff_export(s->active.state,NULL));surface_step(s);surface_diff_action(s->active.state,ACTION_DIFF_CANCEL);v=view(s,&media);assert(v.status==TIKU_RESOURCE_CANCELLED&&!strcmp(s->clipboard,"keep this"));
    document("binary","a\0b",3);assert(surface_diff_open(s->active.state,0,"/binary"));v=view(s,&media);assert(v.diff_binary&&!surface_diff_export(s->active.state,"/binary.patch"));surface_view(s,&v);assert(v.diff_patch_result==4);
    assert(surface_diff_open(s->active.state,0,"/left"));(void)view(s,&media);assert(surface_diff_open(s->active.state,1,"/left"));(void)view(s,&media);assert(surface_diff_export(s->active.state,NULL));v=view(s,&media);assert(v.diff_patch_result==5&&!strcmp(s->clipboard,"keep this"));
    assert(surface_diff_export(s->active.state,"/empty.patch"));(void)view(s,&media);assert(!read_patch("empty.patch",actual,sizeof actual));
    assert(surface_diff_open(s->active.state,1,"/right"));(void)view(s,&media);assert(surface_diff_export(s->active.state,"/cancel.patch"));
    unsigned n;for(n=0;n<30000;++n){surface_step(s);surface_media_poll(&media);if(media.request&&media.request->operation==TIKU_STORAGE_REPLACE&&store.running==4)break;}assert(n<30000);
    assert(media.request->input_bytes==strlen(expected)&&!memcmp(media.request->input,expected,strlen(expected)));close_refusals=3;
    assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);(void)view(s,&media);if(fs.failed)assert(!tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used));
    tiku_mfs_object_t object;if(!tiku_mfs_stat(&fs,"cancel.patch",&object))assert(read_patch("cancel.patch",actual,sizeof actual)==strlen(expected)&&!strcmp(actual,expected));
    destroy(s,&host,&media);free(bridge);free(disk);puts("Patch: native bytes, clipboard bounds, immutable snapshots, create-only saves, dialog lifetime, binary refusal and cancellation passed");return 0;
}
