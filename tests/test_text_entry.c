/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_text_entry.c - modal touch editing, clipboard and publication boundaries.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_event.h"
#include "host.h"
#include "surface_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/** @brief Copy the shell view after one owner turn. */
static surface_view_t view(surface_t *s)
{ surface_view_t v;surface_view(s,&v);return v; }
/** @brief Deliver a primary touch pair. */
static void click(surface_t *s,int x,int y)
{
    assert(surface_post(s,&(tiku_input_event_t){.type=TIKU_EVENT_POINTER_DOWN,.button=1,.x=x,.y=y}));
    assert(surface_post(s,&(tiku_input_event_t){.type=TIKU_EVENT_POINTER_UP,.button=1,.x=x,.y=y}));surface_step(s);
}
/** @brief Deliver one physical character or command. */
static void key(surface_t *s,unsigned code,unsigned mods)
{
    assert(surface_post(s,&(tiku_input_event_t){.type=TIKU_EVENT_KEY_DOWN,.key=code,.modifiers=mods}));surface_step(s);
}
/** @brief Exercise the real shell entry points, touch keys and bounded repaint. */
int main(int argc,char **argv)
{
    surface_host_t host;surface_config_t config=surface_host_config(&host);
    surface_t *s=surface_create(&config);assert(s && surface_launch(s,SURFACE_CALCULATOR));surface_step(s);
    key(s,'A',0);key(s,'B',0);key(s,'x',TIKU_MOD_CMD);
    assert(!strcmp(view(s).hex,"AB"));click(s,100,140);
    assert(view(s).text_entry && !strcmp(view(s).text_value,"AB"));
    assert(!surface_launch(s,SURFACE_HOME) && surface_close(s)==SURFACE_CLOSE_BUSY);
    click(s,50,225);assert(!strcmp(view(s).text_value,"1") && !strcmp(view(s).hex,"AB"));
    click(s,500,178);assert(!strcmp(view(s).text_value,"AB"));
    click(s,600,178);assert(!strcmp(view(s).text_value,"1"));
    click(s,50,178);click(s,270,178);click(s,160,178);
    assert(!view(s).text_value[0]);click(s,380,178);assert(!strcmp(view(s).text_value,"1"));
    click(s,700,420);assert(!view(s).text_entry && !strcmp(view(s).hex,"1"));
    click(s,100,140);key(s,'q',0);click(s,700,420);
    assert(view(s).text_entry && view(s).text_error && !strcmp(view(s).hex,"1"));
    key(s,TIKU_KEY_ESCAPE,0);assert(!view(s).text_entry && !strcmp(view(s).hex,"1"));
    click(s,100,140);key(s,'a',TIKU_MOD_CMD);key(s,'v',TIKU_MOD_CMD);
    assert(!strcmp(view(s).text_value,"1"));key(s,TIKU_KEY_RETURN,0);assert(!view(s).text_entry);
    click(s,100,140);key(s,'F',0);key(s,'F',0);key(s,TIKU_KEY_RETURN,0);
    click(s,470,330);assert(view(s).word_width==8);
    click(s,100,140);key(s,'1',0);key(s,'0',0);key(s,'0',0);key(s,TIKU_KEY_RETURN,0);
    assert(view(s).text_entry && view(s).text_error && !strcmp(view(s).hex,"FF"));
    key(s,TIKU_KEY_ESCAPE,0);click(s,100,140);
    key(s,'a',TIKU_MOD_CMD);key(s,'c',TIKU_MOD_CMD);key(s,'x',TIKU_MOD_CMD);
    assert(!view(s).text_value[0]);key(s,'z',TIKU_MOD_CMD);assert(!strcmp(view(s).text_value,"FF"));
    key(s,'z',TIKU_MOD_CMD|TIKU_MOD_SHIFT);assert(!view(s).text_value[0]);
    key(s,'v',TIKU_MOD_CMD);assert(!strcmp(view(s).text_value,"FF"));
    key(s,'a',TIKU_MOD_CMD);key(s,'x',TIKU_MOD_CMD);
    for(int i=0;i<255;++i) key(s,'A',0);
    assert(strlen(view(s).text_value)==255);key(s,'B',0);
    assert(strlen(view(s).text_value)==255 && view(s).text_error);
    /* A queue overflow cancels an armed key before the release arrives. */
    assert(surface_post(s,&(tiku_input_event_t){.type=TIKU_EVENT_POINTER_DOWN,.button=1,.x=700,.y=178}));surface_step(s);
    for(int i=0;i<20;++i) (void)surface_post(s,&(tiku_input_event_t){.type=TIKU_EVENT_KEY_DOWN,.key='C'});
    surface_step(s);
    assert(surface_post(s,&(tiku_input_event_t){.type=TIKU_EVENT_POINTER_UP,.button=1,.x=700,.y=178}));surface_step(s);
    assert(strlen(view(s).text_value)==255);
    key(s,TIKU_KEY_ESCAPE,0);click(s,100,140);
    tiku_text_input_t commit; tiku_input_event_t piece;
    assert(tiku_text_input_set(&commit,"caf\xc3\xa9-123456789",15));
    assert(tiku_text_input_next(&commit,&piece));assert(surface_post(s,&piece));surface_step(s);
    assert(!strcmp(view(s).text_value,"FF"));
    assert(surface_post(s,&(tiku_input_event_t){.type=TIKU_EVENT_DEACTIVATED}));surface_step(s);
    while(tiku_text_input_next(&commit,&piece)){assert(surface_post(s,&piece));surface_step(s);}
    assert(!strcmp(view(s).text_value,"FF"));
    assert(tiku_text_input_set(&commit,"caf\xc3\xa9",5));
    while(tiku_text_input_next(&commit,&piece)){assert(surface_post(s,&piece));surface_step(s);}
    assert(!strcmp(view(s).text_value,"caf\xc3\xa9"));
    key(s,TIKU_KEY_LEFT,0);assert(view(s).text_caret==3);
    key(s,TIKU_KEY_DELETE,0);assert(!strcmp(view(s).text_value,"caf"));
    key(s,'z',TIKU_MOD_CMD);assert(!strcmp(view(s).text_value,"caf\xc3\xa9"));
    key(s,TIKU_KEY_ESCAPE,0);click(s,100,140);
    click(s,70,420);click(s,180,275);assert(!strcmp(view(s).text_value,"E"));
    click(s,170,420);click(s,70,225);assert(!strcmp(view(s).text_value,"E!"));
    key(s,TIKU_KEY_ESCAPE,0);click(s,100,140);
    surface_view_t v=view(s);static uint32_t full[800*480],rows[800*480];
    tiku_surface_t a={.px=full,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=rows;
    surface_paint(&a,&v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}
    assert(!memcmp(full,rows,sizeof full));
    if(argc==2) {
        FILE *file=fopen(argv[1],"wb");assert(file);fprintf(file,"P6\n800 480\n255\n");
        for(unsigned i=0;i<800*480;++i){fputc((full[i]>>16)&255,file);fputc((full[i]>>8)&255,file);fputc(full[i]&255,file);}assert(!fclose(file));
    }
    for(int i=0;i<100;++i){key(s,TIKU_KEY_ESCAPE,0);click(s,100,140);assert(view(s).text_entry);}
    key(s,TIKU_KEY_ESCAPE,0);click(s,290,102);assert(view(s).word_mode==2);
    click(s,100,150);assert(view(s).text_entry);
    const char *sample="313233343536373839";
    while(*sample) key(s,(unsigned char)*sample++,0);
    key(s,TIKU_KEY_RETURN,0);assert(!view(s).text_entry && view(s).checksum_bytes==9);
    assert(!strcmp(view(s).crc32,"CBF43926") && !strcmp(view(s).crc16,"29B1"));
    click(s,100,150);key(s,'F',0);key(s,TIKU_KEY_RETURN,0);
    assert(view(s).text_entry && view(s).text_error && view(s).checksum_bytes==9);
    key(s,TIKU_KEY_ESCAPE,0);click(s,180,102);assert(view(s).word_mode==1);
    uint64_t word=view(s).word_value;
    click(s,60,170);assert(view(s).word_value==word);
    click(s,610,330);assert(view(s).word_value==(word^UINT64_C(2)));
    click(s,60,102);assert(view(s).word_mode==0);
    click(s,470,330);click(s,470,330);click(s,470,330);assert(view(s).word_width==64);
    click(s,180,102);click(s,60,170);
    assert(view(s).word_value==((word^UINT64_C(2))|(UINT64_C(1)<<31)));
    click(s,70,390);assert(view(s).bit_page==1);click(s,60,170);
    assert(view(s).word_value==((word^UINT64_C(2))|(UINT64_C(1)<<31)|(UINT64_C(1)<<63)));
    assert(surface_launch(s,SURFACE_HOME));surface_step(s);
    assert(surface_launch(s,SURFACE_CALCULATOR));surface_step(s);
    assert(view(s).word_mode==1 && view(s).bit_page==1);
    for(int mode=0;mode<3;++mode) {
        click(s,60+116*mode,102);v=view(s);a.clip=(tiku_rect_t){0,0,800,480};surface_paint(&a,&v);
        for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}
        assert(!memcmp(full,rows,sizeof full));
    }
    assert(!strcmp(view(s).crc32,"CBF43926"));
    surface_stop(s);assert(surface_destroy(&s) && !host.memory.live);
    puts("Text entry: touch, modal ownership, clipboard, undo, atomic apply, limits and sliced painting passed");
    return 0;
}
