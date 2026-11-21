/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_word_ranges.c - inclusive bit selection and clipboard interaction.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_event.h"
#include "host.h"
#include "surface_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/** @brief Copy the current public view. */
static surface_view_t view(surface_t *s)
{ surface_view_t v;surface_view(s,&v);return v; }
/** @brief Deliver a touch pair through the input queue. */
static void click(surface_t *s,int x,int y)
{
    assert(surface_post(s,&(tiku_input_event_t){.type=TIKU_EVENT_POINTER_DOWN,.button=1,.x=x,.y=y}));
    assert(surface_post(s,&(tiku_input_event_t){.type=TIKU_EVENT_POINTER_UP,.button=1,.x=x,.y=y}));surface_step(s);
}
/** @brief Deliver one physical key or editing command. */
static void key(surface_t *s,unsigned code,unsigned mods)
{
    assert(surface_post(s,&(tiku_input_event_t){.type=TIKU_EVENT_KEY_DOWN,.key=code,.modifiers=mods}));surface_step(s);
}
/** @brief Check inclusive endpoints, both directions, page crossing and copy. */
int main(int argc,char **argv)
{
    surface_host_t host;surface_config_t config=surface_host_config(&host);
    surface_t *s=surface_create(&config);assert(s && surface_launch(s,SURFACE_CALCULATOR));surface_step(s);
    const char *digits="FEDCBA9876543210";while(*digits) key(s,(unsigned char)*digits++,0);
    click(s,180,102);click(s,270,392);assert(view(s).range_select);
    click(s,74,277);assert(view(s).range_pending && view(s).range_high==15);
    click(s,718,277);assert(!view(s).range_pending && view(s).range_low==8);
    assert(!strcmp(view(s).range_hex,"32") && !strcmp(view(s).hex,"FEDCBA9876543210"));
    click(s,410,392);assert(view(s).range_copied && !strcmp(s->clipboard,"32"));
    click(s,110,392);click(s,74,169);assert(view(s).range_high==63);
    click(s,110,392);click(s,718,331);
    assert(view(s).range_low==0 && !strcmp(view(s).range_hex,"FEDCBA9876543210"));
    click(s,410,392);assert(!strcmp(s->clipboard,"FEDCBA9876543210"));
    click(s,110,392);click(s,718,331);click(s,74,169);
    assert(view(s).range_low==32 && view(s).range_high==63 && !strcmp(view(s).range_hex,"FEDCBA98"));
    click(s,74,169);click(s,74,169);assert(!strcmp(view(s).range_hex,"1"));
    assert(surface_launch(s,SURFACE_HOME));surface_step(s);
    assert(surface_launch(s,SURFACE_CALCULATOR));surface_step(s);
    assert(view(s).range_select && view(s).range_high==63 && view(s).range_low==63);
    click(s,270,392);click(s,74,169);
    assert(!strcmp(view(s).hex,"7EDCBA9876543210"));
    click(s,60,102);key(s,'X',0);key(s,'F',0);key(s,'0',0);click(s,470,330);
    assert(view(s).word_width==8 && view(s).range_high==7 && view(s).range_low==7);
    click(s,180,102);click(s,270,392);click(s,74,331);click(s,350,331);
    assert(view(s).range_high==7 && view(s).range_low==4 && !strcmp(view(s).range_hex,"F"));
    click(s,74,169);assert(view(s).range_high==7 && view(s).range_low==4);
    click(s,410,392);assert(!strcmp(s->clipboard,"F"));
    surface_view_t v=view(s);static uint32_t full[800*480],rows[800*480];
    tiku_surface_t a={.px=full,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=rows;
    surface_paint(&a,&v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}
    assert(!memcmp(full,rows,sizeof full));
    if(argc==2) {
        FILE *file=fopen(argv[1],"wb");assert(file);fprintf(file,"P6\n800 480\n255\n");
        for(unsigned i=0;i<800*480;++i){fputc((full[i]>>16)&255,file);fputc((full[i]>>8)&255,file);fputc(full[i]&255,file);}assert(!fclose(file));
    }
    click(s,60,102);click(s,100,140);key(s,'a',TIKU_MOD_CMD);key(s,'v',TIKU_MOD_CMD);key(s,TIKU_KEY_RETURN,0);
    assert(!view(s).text_entry && !strcmp(view(s).hex,"F") && !view(s).range_copied);
    assert(view(s).range_high==7 && view(s).range_low==4 && !strcmp(view(s).range_hex,"0"));
    surface_stop(s);assert(surface_destroy(&s) && !host.memory.live);
    puts("Word ranges: endpoints, page crossing, retained selection, clipboard and sliced painting passed");
    return 0;
}
