/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_device_preferences.c - reviewed device controls and independent Settings ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main terminal_fixture_main
#include "test_terminal.c"
#undef main
#include <stdlib.h>
static const char *paths[]={"/sys/device/name","/sys/time","/sys/cpu/freq_target","/sys/crypto/mode","/sys/radio/txpower","/sys/cpu/freq_available"};
static char values[6][256];
static unsigned reads,writes,metadata;
static int readonly,fail_read,absent;
/** @brief Map an audited preference path to a fixture identity. */
static int index_of(const char *path){for(int i=0;i<6;++i)if(!strcmp(path,paths[i]))return i;return -1;}
/** @brief Copy metadata without invoking a value handler. */
static int stat_node(void *ctx,const char *path,surface_vfs_node_t *out)
{(void)ctx;++metadata;int i=index_of(path);if(i<0||absent)return -1;memset(out,0,sizeof *out);out->id=(uint32_t)i+1;out->flags=SURFACE_NODE_READ;if(i<5&&!readonly)out->flags|=SURFACE_NODE_WRITE|SURFACE_NODE_EDIT;out->policy=SURFACE_READ_POLL;return 0;}
/** @brief Read one identity-bound value with optional failure. */
static int read_node(void *ctx,const char *path,uint32_t id,char *out,size_t cap,size_t *total)
{(void)ctx;++reads;int i=index_of(path);if(i<0||id!=(unsigned)i+1||fail_read||absent)return -1;*total=strlen(values[i]);assert(*total<cap);memcpy(out,values[i],*total+1);return (int)*total;}
/** @brief Publish only against the reviewed value and current permission. */
static int write_node(void *ctx,const char *path,uint32_t id,const char *value,size_t n,const char *before,size_t length)
{(void)ctx;int i=index_of(path);if(i<0||i==5||id!=(unsigned)i+1||readonly||absent)return -1;if(length!=strlen(values[i])||memcmp(before,values[i],length))return -11;assert(n<256);memcpy(values[i],value,n);values[i][n]=0;++writes;return 0;}
/** @brief Drain separate metadata/read/write phases. */
static surface_view_t view(surface_t *s)
{for(unsigned i=0;i<3;++i){unsigned count=reads+writes+metadata;surface_step(s);assert(reads+writes+metadata-count<=1);}surface_view_t v;surface_view(s,&v);return v;}
/** @brief Submit one reviewed draft through the normal clipboard and field path. */
static void enter(surface_t *s,const char *text)
{strcpy(s->clipboard,text);key(s,'a',TIKU_MOD_CMD);key(s,'v',TIKU_MOD_CMD);key(s,TIKU_KEY_RETURN,0);}
/** @brief Select a device control using its touch row. */
static void select_setting(surface_t *s,unsigned i)
{click(s,100+(i%2)*376,165+(i/2)*72);assert(view(s).preferences_page==3);}
/** @brief Verify sliced painting and retain optional review captures. */
static void painting(surface_t *s,const char *name)
{
    static uint32_t full[800*480],rows[800*480];surface_view_t v=view(s);
    tiku_surface_t a={.px=full,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=rows;surface_paint(&a,&v);
    for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}assert(!memcmp(full,rows,sizeof full));
    const char *dir=getenv("SURFACE_CAPTURE_DIR");if(!dir)return;char path[1024];int n=snprintf(path,sizeof path,"%s/%s.ppm",dir,name);assert(n>0&&(size_t)n<sizeof path);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");for(size_t i=0;i<800*480;++i){fputc(full[i]>>16,f);fputc(full[i]>>8,f);fputc(full[i],f);}assert(!fclose(f));
}
/** @brief Expose editable unsaved desktop defaults independently of device settings. */
static unsigned preferences_read(void *ctx,surface_preferences_t *out)
{(void)ctx;*out=(surface_preferences_t){0};return SURFACE_SETTINGS_DEFAULT;}
int main(void)
{
    strcpy(values[0],"desk");strcpy(values[1],"1795000000\n");strcpy(values[2],"160000000\n");strcpy(values[3],"auto\n");strcpy(values[4],"10\n");strcpy(values[5],"80000000 160000000 240000000\n");
    surface_host_t h;surface_config_t c=surface_host_config(&h);c.vfs=(surface_vfs_provider_t){.stat=stat_node,.read=read_node,.write=write_node};c.preferences.read=preferences_read;surface_t *s=surface_create(&c);assert(s&&surface_launch(s,SURFACE_SETTINGS));surface_step(s);
    click(s,400,110);assert(view(s).preferences_page==2&&!reads&&!writes);painting(s,"device-settings");
    h.memory.calls=0;h.memory.fail_at=1;click(s,100,165);surface_view_t v=view(s);assert(v.preferences_page==2&&v.preferences_device_error&&!metadata);h.memory.fail_at=0;
    select_setting(s,0);assert(!reads&&!writes);click(s,300,410);assert(!view(s).text_entry);click(s,100,410);v=view(s);assert(v.inspector_loaded&&!strcmp(v.inspector_value,"desk"));painting(s,"device-name");
    click(s,500,410);assert(!strcmp(s->clipboard,"desk"));click(s,300,410);v=view(s);assert(v.text_entry&&!strcmp(v.text_value,"desk"));enter(s,"studio");v=view(s);assert(writes==1&&v.inspector_saved&&!strcmp(values[0],"studio"));
    click(s,300,410);strcpy(values[0],"foreign");enter(s,"stale");v=view(s);assert(v.inspector_error==-11&&writes==1&&!strcmp(values[0],"foreign"));
    click(s,100,410);v=view(s);click(s,300,410);key(s,TIKU_KEY_ESCAPE,0);assert(writes==1&&!view(s).text_entry);
    click(s,300,410);fail_read=1;enter(s,"accepted");v=view(s);assert(writes==2&&v.inspector_saved&&v.inspector_error&&!v.inspector_loaded);fail_read=0;
    click(s,690,410);for(unsigned i=1;i<6;++i){select_setting(s,i);click(s,100,410);v=view(s);assert(v.inspector_loaded&&!strcmp(v.inspector_value,values[i]));if(i==5){click(s,300,410);assert(!view(s).text_entry);}click(s,690,410);}
    absent=1;select_setting(s,0);v=view(s);assert(v.inspector_error&&!v.inspector_loaded);click(s,300,410);assert(!view(s).text_entry);absent=0;click(s,690,410);
    readonly=1;select_setting(s,0);click(s,100,410);click(s,300,410);assert(!view(s).text_entry);readonly=0;click(s,690,410);select_setting(s,0);click(s,100,410);
    assert(surface_launch(s,SURFACE_VFS_APP));surface_step(s);assert(surface_inspector_open(s->active.state,"/sys/crypto/mode"));(void)view(s);assert(surface_launch(s,SURFACE_SETTINGS));v=view(s);assert(!strcmp(v.inspector_path,"/sys/device/name")&&!strcmp(v.inspector_value,"accepted"));
    click(s,160,30);assert(view(s).preferences_page==2);click(s,160,30);assert(!view(s).preferences_page);click(s,650,110);click(s,350,140);assert(view(s).preferences_dirty);click(s,100,410);click(s,400,110);assert(view(s).preferences_dirty);assert(surface_close(s)==SURFACE_CLOSE_CONFIRM);assert(surface_resolve_close(s,0));assert(view(s).preferences_page==2&&view(s).preferences_dirty);
    assert(surface_close(s)==SURFACE_CLOSE_CONFIRM);assert(surface_resolve_close(s,1));(void)view(s);
    assert(surface_launch(s,SURFACE_SETTINGS));surface_step(s);click(s,400,110);select_setting(s,0);click(s,100,410);(void)view(s);
    surface_inspector_action(s->active.state,ACTION_INSPECTOR_EDIT);assert(surface_inspector_accept(s->active.state,"cancelled"));tiku_field_stop(&s->text_field);unsigned before=writes;assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);(void)view(s);assert(writes==before&&!strcmp(values[0],"accepted"));
    finish(&h,&s);puts("Device preferences: metadata/read separation, reviewed writes, conflicts, permission, cancellation, retained pages, allocation failure and clipped painting passed");return 0;
}
