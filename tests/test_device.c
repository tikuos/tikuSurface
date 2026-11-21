/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_device.c - paced observations, missing values and retained history.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main terminal_fixture_main
#include "test_terminal.c"
#undef main
/** @brief Finish one seven-endpoint observation cycle. */
static void cycle(surface_t *s){for(unsigned n=0;n<7;++n)surface_step(s);}
/** @brief Inject an overflowing decimal reading into the public device provider. */
static int oversized(void *p,const char *path,char *out,size_t cap)
{(void)p;(void)path;assert(cap>20);memcpy(out,"18446744073709551616",20);return 20;}
int main(int argc,char **argv)
{
    surface_host_t h;surface_config_t c=surface_host_config(&h);h.device_used=100;surface_t *s=surface_create(&c);assert(s);
    click(s,590,32);click(s,200,SURFACE_DIFF?294:354);assert(s->active.state->id==SURFACE_MONITOR_APP);cycle(s);
    surface_view_t v;surface_view(s,&v);assert(v.monitor_valid==127&&v.monitor_values[2]==100&&v.monitor_count==1&&h.device_reads==7);
    for(unsigned n=0;n<100;++n)surface_step(s);
    assert(h.device_reads==7);
    h.now+=1000000;h.device_used=200;unsigned before=h.device_reads;surface_step(s);assert(h.device_reads==before+1);cycle(s);
    surface_view(s,&v);assert(v.monitor_count==2&&v.monitor_history[1]==200);
    click(s,400,415);h.now+=2000000;before=h.device_reads;cycle(s);assert(h.device_reads==before);
    click(s,100,415);cycle(s);surface_view(s,&v);assert(v.monitor_paused&&v.monitor_count==3&&h.device_reads==before+7);
    h.device_failed=1;click(s,400,415);h.now+=1000000;cycle(s);surface_view(s,&v);assert(!v.monitor_valid&&v.monitor_gaps[v.monitor_count-1]);
    h.device_failed=0;assert(surface_launch(s,SURFACE_CALCULATOR));surface_step(s);h.now+=1000000;cycle(s);
    assert(surface_launch(s,SURFACE_MONITOR_APP));surface_step(s);surface_view(s,&v);assert(v.monitor_valid==127);
    for(unsigned n=0;n<130;++n){h.now+=1000000;h.device_used=n;cycle(s);}surface_view(s,&v);assert(v.monitor_count==120);
    static uint32_t full[800*480],sliced[800*480];tiku_surface_t a={.px=full,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=sliced;
    surface_paint(&a,&v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}assert(!memcmp(full,sliced,sizeof full));
    if(argc==2){FILE *f=fopen(argv[1],"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");for(unsigned i=0;i<800*480;++i){fputc(full[i]>>16&255,f);fputc(full[i]>>8&255,f);fputc(full[i]&255,f);}assert(!fclose(f));}
    click(s,650,415);surface_view(s,&v);assert(!v.monitor_count);
    s->config.device.read=oversized;h.now+=1000000;cycle(s);surface_view(s,&v);assert(!v.monitor_valid);
    finish(&h,&s);
    for(unsigned failure=1;failure<16;++failure){c=surface_host_config(&h);s=surface_create(&c);assert(s);h.memory.calls=0;h.memory.fail_at=failure;assert(surface_launch(s,SURFACE_MONITOR_APP));surface_step(s);h.memory.fail_at=0;finish(&h,&s);}
    puts("Device Monitor: pacing, pause/refresh, gaps, overflow refusal, retained history and sliced rendering passed");return 0;
}
