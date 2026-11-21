/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * desktop.c - local desktop windows over queued Tracker and settings owners.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_window.h"
#include "tiku_ui.h"
#include "tiku_list.h"
#include "tiku_deskbar_paint.h"
#include "tiku_word_model.h"
#include <string.h>

enum { DESK_TRACKER=0, DESK_SETTINGS, DESK_WINDOWS,
       DESK_VOLUME=4000, DESK_PREFS, DESK_MENU, DESK_HIDE, DESK_OPEN, DESK_SELECT, DESK_EXPAND=4010 };
/** @brief One immutable window's content for an incremental frame. */
typedef struct {
    surface_view_t view;
    int selected;
} desktop_paint_t;
/** @brief Caller-owned workspaces and the desktop's captured input. */
struct surface_desktop {
    tiku_owned_buffer_t self,control,render;
    tiku_workspace_t *workspace,*snapshot;
    tiku_surface_t logical,output;
    tiku_window_t *windows[DESK_WINDOWS],*pressed;
    tiku_hit_t pressed_part;
    tiku_press_t press;
    tiku_list_t list;
    desktop_paint_t paint[DESK_WINDOWS];
    int menu,paint_menu,selected,last_row;
    unsigned expanded,paint_expanded;
    uint64_t last_click;
    char path[65];
    unsigned offset;
    uint64_t epoch;
};
/** @brief Return the product task represented by a desktop window. */
static surface_app_t task(int i) { return i==DESK_TRACKER?SURFACE_BROWSER:SURFACE_SETTINGS; }
/** @brief Return an owned task regardless of keyboard focus. */
static surface_app_state_t *state(surface_t *s,int i)
{ surface_app_t id=task(i);return s->active.state&&s->active.state->id==id?s->active.state:s->background[id].state; }
/** @brief Test a point against a half-open control rectangle. */
static int inside(tiku_rect_t r,int x,int y)
{ return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h; }
/** @brief Draw a label within its own horizontal bounds and the caller clip. */
static void label(tiku_surface_t *s,tiku_rect_t r,const char *name,int bold,tiku_rgb_t ink)
{
    tiku_rect_t clip=s->clip;
    int x=r.x>clip.x?r.x:clip.x,y=r.y>clip.y?r.y:clip.y;
    int right=r.x+r.w<clip.x+clip.w?r.x+r.w:clip.x+clip.w;
    int bottom=r.y+r.h<clip.y+clip.h?r.y+r.h:clip.y+clip.h;
    if(right<=x||bottom<=y)return;
    tiku_clip_set(s,(tiku_rect_t){x,y,right>x?right-x:0,bottom>y?bottom-y:0});
    tiku_text(s,bold?tiku_font_bold():tiku_font_plain(),r.x+4,r.y+18,name,ink);
    tiku_clip_set(s,clip);
}
/** @brief Return the content-relative rectangle of a Tracker or settings control. */
static tiku_rect_t control(tiku_rect_t c,int id)
{
    if(id==ACTION_BACK)return(tiku_rect_t){c.x+4,c.y+4,62,28};
    if(id==ACTION_REFRESH)return(tiku_rect_t){c.x+70,c.y+4,76,28};
    if(id==DESK_HIDE)return(tiku_rect_t){c.x+c.w-64,c.y+4,60,28};
    if(id>=ACTION_ROW&&id<ACTION_ROW+SURFACE_BROWSER_ROWS)return(tiku_rect_t){c.x+4,c.y+70+(id-ACTION_ROW)*28,c.w-8,28};
    if(id==ACTION_PREVIOUS)return(tiku_rect_t){c.x+4,c.y+c.h-34,74,28};
    if(id==ACTION_NEXT)return(tiku_rect_t){c.x+82,c.y+c.h-34,64,28};
    if(id==DESK_OPEN)return(tiku_rect_t){c.x+150,c.y+c.h-34,64,28};
    if(id==DESK_SELECT)return(tiku_rect_t){c.x+218,c.y+c.h-34,100,28};
    if(id==ACTION_FILE_NEW)return(tiku_rect_t){c.x+150,c.y+4,80,28};
    if(id==ACTION_FOLDER_NEW)return(tiku_rect_t){c.x+234,c.y+4,92,28};
    if(id>=ACTION_FILE_RENAME&&id<=ACTION_FILE_DELETE)return(tiku_rect_t){c.x+4+(id-ACTION_FILE_RENAME)*138,c.y+c.h-68,132,28};
    if(id==ACTION_FILE_COPY)return(tiku_rect_t){c.x+280,c.y+c.h-68,114,28};
    if(id==ACTION_FILE_INFO)return(tiku_rect_t){c.x+4,c.y+38,114,28};
    if(id>=ACTION_SWAP&&id<=ACTION_SAVE)return(tiku_rect_t){c.x+12+((id-ACTION_SWAP)%2)*164,c.y+100+((id-ACTION_SWAP)/2)*40,156,32};
    if(id==ACTION_DEFAULTS)return(tiku_rect_t){c.x+12,c.y+190,156,32};
    return(tiku_rect_t){0};
}
/** @brief Paint a native button at the same rectangle used for input. */
static void button(tiku_surface_t *s,tiku_rect_t c,int id,const char *title,int enabled)
{
    tiku_rect_t r=control(c,id),clip=s->clip;
    if(r.x>=clip.x+clip.w||r.y>=clip.y+clip.h||r.x+r.w<=clip.x||r.y+r.h<=clip.y)return;
    tiku_ui_button(s,r,title,enabled?0:TIKU_S_DISABLED);
}
/** @brief Paint copied Tracker or device settings state inside a native window. */
static void draw_content(tiku_window_t *w,tiku_surface_t *s,tiku_rect_t c,void *ctx)
{
    (void)w;desktop_paint_t *p=ctx;const surface_view_t *v=&p->view;
    button(s,c,DESK_HIDE,"Hide",1);
    if(v->foreground==SURFACE_SETTINGS){
        int editable=v->settings_status!=SURFACE_SETTINGS_PENDING&&v->settings_status!=SURFACE_SETTINGS_UNAVAILABLE&&v->settings_status!=SURFACE_SETTINGS_INCOMPATIBLE;
        label(s,(tiku_rect_t){c.x+8,c.y+42,c.w-16,24},"Touch calibration",1,TIKU_C_TEXT);
        label(s,(tiku_rect_t){c.x+8,c.y+70,c.w-16,24},v->settings_dirty?"Unsaved changes":"Device settings",0,TIKU_C_TEXT);
        button(s,c,ACTION_SWAP,v->calibration.swap?"Swap axes: on":"Swap axes: off",editable);
        button(s,c,ACTION_INVERT_X,v->calibration.invert_x?"Invert X: on":"Invert X: off",editable);
        button(s,c,ACTION_INVERT_Y,v->calibration.invert_y?"Invert Y: on":"Invert Y: off",editable);
        button(s,c,ACTION_SAVE,"Save",editable);
        button(s,c,ACTION_DEFAULTS,"Defaults",editable);return;
    }
    button(s,c,ACTION_BACK,"Up",1);button(s,c,ACTION_REFRESH,v->file_job?"Cancel":"Refresh",1);
    if(!v->reading){button(s,c,ACTION_FILE_NEW,"New file",v->file_create);
        button(s,c,ACTION_FOLDER_NEW,v->file_upgrade?"Folders...":"New folder",v->file_create||v->file_upgrade);}
    label(s,(tiku_rect_t){c.x+4,c.y+38,c.w-8,28},v->reading?v->file_path:v->path,1,TIKU_C_TEXT);
    if(!v->reading){
        for(unsigned i=0;i<v->row_count;++i){tiku_rect_t r=control(c,ACTION_ROW+(int)i);
            int selected=p->selected==(int)i;tiku_fill(s,r,selected?TIKU_C_SELECT:TIKU_C_DOC);
            tiku_rgb_t ink=selected?TIKU_C_SELTEXT:TIKU_C_TEXT;
            surface_icon_scaled(s,r.x+6,r.y+6,v->directory[i]?1:2,16);
            label(s,(tiku_rect_t){r.x+26,r.y,r.w-108,r.h},v->rows[i],0,ink);
            label(s,(tiku_rect_t){r.x+r.w-80,r.y,76,r.h},v->directory[i]?"Folder":"File",0,ink);
        }
        if(!v->row_count)label(s,(tiku_rect_t){c.x+8,c.y+76,c.w-16,28},v->status==TIKU_RESOURCE_PENDING?"Reading folder...":v->status==TIKU_RESOURCE_OK?"This folder is empty.":"Folder unavailable. Refresh to retry.",0,TIKU_C_TEXT);
        button(s,c,ACTION_PREVIOUS,"Previous",v->previous);button(s,c,ACTION_NEXT,"Next",v->more);
        button(s,c,DESK_OPEN,"Open",p->selected>=0);button(s,c,DESK_SELECT,"File actions",p->selected>=0);
    }else{
        label(s,(tiku_rect_t){c.x+8,c.y+74,c.w-16,28},v->file_directory?"Folder":v->preview_hex?"Binary file":"Text file",1,TIKU_C_TEXT);
        if(v->file_info_known){char n[65];tiku_word_format(v->file_length,10,n);
            label(s,(tiku_rect_t){c.x+8,c.y+104,100,24},"Bytes",0,TIKU_C_TEXT);
            label(s,(tiku_rect_t){c.x+108,c.y+104,c.w-116,24},n,0,TIKU_C_TEXT);}
        if(!v->file_directory){size_t at=0;for(int row=0;row<4&&v->preview[at];++row){char line[81]={0};unsigned n=0;
            while(n<80&&v->preview[at]&&v->preview[at]!='\n')line[n++]=v->preview[at++];
            if(v->preview[at]=='\n')++at;
            label(s,(tiku_rect_t){c.x+8,c.y+132+row*22,c.w-16,22},line,0,TIKU_C_TEXT);}}
        button(s,c,ACTION_FILE_RENAME,"Rename / move",v->file_actions);
        button(s,c,ACTION_FILE_DELETE,"Delete...",v->file_actions);
        button(s,c,ACTION_FILE_COPY,"Copy to...",v->file_actions);
        button(s,c,ACTION_FILE_INFO,"Information",!v->file_directory&&v->status==TIKU_RESOURCE_OK);
    }
    if(v->status!=TIKU_RESOURCE_OK)label(s,(tiku_rect_t){c.x+8,c.y+c.h-100,c.w-16,24},
        v->status==TIKU_RESOURCE_PENDING?"Working...":v->status==TIKU_RESOURCE_STALE?"Folder changed. Refresh to continue.":"Operation failed. Refresh to retry.",0,TIKU_C_TEXT);
}
/** @brief Draw desktop volume and settings entry points behind the windows. */
static void backdrop(tiku_surface_t *s,void *context)
{
    (void)context;surface_icon_paint(s,30,38,1);label(s,(tiku_rect_t){14,88,110,26},"Volumes",1,0xffffff);
    surface_icon_paint(s,30,140,2);label(s,(tiku_rect_t){14,190,110,26},"Settings",1,0xffffff);
}
int surface_desktop_create(surface_t *s)
{
    tiku_owned_buffer_t self;
    if(!tiku_buffer_init(&self,s->config.allocator,_Alignof(struct surface_desktop))||
       !tiku_buffer_resize(&self,1,sizeof(struct surface_desktop),1))return 0;
    struct surface_desktop *d=self.data;s->desktop=d;d->self=self;
    d->logical=(tiku_surface_t){.w=800,.h=480,.scale=1};
    d->output=d->logical;d->selected=d->last_row=-1;tiku_list_init(&d->list,1);
    if(!tiku_buffer_init(&d->control,s->config.allocator,tiku_workspace_align())||
       !tiku_buffer_init(&d->render,s->config.allocator,tiku_workspace_align())||
       !tiku_buffer_resize(&d->control,1,tiku_workspace_size(),0)||
       !tiku_buffer_resize(&d->render,1,tiku_workspace_size(),0))return 0;
    d->workspace=tiku_workspace_init(d->control.data,d->control.bytes,&d->logical,NULL);
    d->snapshot=tiku_workspace_init(d->render.data,d->render.bytes,&d->output,NULL);
    if(!d->workspace||!d->snapshot)return 0;
    tiku_workspace_set_reserved(d->workspace,(tiku_rect_t){666,0,134,480});
    tiku_workspace_set_backdrop(d->snapshot,backdrop,NULL);return 1;
}
void surface_desktop_destroy(surface_t *s)
{
    struct surface_desktop *d=s->desktop;if(!d)return;
    tiku_workspace_deinit(d->workspace);tiku_workspace_deinit(d->snapshot);
    tiku_buffer_dispose(&d->control);tiku_buffer_dispose(&d->render);
    tiku_owned_buffer_t self=d->self;s->desktop=NULL;tiku_buffer_dispose(&self);
}
void surface_desktop_cancel(surface_t *s)
{
    struct surface_desktop *d=s->desktop;if(!d)return;
    tiku_workspace_end_track(d->workspace);
    if(d->pressed)d->pressed->pressed=0;
    d->pressed=NULL;d->pressed_part=TIKU_HIT_NONE;(void)tiku_press_release(&d->press,0);
    d->last_row=-1;
}
void surface_desktop_sync(surface_t *s,int focus)
{
    struct surface_desktop *d=s->desktop;if(!d)return;
    int removed=0;
    for(int i=0;i<DESK_WINDOWS;++i){surface_app_state_t *a=state(s,i);tiku_window_t *w=d->windows[i];
        if(!a){if(w){if(d->pressed==w)surface_desktop_cancel(s);tiku_workspace_close(d->workspace,w);d->windows[i]=NULL;removed=1;}continue;}
        if(!w){w=tiku_workspace_open(d->workspace,i==DESK_TRACKER?"TikuTracker":"Settings",
            i==DESK_TRACKER?(tiku_rect_t){128,60,522,378}:(tiku_rect_t){200,110,390,300},NULL,NULL,NULL);d->windows[i]=w;}
        if(focus&&s->active.state==a){tiku_workspace_restore(d->workspace,w);tiku_workspace_activate(d->workspace,w);}
    }
    if(removed&&!s->active.state&&!s->stopped){
        for(int i=tiku_workspace_count(d->workspace)-1;i>=0;--i){
            tiku_window_t *w=tiku_workspace_at(d->workspace,i);if(w->minimized)continue;
            for(int n=0;n<DESK_WINDOWS;++n)if(d->windows[n]==w){(void)surface_launch(s,task(n));break;}
            break;
        }
    }
    if(focus&&!s->active.state)tiku_workspace_deactivate(d->workspace,tiku_workspace_focused(d->workspace));
    surface_app_state_t *a=state(s,DESK_TRACKER);
    if(a&&(strcmp(d->path,a->browser->path)||d->offset!=a->offset||d->epoch!=a->epoch)){
        memcpy(d->path,a->browser->path,sizeof d->path);d->offset=(unsigned)a->offset;d->epoch=a->epoch;
        d->selected=d->last_row=-1;tiku_list_init(&d->list,1);
    }
}
/** @brief Return the task slot for a live native window. */
static int slot(struct surface_desktop *d,tiku_window_t *w)
{ for(int i=0;i<DESK_WINDOWS;++i)if(d->windows[i]==w)return i;return -1; }
/** @brief Admit a content action using the same copied state as its disabled appearance. */
static int enabled(const surface_view_t *v,int id)
{
    if(id==ACTION_FILE_NEW)return v->file_create;
    if(id==ACTION_FOLDER_NEW)return v->file_create||v->file_upgrade;
    if(id==ACTION_FILE_RENAME||id==ACTION_FILE_DELETE||id==ACTION_FILE_COPY)return v->file_actions;
    if(id==ACTION_FILE_INFO)return !v->file_directory&&v->status==TIKU_RESOURCE_OK;
    if(id==ACTION_PREVIOUS)return v->previous;
    if(id==ACTION_NEXT)return v->more;
    if(id>=ACTION_SWAP&&id<=ACTION_DEFAULTS)return v->settings_status!=SURFACE_SETTINGS_PENDING&&v->settings_status!=SURFACE_SETTINGS_UNAVAILABLE&&v->settings_status!=SURFACE_SETTINGS_INCOMPATIBLE;
    return 1;
}
/** @brief Resolve content controls from a copied view. */
static int content_hit(const surface_view_t *v,tiku_rect_t c,int x,int y)
{
    if(!inside(c,x,y))return 0;
    if(inside(control(c,DESK_HIDE),x,y))return DESK_HIDE;
    if(v->foreground==SURFACE_SETTINGS){for(int id=ACTION_SWAP;id<=ACTION_DEFAULTS;++id)if(enabled(v,id)&&inside(control(c,id),x,y))return id;return 0;}
    const int fixed[]={ACTION_BACK,ACTION_REFRESH};
    for(unsigned i=0;i<sizeof fixed/sizeof *fixed;++i)if(inside(control(c,fixed[i]),x,y))return fixed[i];
    if(v->reading){const int ids[]={ACTION_FILE_RENAME,ACTION_FILE_DELETE,ACTION_FILE_COPY,ACTION_FILE_INFO};
        for(unsigned i=0;i<sizeof ids/sizeof *ids;++i)if(enabled(v,ids[i])&&inside(control(c,ids[i]),x,y))return ids[i];
    }else{
        const int ids[]={ACTION_FILE_NEW,ACTION_FOLDER_NEW,ACTION_PREVIOUS,ACTION_NEXT,DESK_OPEN,DESK_SELECT};
        for(unsigned i=0;i<sizeof ids/sizeof *ids;++i)if(enabled(v,ids[i])&&inside(control(c,ids[i]),x,y))return ids[i];
        for(unsigned i=0;i<v->row_count;++i)if(inside(control(c,ACTION_ROW+(int)i),x,y))return ACTION_ROW+(int)i;
    }
    return 0;
}
/** @brief Open the selected row or retain it for file operations. */
static void open_selected(surface_t *s,int select)
{
    struct surface_desktop *d=s->desktop;surface_app_state_t *a=s->active.state;
    if(!a||a->id!=SURFACE_BROWSER||d->selected<0)return;
    a->browser->selecting=select;surface_app_action(a,ACTION_ROW+d->selected);d->selected=d->last_row=-1;
}
/** @brief Minimize a window and return keyboard ownership to the desktop. */
static void hide(surface_t *s,tiku_window_t *w)
{
    tiku_workspace_minimize(s->desktop->workspace,w,(tiku_rect_t){666,40,134,30});
    (void)surface_launch(s,SURFACE_HOME);
}
/** @brief Toggle zoom while keeping the embedded content controls on screen. */
static void zoom(struct surface_desktop *d,tiku_window_t *w)
{
    if(w->zoom_valid){w->frame=w->saved_zoom;w->zoom_valid=0;return;}
    int tracker=w==d->windows[DESK_TRACKER];
    (void)tiku_workspace_zoom(d->workspace,w,640,400);
    if(w->frame.w<(tracker?450:370))w->frame.w=tracker?450:370;
    if(w->frame.h<(tracker?378:280))w->frame.h=tracker?378:280;
    if(w->frame.x+w->frame.w>661)w->frame.x=661-w->frame.w;
    if(w->frame.y+w->frame.h>470)w->frame.y=470-w->frame.h;
    if(w->frame.x<0)w->frame.x=0;
    if(w->frame.y<0)w->frame.y=0;
}
int surface_desktop_event(surface_t *s,const tiku_input_event_t *e)
{
    struct surface_desktop *d=s->desktop;if(!d||s->close_confirmation)return 0;
    if(e->type==TIKU_EVENT_DEACTIVATED){surface_desktop_cancel(s);return 0;}
    if(e->type==TIKU_EVENT_KEY_DOWN){
        if(e->key==TIKU_KEY_ESCAPE){d->menu=0;surface_desktop_cancel(s);return 1;}
        tiku_window_t *w=tiku_workspace_focused(d->workspace);int n=slot(d,w);
        if(n<0)return 1;
        if((e->modifiers&TIKU_MOD_CMD)&&e->key=='w'){(void)surface_close(s);return 1;}
        if((e->modifiers&TIKU_MOD_CMD)&&e->key=='m'){hide(s,w);return 1;}
        if(n==DESK_TRACKER){surface_view_t v;surface_view(s,&v);
            if(e->key==TIKU_KEY_RETURN){open_selected(s,0);return 1;}
            if(e->key==TIKU_KEY_BACKSPACE){surface_action(s,ACTION_BACK);return 1;}
            if(e->key==TIKU_KEY_PAGE_DOWN||e->key==TIKU_KEY_PAGE_UP){surface_action(s,e->key==TIKU_KEY_PAGE_DOWN?ACTION_NEXT:ACTION_PREVIOUS);return 1;}
            tiku_list_set_count(&d->list,(int)v.row_count);
            if(tiku_list_key(&d->list,e->key,e->modifiers,(tiku_rect_t){0,0,500,180})){d->selected=d->list.cursor;return 1;}
        }
        return 1;
    }
    if(e->type!=TIKU_EVENT_POINTER_DOWN&&e->type!=TIKU_EVENT_POINTER_MOVE&&e->type!=TIKU_EVENT_POINTER_UP)return 1;
    if(e->type==TIKU_EVENT_POINTER_MOVE&&tiku_workspace_tracking(d->workspace)){(void)tiku_workspace_track(d->workspace,e->x,e->y);
        for(int i=0;i<DESK_WINDOWS;++i){tiku_window_t *resize=d->windows[i];if(resize){
            int width=i==DESK_TRACKER?450:370,height=i==DESK_TRACKER?378:280;
            if(resize->frame.w<width)resize->frame.w=width;
            if(resize->frame.h<height)resize->frame.h=height;}}return 1;}
    if(e->type==TIKU_EVENT_POINTER_UP&&e->button==1&&tiku_workspace_tracking(d->workspace)){tiku_workspace_end_track(d->workspace);return 1;}
    tiku_window_t *w=NULL;tiku_hit_t part=tiku_workspace_hit(d->workspace,e->x,e->y,&w);
    int id=0,bar_bottom=40;
    for(int i=0;i<DESK_WINDOWS;++i)if(d->windows[i])bar_bottom+=32+((d->expanded&(1u<<i))?28:0);
    if(e->x>=666&&e->y<36)id=DESK_MENU;
    else if(d->menu){if(inside((tiku_rect_t){530,36,270,32},e->x,e->y))id=DESK_VOLUME;
        else if(inside((tiku_rect_t){530,68,270,32},e->x,e->y))id=DESK_PREFS;
        w=NULL;part=TIKU_HIT_NONE;
    }else if(e->x>=666&&e->y>=40&&e->y<bar_bottom){int y=40;
        for(int i=0;i<DESK_WINDOWS;++i)if(d->windows[i]){
            if(e->y>=y&&e->y<y+32){id=e->x>=778?DESK_EXPAND+i:i==DESK_TRACKER?DESK_VOLUME:DESK_PREFS;w=NULL;part=TIKU_HIT_NONE;break;}
            y+=32;
            if(d->expanded&(1u<<i)){if(e->y>=y&&e->y<y+28){id=i==DESK_TRACKER?DESK_VOLUME:DESK_PREFS;w=NULL;part=TIKU_HIT_NONE;break;}y+=28;}
        }
    }
    else if(!w){if(inside((tiku_rect_t){14,30,110,90},e->x,e->y))id=DESK_VOLUME;
        else if(inside((tiku_rect_t){14,132,110,90},e->x,e->y))id=DESK_PREFS;}
    else if(part==TIKU_HIT_CONTENT){surface_view_t v;int n=slot(d,w);surface_view_state(s,state(s,n),&v);id=content_hit(&v,tiku_window_content(w),e->x,e->y);}
    if(e->type==TIKU_EVENT_POINTER_DOWN&&e->button==1){
        if((id>=DESK_VOLUME&&id<=DESK_MENU)||(id>=DESK_EXPAND&&id<DESK_EXPAND+DESK_WINDOWS)){(void)tiku_press_arm(&d->press,id);return 1;}
        if(w&&!d->menu){int n=slot(d,w);if(n>=0&&s->active.state!=state(s,n))(void)surface_launch(s,task(n));
            tiku_workspace_activate(d->workspace,w);
            if(part==TIKU_HIT_TAB||part==TIKU_HIT_RESIZE){tiku_workspace_begin_track(d->workspace,w,part,e->x,e->y);return 1;}
            if(part==TIKU_HIT_CLOSE||part==TIKU_HIT_ZOOM){d->pressed=w;d->pressed_part=part;w->pressed=part;return 1;}}
        (void)tiku_press_arm(&d->press,id);return 1;
    }
    if(e->type==TIKU_EVENT_POINTER_MOVE){if(d->pressed)d->pressed->pressed=w==d->pressed&&part==d->pressed_part?part:0;
        (void)tiku_press_track(&d->press,id);return 1;}
    if(e->type==TIKU_EVENT_POINTER_UP&&e->button==1){
        if(d->pressed){tiku_window_t *held=d->pressed;tiku_hit_t what=d->pressed_part;
            d->pressed=NULL;held->pressed=0;
            if(w==held&&part==what){if(what==TIKU_HIT_CLOSE)(void)surface_close(s);
                else zoom(d,w);}return 1;}
        id=tiku_press_release(&d->press,id);
        if(id==DESK_MENU){d->menu^=1;return 1;}
        if(id>=DESK_EXPAND&&id<DESK_EXPAND+DESK_WINDOWS){d->expanded^=1u<<(id-DESK_EXPAND);return 1;}
        if(id==DESK_VOLUME||id==DESK_PREFS){d->menu=0;(void)surface_launch(s,id==DESK_VOLUME?SURFACE_BROWSER:SURFACE_SETTINGS);return 1;}
        if(d->menu){d->menu=0;return 1;}
        if(id==DESK_HIDE){hide(s,w);return 1;}
        if(id==DESK_OPEN||id==DESK_SELECT){open_selected(s,id==DESK_SELECT);return 1;}
        if(id>=ACTION_ROW&&id<ACTION_ROW+SURFACE_BROWSER_ROWS){int row=id-ACTION_ROW;
            if(d->last_row==row&&s->now>=d->last_click&&s->now-d->last_click<=500000){d->selected=row;open_selected(s,0);}
            else{surface_view_t v;surface_view(s,&v);tiku_list_set_count(&d->list,(int)v.row_count);tiku_list_click(&d->list,row,0);
                d->selected=d->list.cursor;d->last_row=row;d->last_click=s->now;}return 1;}
        if(id==ACTION_BACK){(void)surface_app_back(s->active.state);return 1;}
        if(id)surface_action(s,id);
        return 1;
    }
    return 1;
}
void surface_desktop_snapshot(surface_t *s)
{
    struct surface_desktop *d=s->desktop;surface_desktop_sync(s,0);
    d->paint_menu=d->menu;d->paint_expanded=d->expanded;
    for(int n=0;n<DESK_WINDOWS;++n){
        tiku_window_t *copy=tiku_workspace_find(d->snapshot,&d->paint[n]);
        if(copy&&!d->windows[n])tiku_workspace_close(d->snapshot,copy);
    }
    for(int order=0;order<tiku_workspace_count(d->workspace);++order){
        tiku_window_t *w=tiku_workspace_at(d->workspace,order);int n=slot(d,w);
        if(n<0)continue;
        surface_view_state(s,state(s,n),&d->paint[n].view);d->paint[n].selected=d->selected;
        tiku_window_t *copy=tiku_workspace_find(d->snapshot,&d->paint[n]);
        if(!copy)copy=tiku_workspace_open(d->snapshot,w->title,w->frame,draw_content,&d->paint[n],&d->paint[n]);
        else{copy->frame=w->frame;tiku_workspace_activate(d->snapshot,copy);}
        copy->minimized=w->minimized;copy->pressed=w->pressed;
    }
    tiku_window_t *focus=tiku_workspace_focused(d->workspace);
    int focused=slot(d,focus);
    tiku_window_t *copy=focused>=0?tiku_workspace_find(d->snapshot,&d->paint[focused]):NULL;
    if(copy)tiku_workspace_activate(d->snapshot,copy);
    else tiku_workspace_deactivate(d->snapshot,tiku_workspace_focused(d->snapshot));
}
void surface_desktop_paint(surface_t *s,tiku_surface_t *out)
{
    struct surface_desktop *d=s->desktop;
    if(s->painted.text_entry){surface_paint(out,&s->painted);return;}
    d->output=*out;tiku_workspace_draw(d->snapshot);
    int height=40;
    for(int n=0;n<DESK_WINDOWS;++n)if(tiku_workspace_find(d->snapshot,&d->paint[n]))height+=32+((d->paint_expanded&(1u<<n))?28:0);
    tiku_ui_raised(out,(tiku_rect_t){666,0,134,height+4});
    tiku_deskbar_row_paint(out,(tiku_rect_t){668,2,130,34},d->paint_menu);
    label(out,(tiku_rect_t){676,8,116,24},"TikuOS",1,TIKU_C_TEXT);
    int y=40;for(int n=0;n<DESK_WINDOWS;++n){
        tiku_window_t *copy=tiku_workspace_find(d->snapshot,&d->paint[n]);
        if(copy){int focused=copy==tiku_workspace_focused(d->snapshot);
            tiku_deskbar_row_paint(out,(tiku_rect_t){668,y,130,31},focused);
            tiku_deskbar_window_icon(out,674,y+11,copy->minimized,0);
            label(out,(tiku_rect_t){687,y+3,90,26},n==DESK_TRACKER?"Tracker":"Settings",0,TIKU_C_TEXT);
            tiku_deskbar_expander(out,782,y+11,!!(d->paint_expanded&(1u<<n)));
            y+=32;
            if(d->paint_expanded&(1u<<n)){
                tiku_deskbar_row_paint(out,(tiku_rect_t){668,y,130,27},focused);
                tiku_deskbar_window_icon(out,690,y+9,copy->minimized,0);
                label(out,(tiku_rect_t){702,y+1,92,24},n==DESK_TRACKER?d->paint[n].view.path:"Settings",0,TIKU_C_TEXT);y+=28;
            }
        }
    }
    if(d->paint_menu){tiku_ui_raised(out,(tiku_rect_t){530,36,270,68});
        label(out,(tiku_rect_t){534,40,262,28},"Volumes",0,TIKU_C_TEXT);
        label(out,(tiku_rect_t){534,72,262,28},"Settings",0,TIKU_C_TEXT);}
    if(s->painted.close_confirmation){const surface_view_t *v=&s->painted;
        int deleting=v->close_confirmation==2,upgrade=v->close_confirmation==4;
        tiku_ui_window(out,(tiku_rect_t){224,160,352,168},deleting?"Delete permanently?":upgrade?"Enable folders?":"Unsaved settings",1);
        label(out,(tiku_rect_t){240,192,320,28},deleting?(v->file_directory?"Delete this folder and its contents?":"Delete this file from storage?"):upgrade?"Existing files are preserved.":"These changes have not been saved.",1,TIKU_C_TEXT);
        label(out,(tiku_rect_t){240,224,320,28},deleting?v->file_path:upgrade?"Older firmware cannot open this volume.":"Keep editing or discard this draft?",0,TIKU_C_TEXT);
        tiku_ui_button(out,(tiku_rect_t){240,270,152,44},deleting?"Keep":upgrade?"Keep current":"Keep editing",v->pressed==ACTION_KEEP?TIKU_S_PRESSED:0);
        tiku_ui_button(out,(tiku_rect_t){408,270,152,44},deleting?"Delete":upgrade?"Enable folders":"Discard",v->pressed==ACTION_DISCARD?TIKU_S_PRESSED:0);
    }
}

int surface_desktop_geometry(const surface_t *s,surface_app_t id,tiku_rect_t *frame,int *minimized)
{
    if(!s||!s->desktop)return 0;
    for(int i=0;i<DESK_WINDOWS;++i)if(task(i)==id&&s->desktop->windows[i]){
        const tiku_window_t *w=s->desktop->windows[i];
        if(frame)*frame=w->frame;
        if(minimized)*minimized=w->minimized;
        return 1;
    }
    return 0;
}
