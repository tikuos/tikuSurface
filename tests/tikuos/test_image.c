/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_image.c - image loading, view controls, paging and retained frames.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_EDITOR_MAIN editor_fixture_main
#include "test_editor.c"
#include "../png_fixture.h"
static size_t picture_work;
void __real_tiku_picture(tiku_surface_t *,int,int,int,int,const uint32_t *);
/** @brief Count submitted image pixels before clipping, including discarded work. */
void __wrap_tiku_picture(tiku_surface_t *s,int x,int y,int w,int h,const uint32_t *pixels)
{picture_work+=(size_t)w*h;__real_tiku_picture(s,x,y,w,h,pixels);}
/** @brief Store a little-endian word in a BMP header. */
static void word(unsigned char *p,uint32_t value)
{for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(value>>(8*i));}
/** @brief Create a native bitmap with position-dependent RGB channels. */
static void bitmap(const char *name,unsigned width,unsigned height)
{
    size_t stride=((size_t)width*3+3)&~(size_t)3,n=54+stride*height;
    unsigned char *b=calloc(1,n);assert(b);b[0]='B';b[1]='M';word(b+10,54);word(b+14,40);word(b+18,width);word(b+22,height);b[28]=24;
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){unsigned char *p=b+54+y*stride+x*3;p[0]=(unsigned char)(x*255/width);p[1]=(unsigned char)(y*255/height);p[2]=(unsigned char)((x+y)*5);}
    uint64_t id;assert(!tiku_mfs_begin(&fs,name,TIKU_MFS_STREAM,0,NULL,&id));
    for(size_t at=0;at<n;){size_t count=n-at;if(count>4096)count=4096;assert(!tiku_mfs_append(&fs,b+at,count));at+=count;}
    assert(!tiku_mfs_finalize(&fs));free(b);
}
/** @brief Append an encoded fixture through the media store's chunk limit. */
static void encoded(const char *name,const unsigned char *bytes,size_t length)
{
    uint64_t id;assert(!tiku_mfs_begin(&fs,name,TIKU_MFS_STREAM,0,NULL,&id));
    for(size_t at=0;at<length;){size_t n=length-at;if(n>4096)n=4096;assert(!tiku_mfs_append(&fs,bytes+at,n));at+=n;}
    assert(!tiku_mfs_finalize(&fs));
}
/** @brief Advance media and application work until an image job retires. */
static surface_view_t image_settle(surface_t *s,surface_media_t *media)
{
    surface_view_t v;
    for(unsigned turn=0;turn<400000;turn++){
        surface_step(s);surface_media_poll(media);surface_view(s,&v);
        if(v.foreground==SURFACE_IMAGE_APP&&!v.image_busy&&!v.pending)return v;
    }
    assert(0);return v;
}
/** @brief Full and clipped painting must match for the copied frame. */
static void painting(surface_t *s)
{
    static uint32_t whole[800*480],sliced[800*480];surface_view_t v;surface_view(s,&v);
    tiku_surface_t a={.px=whole,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=sliced;
    picture_work=0;surface_paint(&a,&v);size_t full_work=picture_work;picture_work=0;
    for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}
    assert(picture_work==full_work);assert(!memcmp(whole,sliced,sizeof whole));
}
/** @brief Deliver one pointer event while allowing one bounded rendering turn. */
static void pointer(surface_t *s,tiku_event_type_t type,int x,int y,int travel,unsigned modifiers)
{
    tiku_input_event_t event={.type=type,.button=1,.x=x,.y=y,.dy=travel,.modifiers=modifiers};
    assert(surface_post(s,&event));surface_step(s);
}
/** @brief Exercise captured input and rendering coalescence through the shell queue. */
static void pointers(surface_t *s,surface_media_t *media,surface_host_t *host)
{
    key(s,'1',0);surface_view_t v=image_settle(s,media);
    for(unsigned i=0;i<4;i++){key(s,'+',0);v=image_settle(s,media);}
    const void *frame=v.image_frame;surface_image_hold(frame);size_t calls=host->memory.calls;
    pointer(s,TIKU_EVENT_POINTER_DOWN,200,260,0,0);
    pointer(s,TIKU_EVENT_POINTER_MOVE,224,280,0,0);surface_view(s,&v);
    assert(v.image_busy&&v.image_frame==frame&&host->memory.calls==calls+1);
    pointer(s,TIKU_EVENT_POINTER_MOVE,260,300,0,0);
    pointer(s,TIKU_EVENT_POINTER_UP,700,100,0,0);
    assert(host->memory.calls==calls+1);v=image_settle(s,media);
    assert(v.image_pan_x==60&&v.image_pan_y==40&&v.image_frame!=frame);surface_image_drop(frame);
    frame=v.image_frame;pointer(s,TIKU_EVENT_POINTER_MOVE,300,350,0,0);surface_view(s,&v);
    assert(!v.image_busy&&v.image_frame==frame);
    pointer(s,TIKU_EVENT_WHEEL,100,250,2,0);v=image_settle(s,media);assert(v.image_pan_y==8);
    pointer(s,TIKU_EVENT_WHEEL,100,250,3,TIKU_MOD_SHIFT);v=image_settle(s,media);assert(v.image_pan_x==12);
    double zoom=v.image_zoom;
    pointer(s,TIKU_EVENT_WHEEL,100,250,-1,TIKU_MOD_CMD|TIKU_MOD_SHIFT);v=image_settle(s,media);
    assert(v.image_zoom==zoom*1.25&&v.image_pan_x==15&&v.image_pan_y==10);frame=v.image_frame;
    pointer(s,TIKU_EVENT_WHEEL,700,250,-1,TIKU_MOD_CMD);surface_view(s,&v);assert(!v.image_busy&&v.image_frame==frame);
    pointer(s,TIKU_EVENT_POINTER_DOWN,200,260,0,0);
    pointer(s,TIKU_EVENT_DEACTIVATED,200,260,0,0);
    pointer(s,TIKU_EVENT_POINTER_MOVE,250,310,0,0);surface_view(s,&v);assert(!v.image_busy&&v.image_frame==frame);
    pointer(s,TIKU_EVENT_POINTER_DOWN,200,260,0,0);
    for(unsigned i=0;i<9;i++){tiku_input_event_t event={.type=TIKU_EVENT_KEY_UP,.key='a'+i};(void)surface_post(s,&event);}
    surface_step(s);pointer(s,TIKU_EVENT_POINTER_MOVE,250,310,0,0);surface_view(s,&v);assert(!v.image_busy&&v.image_frame==frame);
    pointer(s,TIKU_EVENT_POINTER_DOWN,200,260,0,0);
    assert(surface_launch(s,SURFACE_HOME));surface_step(s);assert(surface_launch(s,SURFACE_IMAGE_APP));v=image_settle(s,media);
    pointer(s,TIKU_EVENT_POINTER_MOVE,250,310,0,0);surface_view(s,&v);assert(!v.image_busy&&v.image_frame==frame);
    pointer(s,TIKU_EVENT_POINTER_DOWN,200,260,0,0);pointer(s,TIKU_EVENT_POINTER_MOVE,224,280,0,0);
    pointer(s,TIKU_EVENT_POINTER_MOVE,260,300,0,0);surface_image_action(s->active.state,ACTION_IMAGE_CANCEL);
    v=image_settle(s,media);assert(v.image_frame==frame&&v.status==TIKU_RESOURCE_CANCELLED);
    pointer(s,TIKU_EVENT_POINTER_MOVE,270,320,0,0);surface_view(s,&v);assert(!v.image_busy&&v.image_frame==frame);
    painting(s);
}
int main(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();
    bitmap("a.bmp",80,40);bitmap("wide",513,1);bitmap("max",512,256);bitmap("pixels",512,257);document("bad","BMbroken",8);bitmap("data",32,16);
    static unsigned char png[1048576];size_t png_bytes=png_fixture(png,sizeof png,512,256,sizeof png);
    encoded("maximum.png",png,png_bytes);
    png_bytes=png_fixture(png,sizeof png,7,9,0);encoded("valid.png",png,png_bytes);
    png[png_bytes-1]^=1;encoded("corrupt.png",png,png_bytes);
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    cfg.storage=surface_media_init(&media,bridge);cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s);surface_view_t v;surface_view(s,&v);
    assert(surface_hit(&v,700,110)==ACTION_LAUNCH_NEXT);file_tap(s,&media,700,110);surface_view(s,&v);
    assert(v.home_page==1&&surface_hit(&v,100,190)==ACTION_IMAGE);painting(s);file_capture(s,&host,"home-image-page");
    file_tap(s,&media,100,190);v=image_settle(s,&media);assert(v.foreground==SURFACE_IMAGE_APP&&!v.image_frame);
    file_tap(s,&media,100,114);surface_view(s,&v);assert(v.text_entry);file_name(s,"/a.bmp");v=image_settle(s,&media);
    assert(v.image_frame&&v.image_width==80&&v.image_height==40&&!v.image_error&&!strcmp(v.image_path,"/a.bmp"));
    painting(s);file_capture(s,&host,"image-viewer");const void *first=v.image_frame;
    file_tap(s,&media,450,164);v=image_settle(s,&media);assert(v.image_rotation==1&&v.image_frame!=first);painting(s);
    key(s,'1',0);v=image_settle(s,&media);assert(v.image_zoom==1);key(s,'+',0);v=image_settle(s,&media);assert(v.image_zoom==1.25);
    key(s,'f',0);v=image_settle(s,&media);assert(v.image_zoom==2.8);first=v.image_frame;
    const char *bad[]={"/bad","/missing","/wide","/pixels"};
    for(unsigned i=0;i<4;i++){assert(surface_image_open(s->active.state,bad[i],0,0));v=image_settle(s,&media);assert(v.image_error&&v.image_frame==first&&!strcmp(v.image_path,"/a.bmp"));}
    assert(!surface_image_open(s->active.state,"/../a.bmp",0,0));
    assert(surface_image_open(s->active.state,"/data",0,0));v=image_settle(s,&media);assert(!v.image_error&&v.image_width==32&&!strcmp(v.image_path,"/data"));
    first=v.image_frame;size_t baseline=host.memory.bytes;
    for(unsigned fail=1;fail<=5;fail++){
        host.memory.fail_at=host.memory.calls+fail;
        assert(surface_image_open(s->active.state,"/max",0,0));v=image_settle(s,&media);host.memory.fail_at=0;
        if(v.image_error){assert(v.image_frame==first&&host.memory.bytes==baseline);}
        else{assert(v.image_width==512&&v.image_height==256);first=v.image_frame;baseline=host.memory.bytes;}
    }
    assert(surface_image_open(s->active.state,"/a.bmp",0,0));v=image_settle(s,&media);first=v.image_frame;
    assert(surface_image_open(s->active.state,"/max",0,0));
    for(unsigned i=0;i<10;i++){surface_step(s);surface_media_poll(&media);}
    surface_view(s,&v);assert(v.image_busy&&surface_hit(&v,280,114)==ACTION_IMAGE_CANCEL);file_tap(s,&media,280,114);v=image_settle(s,&media);assert(v.image_frame==first&&v.status==TIKU_RESOURCE_CANCELLED);
    tiku_mfs_object_t object;uint64_t rank,epoch;assert(!tiku_mfs_enumerate(&fs,"a.bmp",0,0,&object,&rank,&epoch));file("bump");
    assert(surface_image_open(s->active.state,"/a.bmp",epoch,rank+1));v=image_settle(s,&media);assert(v.status==TIKU_RESOURCE_STALE_CURSOR&&v.image_frame==first);
    file_tap(s,&media,650,164);surface_view(s,&v);assert(v.image_watch);host.now+=2000001;v=image_settle(s,&media);assert(v.image_watch&&v.image_frame==first);
    file_tap(s,&media,650,164);surface_view(s,&v);assert(!v.image_watch);
    file_tap(s,&media,580,30);surface_view(s,&v);assert(v.applications_menu&&surface_hit(&v,620,405)==ACTION_LAUNCH_NEXT);
    file_tap(s,&media,620,405);surface_view(s,&v);assert(v.task_page==1&&surface_hit(&v,100,176)==ACTION_TASK+12);painting(s);file_capture(s,&host,"applications-image-page");
    key(s,'3',0);v=image_settle(s,&media);assert(v.foreground==SURFACE_IMAGE_APP&&!v.applications_menu&&v.image_frame==first);
    assert(surface_launch(s,SURFACE_BROWSER));settle(s,&media);surface_view(s,&v);
    unsigned row=0;while(row<v.row_count&&strcmp(v.rows[row],"a.bmp"))row++;
    assert(row<v.row_count);file_tap(s,&media,70,130+44*(int)row);settle(s,&media);surface_view(s,&v);
    assert(v.document_default==SURFACE_IMAGE_APP);file_tap(s,&media,500,130);v=image_settle(s,&media);
    assert(v.image_frame&&!v.image_error&&!strcmp(v.image_path,"/a.bmp"));
    assert(surface_image_open(s->active.state,"/valid.png",0,0));v=image_settle(s,&media);
    assert(!v.image_error&&v.image_width==7&&v.image_height==9);painting(s);first=v.image_frame;
    assert(surface_image_open(s->active.state,"/corrupt.png",0,0));v=image_settle(s,&media);
    assert(v.image_error&&v.image_frame==first&&!strcmp(v.image_path,"/valid.png"));
    baseline=host.memory.bytes;
    for(unsigned fail=1;fail<=7;fail++){
        host.memory.fail_at=host.memory.calls+fail;
        assert(surface_image_open(s->active.state,"/maximum.png",0,0));v=image_settle(s,&media);host.memory.fail_at=0;
        if(v.image_error){assert(v.image_frame==first&&host.memory.bytes==baseline);}
        else{assert(v.image_width==512&&v.image_height==256);first=v.image_frame;baseline=host.memory.bytes;}
    }
    static const unsigned stops[]={0,700,60000,120000,180000,240000,300000};
    for(unsigned trial=0;trial<sizeof stops/sizeof *stops;trial++){
        unsigned stop=stops[trial];
        assert(surface_image_open(s->active.state,"/maximum.png",0,0));
        for(unsigned n=0;n<stop;n++){surface_step(s);surface_media_poll(&media);}
        surface_view(s,&v);
        if(v.image_busy){surface_image_action(s->active.state,ACTION_IMAGE_CANCEL);v=image_settle(s,&media);assert(v.image_frame==first&&host.memory.bytes==baseline&&v.status==TIKU_RESOURCE_CANCELLED);}
        else{assert(!v.image_error);first=v.image_frame;baseline=host.memory.bytes;}
    }
    assert(surface_image_open(s->active.state,"/maximum.png",0,0));v=image_settle(s,&media);assert(!v.image_error&&v.image_width==512);painting(s);file_capture(s,&host,"image-png");
    pointers(s,&media,&host);file_capture(s,&host,"image-pointer");
    s->config.paint_rows=1;s->dirty=1;assert(surface_render(s,surface_host_presenter(&host))==TIKU_PRESENT_BUSY);
    assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);settle(s,&media);
    for(unsigned n=0;n<480&&s->painting;n++)(void)surface_render(s,surface_host_presenter(&host));
    host.now=host.frame_due;surface_host_poll(&host);assert(!s->painting);
    assert(surface_launch(s,SURFACE_IMAGE_APP));v=image_settle(s,&media);assert(!v.image_frame);
    assert(surface_image_open(s->active.state,"/max",0,0));for(unsigned n=0;n<20;n++){surface_step(s);surface_media_poll(&media);}
    surface_stop(s);for(unsigned n=0;n<60000&&!surface_destroy(&s);n++){surface_step(s);surface_media_poll(&media);}assert(!s&&!host.memory.live&&!store.count&&!store.running);
    free(bridge);free(disk);puts("Image: media, controls, paging, refusal, cancellation and retained paint passed");return 0;
}
