/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_inspector.c - explicit reads, reviewed writes and namespace navigation.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main terminal_fixture_main
#include "tiku_event.h"
#include "test_terminal.c"
#undef main
/** @brief Drain the copied metadata operation without advancing fixture time. */
static surface_view_t inspect(surface_t *s)
{for(unsigned i=0;i<3;++i)surface_step(s);surface_view_t v;surface_view(s,&v);return v;}
/** @brief Replace a modal field and submit through the real key path. */
static void enter(surface_t *s,const char *text)
{key(s,'a',TIKU_MOD_CMD);for(size_t i=0;text[i];++i)key(s,(unsigned char)text[i],0);key(s,TIKU_KEY_RETURN,0);}
/** @brief Navigate to an absolute path through the touch path editor. */
static void path(surface_t *s,const char *text)
{click(s,150,110);enter(s,text);(void)inspect(s);}
int main(int argc,char **argv)
{
    surface_host_t h;surface_config_t c=surface_host_config(&h);surface_t *s=surface_create(&c);assert(s);
    click(s,590,32);click(s,500,SURFACE_DIFF?294:354);assert(s->active.state->id==SURFACE_VFS_APP);
    surface_view_t v=inspect(s);assert(v.inspector_count==2&&!h.vfs_reads);
    click(s,200,150);v=inspect(s);assert(!strcmp(v.inspector_path,"/sys")&&v.inspector_count==6&&v.inspector_more&&!h.vfs_reads);
    click(s,700,415);v=inspect(s);assert(v.inspector_count==3&&v.inspector_offset==6&&!v.inspector_more&&!strcmp(v.inspector_rows[2].name,"value8"));
    click(s,500,415);v=inspect(s);assert(v.inspector_offset==0&&v.inspector_count==6);
    click(s,200,150);v=inspect(s);assert(!strcmp(v.inspector_path,"/sys/device"));
    click(s,200,150);v=inspect(s);assert(v.inspector_reading&&!v.inspector_loaded&&!h.vfs_reads);
    click(s,300,415);surface_view(s,&v);assert(!v.text_entry);
    click(s,100,415);v=inspect(s);assert(v.inspector_loaded&&h.vfs_reads==1&&!strcmp(v.inspector_value,"preview-device"));
    click(s,500,415);assert(!strcmp(s->clipboard,"preview-device"));
    click(s,300,415);surface_view(s,&v);assert(v.text_entry);enter(s,"desk");v=inspect(s);
    assert(h.vfs_writes==1&&v.inspector_saved&&!v.inspector_error&&!strcmp(v.inspector_value,"desk"));
    click(s,300,415);strcpy(h.vfs_value,"foreign");enter(s,"next");v=inspect(s);assert(v.inspector_error==-11&&h.vfs_writes==1&&!strcmp(h.vfs_value,"foreign"));
    click(s,100,415);v=inspect(s);assert(v.inspector_loaded&&!v.inspector_error);
    click(s,300,415);key(s,TIKU_KEY_ESCAPE,0);assert(h.vfs_writes==1);
    click(s,300,415);h.vfs_fail_read=1;enter(s,"accepted");v=inspect(s);
    assert(h.vfs_writes==2&&v.inspector_saved&&v.inspector_error&&!v.inspector_loaded&&!strcmp(h.vfs_value,"accepted"));
    click(s,300,415);surface_view(s,&v);assert(!v.text_entry);h.vfs_fail_read=0;
    click(s,700,415);v=inspect(s);assert(!v.inspector_loaded&&!v.inspector_error);
    click(s,100,415);v=inspect(s);assert(v.inspector_loaded);
    path(s,"/sys/value1");unsigned before=h.vfs_reads;v=inspect(s);assert(v.inspector_node.policy==SURFACE_READ_EFFECT&&!v.inspector_loaded);
    for(unsigned i=0;i<100;++i)surface_step(s);
    assert(h.vfs_reads==before);
    click(s,100,415);v=inspect(s);assert(h.vfs_reads==before+1&&v.inspector_loaded&&!(v.inspector_node.flags&SURFACE_NODE_EDIT));
    path(s,"/data");v=inspect(s);assert(v.inspector_dynamic&&!v.inspector_count);
    path(s,"/absent");v=inspect(s);assert(v.inspector_error&&!v.inspector_dynamic&&!v.inspector_loaded&&!v.inspector_reading);
    path(s,"/sys/device/name");click(s,100,415);v=inspect(s);
    h.vfs_readonly=1;click(s,300,415);enter(s,"denied");v=inspect(s);assert(v.inspector_error&&h.vfs_writes==2);
    click(s,700,415);v=inspect(s);assert(!(v.inspector_node.flags&SURFACE_NODE_EDIT));h.vfs_readonly=0;
    click(s,700,415);v=inspect(s);click(s,100,415);v=inspect(s);
    static uint32_t full[800*480],sliced[800*480];tiku_surface_t a={.px=full,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=sliced;
    surface_paint(&a,&v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}assert(!memcmp(full,sliced,sizeof full));
    if(argc==2){FILE *f=fopen(argv[1],"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");for(unsigned i=0;i<800*480;++i){fputc(full[i]>>16&255,f);fputc(full[i]>>8&255,f);fputc(full[i]&255,f);}assert(!fclose(f));}
    assert(surface_launch(s,SURFACE_MONITOR_APP));surface_step(s);assert(surface_launch(s,SURFACE_VFS_APP));v=inspect(s);assert(!strcmp(v.inspector_value,"accepted"));
    surface_inspector_action(s->active.state,ACTION_INSPECTOR_EDIT);assert(surface_inspector_accept(s->active.state,"cancelled"));
    tiku_field_stop(&s->text_field);assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);surface_step(s);assert(h.vfs_writes==2);
    finish(&h,&s);
    for(unsigned failure=1;failure<16;++failure){c=surface_host_config(&h);s=surface_create(&c);assert(s);h.memory.calls=0;h.memory.fail_at=failure;assert(surface_launch(s,SURFACE_VFS_APP));surface_step(s);h.memory.fail_at=0;finish(&h,&s);}
    puts("Inspector: pagination, metadata-only navigation, explicit reads, reviewed writes, conflicts, cancelled dispatch, readback failures and retained state passed");return 0;
}
