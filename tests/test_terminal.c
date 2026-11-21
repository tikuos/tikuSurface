/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_terminal.c - console lifetime, copied input and retained desktop output.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host.h"
#include "surface_internal.h"
#include "tiku_event.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/** @brief Route a complete pointer gesture through the application. */
static void click(surface_t *s,int x,int y)
{
    tiku_input_event_t e={.type=TIKU_EVENT_POINTER_DOWN,.button=1,.x=x,.y=y};assert(surface_post(s,&e));
    e.type=TIKU_EVENT_POINTER_UP;assert(surface_post(s,&e));surface_step(s);
}
/** @brief Dispatch one key through the active modal/application owner. */
static void key(surface_t *s,unsigned k,unsigned modifiers)
{tiku_input_event_t e={.type=TIKU_EVENT_KEY_DOWN,.key=k,.modifiers=modifiers};assert(surface_post(s,&e));surface_step(s);}
/** @brief Complete retirement with a bounded number of owner turns. */
static void finish(surface_host_t *h,surface_t **s)
{
    surface_stop(*s);for(unsigned n=0;n<1000&&!surface_destroy(s);++n){h->now+=100000;(void)surface_host_poll(h);surface_step(*s);}
    assert(!*s&&!h->memory.live&&!h->terminal_open);
}
int main(int argc,char **argv)
{
    surface_host_t h;surface_config_t c=surface_host_config(&h);surface_t *s=surface_create(&c);assert(s);
    click(s,590,32);click(s,SURFACE_DEVICE?500:350,SURFACE_DIFF?238:SURFACE_DEVICE?284:402);assert(h.terminal_open&&s->active.state->id==SURFACE_TERMINAL_APP);
    const char *reply="tiku> \033[32mready\033[0m\r\n";memcpy(h.terminal_output,reply,strlen(reply));h.terminal_out_size=strlen(reply);surface_step(s);
    surface_view_t v;surface_view(s,&v);assert(v.terminal_cells[0][6].ch=='r'&&v.terminal_cells[0][6].fg==2);
    h.terminal_write_limit=0;click(s,70,420);surface_view(s,&v);assert(v.text_entry);
    const char *command="echo hello";for(size_t i=0;command[i];++i)key(s,(unsigned)command[i],0);key(s,TIKU_KEY_RETURN,0);
    surface_view(s,&v);assert(!v.text_entry&&v.terminal_pending==11&&!h.terminal_in_size);
    key(s,'x',0);key(s,'y',0);surface_view(s,&v);assert(v.terminal_pending==13);
    h.terminal_write_limit=2;for(unsigned n=0;n<10;++n)surface_step(s);
    assert(h.terminal_in_size==13&&!memcmp(h.terminal_input,"echo hello\nxy",13));
    assert(surface_launch(s,SURFACE_CALCULATOR));surface_step(s);assert(h.terminal_open);
    memcpy(h.terminal_output,"background\r\n",12);h.terminal_out_size=12;surface_step(s);
    assert(surface_launch(s,SURFACE_TERMINAL_APP));surface_step(s);surface_view(s,&v);assert(v.terminal_cells[1][0].ch=='b');
    click(s,570,420);assert(!s->clipboard[0]);
    key(s,'c',TIKU_MOD_CMD);assert(h.terminal_in_size==1&&h.terminal_input[0]==3);
    static uint32_t full[800*480],rows[800*480];tiku_surface_t a={.px=full,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=rows;
    surface_view(s,&v);surface_paint(&a,&v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}assert(!memcmp(full,rows,sizeof full));
    if(argc==2){FILE *f=fopen(argv[1],"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");for(unsigned i=0;i<800*480;++i){fputc(full[i]>>16&255,f);fputc(full[i]>>8&255,f);fputc(full[i]&255,f);}assert(!fclose(f));}
    h.terminal_close_delay=4;assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);surface_step(s);assert(h.terminal_open);
    for(unsigned n=0;n<20;++n)surface_step(s);
    assert(!h.terminal_open);
    assert(surface_launch(s,SURFACE_TERMINAL_APP));surface_step(s);h.terminal_error=1;surface_step(s);surface_view(s,&v);assert(v.terminal_error);finish(&h,&s);
    for(unsigned failure=1;failure<20;++failure){c=surface_host_config(&h);s=surface_create(&c);assert(s);h.memory.calls=0;h.memory.fail_at=failure;assert(surface_launch(s,SURFACE_TERMINAL_APP));surface_step(s);h.memory.fail_at=0;finish(&h,&s);}
    puts("Terminal: menu, output, touch commands, backpressure, background retention, cancellation, rendering and allocation failures passed");return 0;
}
