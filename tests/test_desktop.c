/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_desktop.c - desktop window input, frozen frames and allocator failures.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host.h"
#include "surface_internal.h"
#include "tiku_window.h"
#include "tiku_font.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static tiku_rgb_t expected[800*480];
/** @brief Advance copied providers and one desktop owner turn. */
static void step(surface_host_t *h,surface_t *s)
{ h->now+=10000;surface_host_poll(h);surface_step(s); }
/** @brief Deliver an input edge and advance its serialized owner. */
static void pointer(surface_t *s,unsigned type,int x,int y)
{ assert(surface_post(s,&(tiku_input_event_t){.type=type,.button=1,.x=x,.y=y}));surface_step(s); }
/** @brief Click with an owner turn between press and release. */
static void click(surface_t *s,int x,int y)
{ pointer(s,TIKU_EVENT_POINTER_DOWN,x,y);pointer(s,TIKU_EVENT_POINTER_UP,x,y); }
/** @brief Read a current native window rectangle. */
static tiku_rect_t frame(surface_t *s,surface_app_t id)
{ tiku_rect_t r;assert(surface_desktop_geometry(s,id,&r,NULL));return r; }
/** @brief Read a native content rectangle from the window geometry contract. */
static tiku_rect_t content(surface_t *s,surface_app_t id)
{ tiku_window_t w={.frame=frame(s,id)};return tiku_window_content(&w); }
/** @brief Complete one frame without retiring its presentation borrow. */
static void render(surface_host_t *h,surface_t *s)
{
    unsigned limit=500;while(surface_render(s,surface_host_presenter(h))==TIKU_PRESENT_BUSY)assert(--limit);
    assert(h->frame.pixels);
}
/** @brief Retire outstanding media and display leases and restore allocation count. */
static void finish(surface_host_t *h,surface_t **s)
{
    surface_stop(*s);for(int i=0;i<100&&*s;++i){step(h,*s);(void)surface_destroy(s);}
    assert(!*s&&!h->memory.live&&!h->memory.bytes);
}
/** @brief Supply editable settings for the modal-close journey. */
static unsigned settings_read(void *ctx,surface_touch_map_t *map)
{ (void)ctx;*map=(surface_touch_map_t){0,799,0,479,0,0,0};return SURFACE_SETTINGS_SAVED; }
/** @brief Drive retained windows, content navigation and canceled captured gestures. */
static void windows(void)
{
    surface_host_t h;surface_config_t c=surface_host_config(&h);c.settings.read=settings_read;surface_t *s=surface_create(&c);assert(s);
    click(s,50,70);for(int i=0;i<30;++i)step(&h,s);
    surface_view_t v;surface_view(s,&v);assert(v.foreground==SURFACE_BROWSER&&v.row_count==SURFACE_BROWSER_ROWS);
    tiku_rect_t before=frame(s,SURFACE_BROWSER);
    pointer(s,TIKU_EVENT_POINTER_DOWN,before.x+60,before.y+8);
    pointer(s,TIKU_EVENT_POINTER_MOVE,before.x+80,before.y+18);
    pointer(s,TIKU_EVENT_POINTER_UP,before.x+80,before.y+18);
    tiku_rect_t after=frame(s,SURFACE_BROWSER);assert(after.x==before.x+20&&after.y==before.y+10);
    pointer(s,TIKU_EVENT_POINTER_DOWN,after.x+60,after.y+8);
    assert(surface_post(s,&(tiku_input_event_t){.type=TIKU_EVENT_DEACTIVATED}));surface_step(s);
    pointer(s,TIKU_EVENT_POINTER_MOVE,300,200);assert(!memcmp(&after,&(tiku_rect_t){frame(s,SURFACE_BROWSER).x,frame(s,SURFACE_BROWSER).y,frame(s,SURFACE_BROWSER).w,frame(s,SURFACE_BROWSER).h},sizeof after));
    tiku_rect_t body=content(s,SURFACE_BROWSER);click(s,body.x+30,body.y+80);
    surface_view(s,&v);assert(!strcmp(v.path,"/"));
    click(s,body.x+30,body.y+80);for(int i=0;i<30;++i)step(&h,s);
    surface_view(s,&v);assert(!strcmp(v.path,"/docs"));
    click(s,50,165);assert(surface_desktop_geometry(s,SURFACE_SETTINGS,NULL,NULL));
    assert(surface_desktop_geometry(s,SURFACE_BROWSER,NULL,NULL));
    body=content(s,SURFACE_SETTINGS);click(s,body.x+body.w-30,body.y+16);
    int minimized=0;assert(surface_desktop_geometry(s,SURFACE_SETTINGS,NULL,&minimized)&&minimized);
    click(s,710,86);assert(surface_desktop_geometry(s,SURFACE_SETTINGS,NULL,&minimized)&&!minimized);
    surface_view(s,&v);assert(v.foreground==SURFACE_SETTINGS);
    tiku_rect_t settings=frame(s,SURFACE_SETTINGS);click(s,settings.x+10,settings.y+8);surface_step(s);
    assert(!surface_desktop_geometry(s,SURFACE_SETTINGS,NULL,NULL));
    assert(surface_desktop_geometry(s,SURFACE_BROWSER,NULL,NULL));
    assert(surface_launch(s,SURFACE_SETTINGS));surface_step(s);
    body=content(s,SURFACE_SETTINGS);click(s,body.x+30,body.y+115);
    assert(surface_close(s)==SURFACE_CLOSE_CONFIRM);tiku_rect_t frozen=frame(s,SURFACE_BROWSER);
    pointer(s,TIKU_EVENT_POINTER_DOWN,frozen.x+60,frozen.y+8);pointer(s,TIKU_EVENT_POINTER_MOVE,600,100);pointer(s,TIKU_EVENT_POINTER_UP,600,100);
    tiku_rect_t stayed=frame(s,SURFACE_BROWSER);assert(!memcmp(&frozen,&stayed,sizeof frozen));
    assert(surface_resolve_close(s,0));finish(&h,&s);
}
/** @brief Changing windows mid-frame cannot change that frame's frozen content. */
static void frozen(void)
{
    surface_host_t a,b;surface_config_t ca=surface_host_config(&a),cb=surface_host_config(&b);cb.paint_rows=7;
    surface_t *sa=surface_create(&ca),*sb=surface_create(&cb);assert(sa&&sb);
    assert(surface_launch(sa,SURFACE_BROWSER)&&surface_launch(sb,SURFACE_BROWSER));
    for(int i=0;i<30;++i){step(&a,sa);step(&b,sb);}
    render(&a,sa);memcpy(expected,a.frame.pixels,sizeof expected);
    assert(surface_render(sb,surface_host_presenter(&b))==TIKU_PRESENT_BUSY);
    assert(surface_launch(sb,SURFACE_SETTINGS));surface_step(sb);
    render(&b,sb);assert(!memcmp(expected,b.frame.pixels,sizeof expected));
    assert(surface_render_pending(sb));finish(&a,&sa);finish(&b,&sb);
}
/** @brief Each failed construction releases every admitted allocation. */
static void failures(void)
{
    for(unsigned n=1;n<=8;++n){surface_host_t h;surface_config_t c=surface_host_config(&h);h.memory.fail_at=n;
        surface_t *s=surface_create(&c);if(s)finish(&h,&s);assert(!h.memory.live&&!h.memory.bytes);}
}
/** @brief Drive window chrome, Deskbar expansion and interrupted pointer capture. */
static void controls(void)
{
    surface_host_t h;surface_config_t c=surface_host_config(&h);surface_t *s=surface_create(&c);assert(s);
    click(s,710,18);click(s,560,50);for(int i=0;i<30;++i)step(&h,s);
    tiku_rect_t r=frame(s,SURFACE_BROWSER);
    pointer(s,TIKU_EVENT_POINTER_DOWN,r.x+10,r.y+8);
    pointer(s,TIKU_EVENT_POINTER_MOVE,790,470);pointer(s,TIKU_EVENT_POINTER_UP,790,470);
    assert(surface_desktop_geometry(s,SURFACE_BROWSER,NULL,NULL));
    click(s,784,54);tiku_rect_t body=content(s,SURFACE_BROWSER);
    click(s,body.x+body.w-30,body.y+16);int hidden=0;
    assert(surface_desktop_geometry(s,SURFACE_BROWSER,NULL,&hidden)&&hidden);
    click(s,730,84);assert(surface_desktop_geometry(s,SURFACE_BROWSER,NULL,&hidden)&&!hidden);
    r=frame(s,SURFACE_BROWSER);
    pointer(s,TIKU_EVENT_POINTER_DOWN,r.x+r.w-5,r.y+r.h-5);
    pointer(s,TIKU_EVENT_POINTER_MOVE,r.x+200,r.y+120);
    pointer(s,TIKU_EVENT_POINTER_UP,r.x+200,r.y+120);
    r=frame(s,SURFACE_BROWSER);assert(r.w==450&&r.h==378);
    pointer(s,TIKU_EVENT_POINTER_DOWN,r.x+60,r.y+8);
    pointer(s,TIKU_EVENT_POINTER_MOVE,600,420);pointer(s,TIKU_EVENT_POINTER_UP,600,420);
    r=frame(s,SURFACE_BROWSER);
    int tab=2*tiku_window_tab_button()+28+tiku_text_width(tiku_font_bold(),"TikuTracker");
    click(s,r.x+tab-10,r.y+8);tiku_rect_t zoomed=frame(s,SURFACE_BROWSER);
    assert(zoomed.w>=450&&zoomed.h>=378&&zoomed.x>=0&&zoomed.y>=0);
    assert(zoomed.x+zoomed.w<=666&&zoomed.y+zoomed.h<=480);
    click(s,zoomed.x+tab-10,zoomed.y+8);tiku_rect_t restored=frame(s,SURFACE_BROWSER);
    assert(!memcmp(&r,&restored,sizeof r));
    pointer(s,TIKU_EVENT_POINTER_DOWN,r.x+60,r.y+8);
    int overflow=0;for(int i=0;i<100;++i)if(!surface_post(s,&(tiku_input_event_t){.type=TIKU_EVENT_KEY_DOWN,.key='x'}))overflow=1;
    assert(overflow);surface_step(s);pointer(s,TIKU_EVENT_POINTER_MOVE,500,200);
    restored=frame(s,SURFACE_BROWSER);assert(!memcmp(&r,&restored,sizeof r));
    finish(&h,&s);
}
int main(void){windows();frozen();failures();controls();puts("PASS: desktop windows, Tracker navigation, frozen frames and allocation failures");return 0;}
