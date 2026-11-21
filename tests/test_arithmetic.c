/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_arithmetic.c - separate arithmetic task, touch keypad and clipboard.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_event.h"
#include "host.h"
#include "surface_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/** @brief Copy the current shell view. */
static surface_view_t view(surface_t *s)
{ surface_view_t v;surface_view(s,&v);return v; }
/** @brief Deliver one primary touch pair. */
static void click(surface_t *s,int x,int y)
{
    assert(surface_post(s,&(tiku_input_event_t){.type=TIKU_EVENT_POINTER_DOWN,.button=1,.x=x,.y=y}));
    assert(surface_post(s,&(tiku_input_event_t){.type=TIKU_EVENT_POINTER_UP,.button=1,.x=x,.y=y}));surface_step(s);
}
/** @brief Deliver one key with its modifiers. */
static void key(surface_t *s,unsigned key,unsigned mods)
{
    assert(surface_post(s,&(tiku_input_event_t){.type=TIKU_EVENT_KEY_DOWN,.key=key,.modifiers=mods}));surface_step(s);
}
/** @brief Type a sequence through the public event queue. */
static void type(surface_t *s,const char *p)
{ while(*p)key(s,(unsigned char)*p++,0); }
/** @brief Check discovery, desktop arithmetic, retained tasks and bounded painting. */
int main(int argc,char **argv)
{
    surface_host_t host;surface_config_t config=surface_host_config(&host);
    surface_t *s=surface_create(&config);assert(s);surface_step(s);
    click(s,SURFACE_HEX?680:SURFACE_READER?150:SURFACE_DEVICE?680:150,SURFACE_HEX?200:SURFACE_READER?280:SURFACE_DEVICE?210:380);assert(view(s).foreground==SURFACE_ARITHMETIC_APP && !strcmp(view(s).arithmetic,"0"));
    click(s,210,330);click(s,450,330);click(s,335,330);click(s,450,225);click(s,85,280);click(s,450,385);
    assert(!strcmp(view(s).arithmetic,"20"));
    click(s,650,225);assert(!strcmp(s->clipboard,"20") && view(s).arithmetic_copied);
    type(s,"c0.1+0.2=");assert(!strcmp(view(s).arithmetic,"0.3"));
    type(s,"c5/0=7");assert(!strcmp(view(s).arithmetic,"Error"));
    key(s,TIKU_KEY_ESCAPE,0);type(s,"200*50%=");assert(!strcmp(view(s).arithmetic,"100"));
    type(s,"c123");click(s,650,170);assert(!strcmp(view(s).arithmetic,"12"));
    click(s,210,170);assert(!strcmp(view(s).arithmetic,"-12"));
    click(s,210,170);key(s,'c',TIKU_MOD_CMD);assert(!strcmp(s->clipboard,"12"));
    assert(surface_launch(s,SURFACE_CALCULATOR));surface_step(s);
    click(s,100,140);key(s,'a',TIKU_MOD_CMD);key(s,'v',TIKU_MOD_CMD);key(s,TIKU_KEY_RETURN,0);
    assert(!strcmp(view(s).hex,"12"));
    assert(surface_launch(s,SURFACE_BROWSER));surface_step(s);
    assert(surface_launch(s,SURFACE_SETTINGS));surface_step(s);
    click(s,590,32);click(s,SURFACE_DEVICE?500:350,SURFACE_DIFF?180:SURFACE_DEVICE?210:SURFACE_TERMINAL?290:SURFACE_EDITOR?318:363);
    assert(view(s).foreground==SURFACE_ARITHMETIC_APP && !strcmp(view(s).arithmetic,"12"));
    assert((view(s).open_apps&30u)==30u);
    surface_view_t v=view(s);static uint32_t full[800*480],rows[800*480];
    tiku_surface_t a={.px=full,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=rows;
    surface_paint(&a,&v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}
    assert(!memcmp(full,rows,sizeof full));
    if(argc==2){FILE *f=fopen(argv[1],"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");for(unsigned i=0;i<800*480;++i){fputc(full[i]>>16&255,f);fputc(full[i]>>8&255,f);fputc(full[i]&255,f);}assert(!fclose(f));}
    for(unsigned i=0;i<100;++i){assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);assert(surface_launch(s,SURFACE_ARITHMETIC_APP));surface_step(s);assert(!strcmp(view(s).arithmetic,"0"));}
    surface_stop(s);host.now+=100000;(void)surface_host_poll(&host);surface_step(s);
    assert(surface_destroy(&s) && !host.memory.live);
    puts("Arithmetic: touch, desktop sequences, copy, four retained tasks and repeated close passed");return 0;
}
