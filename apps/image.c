/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * image.c - queued image snapshots and retained native viewer frames.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_bitmap_progress.h"
#include "tiku_png_progress.h"
#include "tiku_image_model.h"
#include "tiku_ui.h"
#include "tiku_font.h"
#include "tiku_event.h"
#include <string.h>
#define IMAGE_BYTES (1024u*1024u)
#define IMAGE_PIXELS 131072u
#define IMAGE_WIDTH 640
#define IMAGE_HEIGHT 224
/** @brief Decoded source pixels owned by one viewer document. */
typedef struct {
    tiku_owned_buffer_t self;
    unsigned width,height;
    uint64_t length,version;
    uint32_t pixels[];
} image_pixels_t;
/** @brief Immutable rendered pixels retained by shell paint snapshots. */
typedef struct {
    tiku_owned_buffer_t self;
    unsigned references;
    uint32_t pixels[];
} image_frame_t;
/** @brief One published image, one pending replacement and paced conversion state. */
struct surface_image {
    tiku_owned_buffer_t self,encoded,workspace;
    image_pixels_t *source,*incoming;
    image_frame_t *frame,*pending;
    tiku_image_model_t model,proposed,requested;
    tiku_image_drag_t drag;
    int view_queued;
    tiku_image_geometry_t geometry;
    tiku_bitmap_progress_t *decoder;
    tiku_png_progress_t *png;
    _Alignas(max_align_t) unsigned char png_control[8192];
    _Alignas(max_align_t) unsigned char control[256];
    char path[65],target[65],parent[65],draft[65];
    uint64_t epoch,node,after,length,version,next_watch;
    size_t bytes,rendered;
    int job,submitted,cancelled,error,watch,watch_read,is_png;
};
/** @brief Allocate aligned product-owned memory without clearing pixel arrays. */
static int buffer(surface_app_state_t *s,tiku_owned_buffer_t *out,size_t bytes)
{return tiku_buffer_init(out,s->shell->config.allocator,_Alignof(max_align_t))&&tiku_buffer_resize(out,1,bytes,0);}
/** @brief Release a decoded image. */
static void drop_pixels(image_pixels_t *p)
{if(p){tiku_owned_buffer_t memory=p->self;tiku_buffer_dispose(&memory);}}
void surface_image_hold(const void *frame)
{if(frame)++((image_frame_t *)frame)->references;}
void surface_image_drop(const void *frame)
{
    image_frame_t *f=(image_frame_t *)frame;if(!f||--f->references)return;
    tiku_owned_buffer_t memory=f->self;tiku_buffer_dispose(&memory);
}
/** @brief Retire private work after its storage request has completed. */
static void temporary(struct surface_image *i)
{
    tiku_bitmap_progress_cancel(i->decoder);tiku_png_progress_cancel(i->png);
    tiku_buffer_dispose(&i->workspace);tiku_buffer_dispose(&i->encoded);
    drop_pixels(i->incoming);i->incoming=NULL;surface_image_drop(i->pending);i->pending=NULL;
}
/** @brief Finish a job while retaining the published document and frame. */
static void finish(surface_app_state_t *s,tiku_resource_error_t status)
{
    struct surface_image *i=s->image;temporary(i);i->job=i->cancelled=0;
    i->error=status!=TIKU_RESOURCE_OK;i->next_watch=s->shell->now+2000000;
    if(i->error){i->watch=0;i->view_queued=0;tiku_image_drag_cancel(&i->drag);}
    s->status=status;s->shell->dirty=1;
}
int surface_image_start(surface_app_state_t *s)
{
    tiku_owned_buffer_t memory;if(!buffer(s,&memory,sizeof(struct surface_image)))return 0;
    s->image=memory.data;memset(s->image,0,sizeof *s->image);s->image->self=memory;
    s->image->decoder=tiku_bitmap_progress_init(s->image->control,sizeof s->image->control);
    s->image->png=tiku_png_progress_init(s->image->png_control,sizeof s->image->png_control);
    if(!s->image->decoder||!s->image->png)return 0;
    s->store=tiku_paged_storage_create(s->shell->config.allocator,s->shell->config.storage,&s->shell->config.caller,16,1024);
    return s->store!=NULL;
}
void surface_image_dispose(surface_app_state_t *s)
{
    struct surface_image *i=s->image;if(!i)return;
    temporary(i);drop_pixels(i->source);surface_image_drop(i->frame);
    tiku_owned_buffer_t memory=i->self;s->image=NULL;tiku_buffer_dispose(&memory);
}
int surface_image_busy(const surface_app_state_t *s)
{return s&&s->image&&(s->image->job||s->image->submitted);}
/** @brief Accept a canonical absolute media path through 64 bytes. */
static int path_ok(const char *path)
{
    if(!path)return 0;
    size_t n=strlen(path);if(n<2||n>64||path[0]!='/'||path[n-1]=='/')return 0;
    const char *part=path+1;
    for(size_t at=1;at<=n;at++){
        if(path[at]&&((unsigned char)path[at]<32||(unsigned char)path[at]==127||path[at]=='\\'))return 0;
        if(!path[at]||path[at]=='/'){size_t length=(size_t)(path+at-part);
            if(!length||(length==1&&part[0]=='.')||(length==2&&part[0]=='.'&&part[1]=='.'))return 0;
            part=path+at+1;}
    }
    return 1;
}
int surface_image_open(surface_app_state_t *s,const char *path,uint64_t epoch,uint64_t node)
{
    if(!s||!s->image||s->stopped||!path_ok(path))return 0;
    struct surface_image *i=s->image;if(surface_image_busy(s))return 0;
    tiku_image_drag_cancel(&i->drag);
    strcpy(i->target,path);strcpy(i->parent,path);char *slash=strrchr(i->parent,'/');if(slash==i->parent)slash[1]=0;else *slash=0;
    i->epoch=epoch;i->node=node;i->after=i->bytes=0;i->length=i->version=0;i->cancelled=i->watch_read=0;
    i->job=epoch&&node?3:1;i->error=0;s->status=TIKU_RESOURCE_PENDING;s->shell->dirty=1;return 1;
}
/** @brief Queue one bounded enumeration or read while retaining the application. */
static void submit(surface_app_state_t *s)
{
    struct surface_image *i=s->image;
    if(!tiku_app_lease_begin(s->owner,&s->lease)){finish(s,TIKU_RESOURCE_LIMIT);return;}
    if(i->job==1)s->status=tiku_paged_storage_list(s->store,"/",0,0);
    else if(i->job==2)s->status=tiku_paged_storage_list(s->store,i->parent,i->epoch,i->after);
    else s->status=tiku_paged_storage_read(s->store,i->target,i->epoch,i->node,i->bytes);
    i->submitted=tiku_paged_storage_pending(s->store);
    if(!i->submitted){tiku_app_lease_release(&s->lease);finish(s,s->status);}
}
/** @brief Allocate a private rendering frame and freeze its sampling geometry. */
static int render_begin(surface_app_state_t *s,tiku_image_model_t proposed)
{
    struct surface_image *i=s->image;tiku_owned_buffer_t memory;
    if(!tiku_image_model_prepare(&proposed,&i->geometry)||
       !buffer(s,&memory,sizeof(image_frame_t)+(size_t)IMAGE_WIDTH*IMAGE_HEIGHT*4))return 0;
    i->pending=memory.data;i->pending->self=memory;i->pending->references=1;
    i->proposed=proposed;i->rendered=0;i->job=6;s->status=TIKU_RESOURCE_PENDING;s->shell->dirty=1;return 1;
}
/** @brief Allocate a private decoded image after dimension admission. */
static int incoming(surface_app_state_t *s,unsigned width,unsigned height)
{
    struct surface_image *i=s->image;tiku_owned_buffer_t memory;
    if(width>512||height>512){finish(s,TIKU_RESOURCE_LIMIT);return 0;}
    if(!buffer(s,&memory,sizeof(image_pixels_t)+(size_t)width*height*4)){finish(s,TIKU_RESOURCE_LIMIT);return 0;}
    i->incoming=memory.data;i->incoming->self=memory;i->incoming->width=width;i->incoming->height=height;
    i->incoming->length=i->length;i->incoming->version=i->version;
    return 1;
}
void surface_image_poll(surface_app_state_t *s)
{
    struct surface_image *i=s->image;if(!i)return;
    if(i->submitted){
        if(tiku_paged_storage_pending(s->store))return;
        i->submitted=0;tiku_app_lease_release(&s->lease);
        if(s->stopped||i->cancelled){finish(s,TIKU_RESOURCE_CANCELLED);return;}
        size_t count;uint64_t epoch;int eof;
        const tiku_storage_request_t *request=tiku_paged_storage_result(s->store,&count,&epoch,&eof);
        tiku_resource_error_t status=tiku_paged_storage_status(s->store);
        if(!request||status!=TIKU_RESOURCE_OK){finish(s,status);return;}
        if(i->job==1){i->epoch=epoch;i->job=2;return;}
        if(epoch!=i->epoch){finish(s,TIKU_RESOURCE_STALE_CURSOR);return;}
        if(i->job==2){
            int found=0;for(size_t at=0;at<count;at++)if(request->entries[at].kind==TIKU_RESOURCE_STREAM&&!strcmp(request->entries[at].path,i->target)){i->node=request->entries[at].node_id;found=1;break;}
            if(found)i->job=3;else if(count&&!eof)i->after=request->entries[count-1].node_id;
            else finish(s,TIKU_RESOURCE_NOT_FOUND);
            return;
        }
        uint64_t length,version;
        if(!tiku_paged_storage_info(s->store,&length,&version)){finish(s,TIKU_RESOURCE_UNSUPPORTED);return;}
        if(!i->bytes){
            if(!length||length>IMAGE_BYTES){finish(s,TIKU_RESOURCE_LIMIT);return;}
            if(i->watch_read&&i->source&&length==i->source->length&&version==i->source->version){finish(s,TIKU_RESOURCE_OK);return;}
            i->length=length;i->version=version;
            if(!buffer(s,&i->encoded,(size_t)length)){finish(s,TIKU_RESOURCE_LIMIT);return;}
        }
        if(length!=i->length||version!=i->version){finish(s,TIKU_RESOURCE_STALE_CURSOR);return;}
        if(count>i->length-i->bytes||(!count&&!eof)){finish(s,TIKU_RESOURCE_INVALID);return;}
        memcpy((unsigned char *)i->encoded.data+i->bytes,request->bytes,count);i->bytes+=count;
        if(eof){if(i->bytes!=i->length){finish(s,TIKU_RESOURCE_INVALID);return;}i->job=4;}
        else if(i->bytes==i->length){finish(s,TIKU_RESOURCE_INVALID);return;}
        return;
    }
    if(s->stopped){if(i->job)finish(s,TIKU_RESOURCE_CANCELLED);return;}
    if(!i->job){
        if(i->watch&&i->path[0]&&s->shell->now>=i->next_watch){char path[65];strcpy(path,i->path);if(surface_image_open(s,path,0,0))i->watch_read=1;}
        return;
    }
    if(i->job<=3){submit(s);return;}
    if(i->job==4){
        const unsigned char *bytes=i->encoded.data;
        i->is_png=i->bytes>=8&&bytes[0]==137&&!memcmp(bytes+1,"PNG\r\n\032\n",7);
        if(i->is_png){
            if(tiku_png_progress_begin(i->png,bytes,i->bytes,IMAGE_PIXELS)!=TIKU_PNG_MORE){finish(s,TIKU_RESOURCE_INVALID);return;}
            i->job=7;return;
        }
        unsigned width,height;tiku_bitmap_status_t result=tiku_bitmap_info(bytes,i->bytes,IMAGE_PIXELS,&width,&height);
        if(result!=TIKU_BITMAP_READY){finish(s,result==TIKU_BITMAP_UNSUPPORTED?TIKU_RESOURCE_UNSUPPORTED:TIKU_RESOURCE_INVALID);return;}
        if(!incoming(s,width,height))return;
        if(tiku_bitmap_progress_begin(i->decoder,bytes,i->bytes,IMAGE_PIXELS,i->incoming->pixels,(size_t)width*height)!=TIKU_BITMAP_READY){finish(s,TIKU_RESOURCE_INVALID);return;}
        i->job=5;return;
    }
    if(i->job==7){
        int status=TIKU_PNG_MORE;
        for(unsigned turn=0;turn<4&&status==TIKU_PNG_MORE;turn++)status=tiku_png_progress_step(i->png,64);
        if(status==TIKU_PNG_MORE)return;
        unsigned width,height;size_t bytes;
        if(!tiku_png_progress_info(i->png,&width,&height,&bytes)){
            finish(s,status==TIKU_PNG_UNSUPPORTED?TIKU_RESOURCE_UNSUPPORTED:TIKU_RESOURCE_INVALID);return;
        }
        if(!incoming(s,width,height))return;
        if(!buffer(s,&i->workspace,bytes)){finish(s,TIKU_RESOURCE_LIMIT);return;}
        if(tiku_png_progress_output(i->png,i->workspace.data,bytes,i->incoming->pixels,(size_t)width*height)!=TIKU_PNG_MORE){finish(s,TIKU_RESOURCE_INVALID);return;}
        i->job=5;return;
    }
    if(i->job==5){
        if(i->is_png){
            int status=TIKU_PNG_MORE;
            for(unsigned turn=0;turn<4&&status==TIKU_PNG_MORE;turn++)status=tiku_png_progress_step(i->png,64);
            if(status==TIKU_PNG_MORE)return;
            if(status!=TIKU_PNG_DONE){finish(s,TIKU_RESOURCE_INVALID);return;}
        }else{
            tiku_bitmap_status_t status=TIKU_BITMAP_READY;
            for(unsigned turn=0;turn<4&&status==TIKU_BITMAP_READY;turn++)status=tiku_bitmap_progress_step(i->decoder,64);
            if(status==TIKU_BITMAP_READY)return;
            if(status!=TIKU_BITMAP_DONE){finish(s,TIKU_RESOURCE_INVALID);return;}
        }
        tiku_buffer_dispose(&i->workspace);tiku_buffer_dispose(&i->encoded);
        tiku_image_model_t model={i->incoming->width,i->incoming->height,IMAGE_WIDTH,IMAGE_HEIGHT,0,1,1,0,0};
        if(i->watch_read&&i->source){model=i->model;model.width=i->incoming->width;model.height=i->incoming->height;}
        if(!render_begin(s,model))finish(s,TIKU_RESOURCE_LIMIT);
        return;
    }
    image_pixels_t *source=i->incoming?i->incoming:i->source;
    const size_t total=(size_t)IMAGE_WIDTH*IMAGE_HEIGHT;
    for(unsigned turn=0;turn<4&&i->rendered<total;turn++){
        size_t done=tiku_image_model_span(&i->geometry,source->pixels,i->rendered,i->pending->pixels+i->rendered,total-i->rendered);
        if(!done){finish(s,TIKU_RESOURCE_INVALID);return;}i->rendered+=done;
    }
    if(i->rendered<total)return;
    if(i->incoming){drop_pixels(i->source);i->source=i->incoming;i->incoming=NULL;strcpy(i->path,i->target);}
    surface_image_drop(i->frame);i->frame=i->pending;i->pending=NULL;i->model=i->proposed;
    finish(s,TIKU_RESOURCE_OK);
    if(i->view_queued){
        tiku_image_model_t requested=i->requested;i->view_queued=0;
        if(!render_begin(s,requested))finish(s,TIKU_RESOURCE_LIMIT);
    }
}
void surface_image_cancel_input(surface_app_state_t *s)
{if(s&&s->image)tiku_image_drag_cancel(&s->image->drag);}
int surface_image_pointer(surface_app_state_t *s,const tiku_input_event_t *event)
{
    if(!s||!s->image||s->stopped||!event)return 0;
    struct surface_image *i=s->image;
    if(event->type==TIKU_EVENT_POINTER_UP){
        int captured=i->drag.active;tiku_image_drag_cancel(&i->drag);return captured;
    }
    if(!i->source||i->submitted||(i->job&&(i->job!=6||i->incoming)))return 0;
    int inside=event->x>=32&&event->x<32+IMAGE_WIDTH&&event->y>=200&&event->y<200+IMAGE_HEIGHT;
    if(event->type==TIKU_EVENT_POINTER_DOWN&&event->button==1&&inside){
        tiku_image_drag_begin(&i->drag,event->x,event->y);return 1;
    }
    tiku_image_model_t model=i->view_queued?i->requested:i->job==6?i->proposed:i->model;
    int changed=0;
    if(event->type==TIKU_EVENT_POINTER_MOVE)changed=tiku_image_drag_move(&i->drag,&model,event->x,event->y);
    else if(event->type==TIKU_EVENT_WHEEL&&inside)
        changed=tiku_image_model_wheel(&model,event->dy,event->modifiers&TIKU_MOD_SHIFT,event->modifiers&TIKU_MOD_CMD);
    if(!changed)return 0;
    if(i->job==6){i->requested=model;i->view_queued=1;}
    else if(!render_begin(s,model))finish(s,TIKU_RESOURCE_LIMIT);
    return 1;
}
void surface_image_action(surface_app_state_t *s,int action)
{
    struct surface_image *i=s->image;if(!i||s->stopped)return;
    if(action==ACTION_IMAGE_CANCEL){i->watch=0;if(i->submitted){i->cancelled=1;tiku_paged_storage_cancel(s->store);}else finish(s,TIKU_RESOURCE_CANCELLED);return;}
    if(surface_image_busy(s))return;
    if(action==ACTION_IMAGE_OPEN){strcpy(i->draft,i->path);surface_text_open(s->shell);return;}
    if(action==ACTION_IMAGE_RELOAD){char path[65];strcpy(path,i->path);(void)surface_image_open(s,path,0,0);return;}
    if(!i->source)return;
    if(action==ACTION_IMAGE_WATCH){i->watch^=1;i->next_watch=s->shell->now+2000000;s->shell->dirty=1;return;}
    tiku_image_model_t model=i->model;double zoom=tiku_image_model_zoom(&model);
    if(action==ACTION_IMAGE_FIT){model.fit=1;model.pan_x=model.pan_y=0;}
    else if(action==ACTION_IMAGE_ACTUAL)(void)tiku_image_model_scale(&model,1);
    else if(action==ACTION_IMAGE_IN)(void)tiku_image_model_scale(&model,zoom*1.25);
    else if(action==ACTION_IMAGE_OUT)(void)tiku_image_model_scale(&model,zoom/1.25);
    else if(action==ACTION_IMAGE_ROTATE){model.rotation=(model.rotation+1)%4;model.pan_x=model.pan_y=0;}
    else if(action==ACTION_IMAGE_LEFT)model.pan_x+=32;
    else if(action==ACTION_IMAGE_RIGHT)model.pan_x-=32;
    else if(action==ACTION_IMAGE_UP)model.pan_y+=32;
    else if(action==ACTION_IMAGE_DOWN)model.pan_y-=32;
    else return;
    if(!render_begin(s,model))finish(s,TIKU_RESOURCE_LIMIT);
}
void surface_image_key(surface_app_state_t *s,const tiku_input_event_t *event)
{
    if(!event||event->type!=TIKU_EVENT_KEY_DOWN)return;
    unsigned key=event->key;int action=0;
    if((event->modifiers&TIKU_MOD_CMD)&&key=='o')action=ACTION_IMAGE_OPEN;
    else if((event->modifiers&TIKU_MOD_CMD)&&key=='r')action=ACTION_IMAGE_RELOAD;
    else if(key=='+'||key=='=')action=ACTION_IMAGE_IN;
    else if(key=='-')action=ACTION_IMAGE_OUT;
    else if(key=='f'||key=='0')action=ACTION_IMAGE_FIT;
    else if(key=='1')action=ACTION_IMAGE_ACTUAL;
    else if(key=='r')action=ACTION_IMAGE_ROTATE;
    else if(key==TIKU_KEY_LEFT)action=ACTION_IMAGE_LEFT;
    else if(key==TIKU_KEY_RIGHT)action=ACTION_IMAGE_RIGHT;
    else if(key==TIKU_KEY_UP)action=ACTION_IMAGE_UP;
    else if(key==TIKU_KEY_DOWN)action=ACTION_IMAGE_DOWN;
    if(action)surface_image_action(s,action);
}
const char *surface_image_draft(const surface_app_state_t *s){return s->image->draft;}
int surface_image_accept(surface_app_state_t *s,const char *path){return surface_image_open(s,path,0,0);}
void surface_image_view(const surface_app_state_t *s,surface_view_t *v)
{
    const struct surface_image *i=s->image;v->status=s->status;v->image_frame=i->frame;strcpy(v->image_path,i->path);
    v->image_busy=surface_image_busy(s);v->image_error=i->error;v->image_watch=i->watch;
    if(i->source){v->image_width=i->source->width;v->image_height=i->source->height;v->image_zoom=tiku_image_model_zoom(&i->model);v->image_rotation=i->model.rotation;v->image_pan_x=i->model.pan_x;v->image_pan_y=i->model.pan_y;}
}
/** @brief Shared toolbar and pan control rectangles. */
static tiku_rect_t rect(int action)
{
    if(action==ACTION_IMAGE_CANCEL)action=ACTION_IMAGE_RELOAD;
    if(action>=ACTION_IMAGE_LEFT)return (tiku_rect_t){688,200+(action-ACTION_IMAGE_LEFT)*56,80,44};
    int n=action-ACTION_IMAGE_OPEN;return (tiku_rect_t){32+(n%4)*188,96+(n/4)*48,176,40};
}
/** @brief Whether a control may act on the copied viewer state. */
static int enabled(const surface_view_t *v,int action)
{
    if(action==ACTION_IMAGE_CANCEL)return v->image_busy;
    if(v->image_busy)return 0;
    return action==ACTION_IMAGE_OPEN||v->image_frame!=NULL;
}
int surface_image_hit(const surface_view_t *v,int x,int y)
{
    for(int action=ACTION_IMAGE_OPEN;action<=ACTION_IMAGE_DOWN;action++){
        if(action==ACTION_IMAGE_CANCEL&&!v->image_busy)continue;
        tiku_rect_t r=rect(action);
        if(enabled(v,action)&&x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h)return action;
    }
    return 0;
}
void surface_image_paint(tiku_surface_t *surface,const surface_view_t *v)
{
    static const char *labels[]={"Open image...","Reload","Fit","1:1","Zoom out","Zoom in","Rotate","Watch 2s","Cancel","Left","Right","Up","Down"};
    for(int action=ACTION_IMAGE_OPEN;action<=ACTION_IMAGE_DOWN;action++){
        if(action==ACTION_IMAGE_CANCEL||(action==ACTION_IMAGE_RELOAD&&v->image_busy))continue;
        unsigned flags=enabled(v,action)?0:TIKU_S_DISABLED;if(v->pressed==action)flags|=TIKU_S_PRESSED;
        tiku_ui_button(surface,rect(action),action==ACTION_IMAGE_WATCH&&v->image_watch?"Watching 2s":labels[action-ACTION_IMAGE_OPEN],flags);
    }
    if(v->image_busy)tiku_ui_button(surface,rect(ACTION_IMAGE_CANCEL),"Cancel",v->pressed==ACTION_IMAGE_CANCEL?TIKU_S_PRESSED:0);
    tiku_rect_t area={32,200,IMAGE_WIDTH,IMAGE_HEIGHT};tiku_fill(surface,area,0x303030);
    const image_frame_t *frame=v->image_frame;
    if(frame){
        int first=surface->clip.y>area.y?surface->clip.y-area.y:0;
        int end=surface->clip.y+surface->clip.h-area.y;if(end>IMAGE_HEIGHT)end=IMAGE_HEIGHT;
        for(int row=first;row<end;row++)
            tiku_picture(surface,area.x,area.y+row,IMAGE_WIDTH,1,frame->pixels+(size_t)row*IMAGE_WIDTH);
    }
    else{
        tiku_text(surface,tiku_font_bold(),56,272,"Open an image from Tracker or choose Open image.",0xffffff);
        tiku_text(surface,tiku_font_plain(),56,304,"BMP / PNG: 1 MiB encoded, 131072 pixels, 512 pixels per side.",0xffffff);
    }
    tiku_frame(surface,area,0x707070);
}
