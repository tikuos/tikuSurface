/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_process_monitor.c - paced process pages, detail and retained selection.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main terminal_fixture_main
#include "tiku_event.h"
#include "test_terminal.c"
#undef main
/** @brief Complete a metric/process cycle without advancing the clock. */
static surface_view_t observed(surface_t *s)
{for(unsigned i=0;i<20;++i)surface_step(s);surface_view_t v;surface_view(s,&v);return v;}
int main(int argc,char **argv)
{
    surface_host_t h;surface_config_t c=surface_host_config(&h);surface_t *s=surface_create(&c);assert(s);
    assert(surface_launch(s,SURFACE_MONITOR_APP));surface_step(s);surface_view_t v=observed(s);assert(!h.process_reads);
    click(s,680,290);v=observed(s);assert(v.monitor_mode==1&&v.monitor_process_valid==255&&v.monitor_process_present==255&&h.process_reads==8);
    assert(!strcmp(v.monitor_processes[7].name,"process-7")&&v.monitor_processes[7].events==7);
    unsigned before=h.process_reads;for(unsigned i=0;i<100;++i)surface_step(s);assert(h.process_reads==before);
    click(s,700,415);v=observed(s);assert(v.monitor_page==4);click(s,200,350);v=observed(s);assert(v.monitor_mode==2&&v.monitor_selected==7);
    click(s,500,415);v=observed(s);assert(v.monitor_selected==6);
    static uint32_t full[800*480],sliced[800*480];tiku_surface_t a={.px=full,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=sliced;
    surface_paint(&a,&v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}assert(!memcmp(full,sliced,sizeof full));
    if(argc==2){FILE *f=fopen(argv[1],"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");for(unsigned i=0;i<800*480;++i){fputc(full[i]>>16&255,f);fputc(full[i]>>8&255,f);fputc(full[i]&255,f);}assert(!fclose(f));}
    click(s,300,415);h.now+=2000000;v=observed(s);assert(v.monitor_paused&&h.process_reads==before);
    click(s,100,415);v=observed(s);assert(v.monitor_paused&&h.process_reads==before+8);
    assert(surface_launch(s,SURFACE_BROWSER));surface_step(s);assert(surface_launch(s,SURFACE_MONITOR_APP));v=observed(s);assert(v.monitor_mode==2&&v.monitor_selected==6);
    h.process_missing=1;click(s,100,415);v=observed(s);assert(v.monitor_process_valid==255&&!v.monitor_process_present&&!v.monitor_processes[6].name[0]);
    h.process_missing=0;h.process_error=1;click(s,100,415);v=observed(s);assert(!v.monitor_process_valid&&!v.monitor_process_present);
    h.process_error=0;click(s,300,415);h.now+=1000000;
    for(unsigned i=0;i<24;++i){before=h.device_reads+h.process_reads;surface_step(s);assert(h.device_reads+h.process_reads<=before+1);}
    v=observed(s);assert(v.monitor_process_present==255);
    key(s,TIKU_KEY_ESCAPE,0);v=observed(s);assert(v.monitor_mode==1);key(s,TIKU_KEY_ESCAPE,0);v=observed(s);assert(!v.monitor_mode);
    before=h.process_reads;h.now+=2000000;v=observed(s);assert(h.process_reads==before);
    finish(&h,&s);puts("Process Monitor: paced reads, eight slots, detail navigation, pause/refresh, missing/error state and retained selection passed");return 0;
}
