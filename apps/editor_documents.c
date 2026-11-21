/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * editor_documents.c - retained Editor sessions and serial storage ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_event.h"
#include "tiku_ui.h"
#include "tiku_font.h"
#include "tiku_text_layout.h"
#include <string.h>
#define DOCUMENT_COUNT 8
/** @brief Child sessions retain independent models under the outer app lease. */
struct surface_editor_documents {
    tiku_owned_buffer_t self;
    surface_app_state_t slots[DOCUMENT_COUNT];
    unsigned used[DOCUMENT_COUNT];
    int selected,worker,page,list,confirm,opening,previous,allocated,error,notice;
    char opening_path[65];
};
surface_app_state_t *surface_editor_session(surface_app_state_t *s)
{
    if(!s||!s->documents)return s;
    int i=s->documents->selected;
    return i>=0?&s->documents->slots[i]:NULL;
}
/** @brief Release a child whose provider request has retired. */
static void clear(surface_app_state_t *s,unsigned i)
{
    struct surface_editor_documents *d=s->documents;surface_app_state_t *child=&d->slots[i];
    tiku_paged_storage_close(child->store);(void)tiku_paged_storage_destroy(&child->store);
    surface_editor_dispose(child);memset(child,0,sizeof *child);d->used[i]=0;
}
/** @brief Construct one child, rolling back every partially allocated resource. */
static int create(surface_app_state_t *s,unsigned i)
{
    struct surface_editor_documents *d=s->documents;
    d->slots[i]=(surface_app_state_t){.shell=s->shell,.owner=s->owner,.id=SURFACE_EDITOR_APP};
    if(!surface_editor_start(&d->slots[i])){clear(s,i);d->notice=s->status=TIKU_RESOURCE_LIMIT;return 0;}
    d->used[i]=1;d->selected=(int)i;d->list=d->confirm=d->notice=0;s->status=TIKU_RESOURCE_OK;return 1;
}
int surface_editor_documents_start(surface_app_state_t *s)
{
    tiku_owned_buffer_t memory;
    if(!tiku_buffer_init(&memory,s->shell->config.allocator,_Alignof(struct surface_editor_documents))||
       !tiku_buffer_resize(&memory,1,sizeof(struct surface_editor_documents),1))return 0;
    s->documents=memory.data;s->documents->self=memory;s->documents->selected=s->documents->worker=s->documents->opening=-1;
    return create(s,0);
}
void surface_editor_documents_stop(surface_app_state_t *s)
{
    if(!s||!s->documents)return;
    for(unsigned i=0;i<DOCUMENT_COUNT;++i){surface_app_state_t *c=&s->documents->slots[i];c->stopped=1;tiku_paged_storage_close(c->store);}
}
void surface_editor_documents_dispose(surface_app_state_t *s)
{
    struct surface_editor_documents *d=s->documents;if(!d)return;
    for(unsigned i=0;i<DOCUMENT_COUNT;++i)clear(s,i);
    tiku_owned_buffer_t memory=d->self;s->documents=NULL;tiku_buffer_dispose(&memory);
}
int surface_editor_documents_modified(const surface_app_state_t *s)
{
    for(unsigned i=0;i<DOCUMENT_COUNT;++i)if(s->documents->used[i]&&surface_editor_modified(&s->documents->slots[i]))return 1;
    return 0;
}
/** @brief Find an existing path, a virgin session, or an unused slot. */
static int destination(const surface_app_state_t *s,const char *path,int *existing)
{
    const struct surface_editor_documents *d=s->documents;int free_slot=-1;*existing=0;
    for(unsigned i=0;i<DOCUMENT_COUNT;++i){
        if(!d->used[i]){if(free_slot<0)free_slot=(int)i;continue;}
        if(d->used[i]!=1)continue;
        surface_editor_info_t info;surface_editor_info(&d->slots[i],&info);
        if(path&&info.path[0]&&!strcmp(info.path,path)){*existing=1;return (int)i;}
        if(!info.path[0]&&!info.bytes&&!info.dirty&&!info.busy)free_slot=(int)i;
    }
    return free_slot;
}
int surface_editor_can_open(const surface_app_state_t *s,const char *path)
{
    if(!s)return 1;
    if(!s->documents)return !surface_editor_modified(s)&&!surface_editor_busy(s);
    if(s->stopped||s->shell->text_field.active)return 0;
    int existing,index=destination(s,path,&existing);return index>=0&&(existing||s->documents->opening<0);
}
int surface_editor_documents_open(surface_app_state_t *s,const char *path,uint64_t epoch,uint64_t node)
{
    if(!path||!surface_editor_can_open(s,path))return 0;
    int existing,index=destination(s,path,&existing);struct surface_editor_documents *d=s->documents;
    if(index<0)return 0;
    if(existing){d->selected=index;d->list=d->confirm=0;s->shell->dirty=1;return 1;}
    int allocated=!d->used[index],previous=d->selected;
    if(allocated&&!create(s,(unsigned)index))return 0;
    if(!surface_editor_open(&d->slots[index],path,epoch,node)){
        if(allocated)clear(s,(unsigned)index);
        d->selected=previous;return 0;
    }
    d->opening=index;d->previous=previous;d->allocated=allocated;strcpy(d->opening_path,path);
    d->selected=index;d->list=d->confirm=d->error=0;s->shell->dirty=1;return 1;
}
void surface_editor_documents_poll(surface_app_state_t *s)
{
    struct surface_editor_documents *d=s->documents;
    if(s->stopped){for(unsigned i=0;i<DOCUMENT_COUNT;++i)if(d->used[i])surface_editor_poll(&d->slots[i]);return;}
    if(d->worker<0)for(unsigned i=0;i<DOCUMENT_COUNT;++i)if(d->used[i]&&surface_editor_busy(&d->slots[i])){d->worker=(int)i;break;}
    if(d->worker>=0){surface_app_state_t *c=&d->slots[d->worker];surface_editor_poll(c);
        if(!surface_editor_busy(c)){
            s->status=c->stopped?TIKU_RESOURCE_CANCELLED:c->status;
            if(d->opening==d->worker){
                surface_editor_info_t info;surface_editor_info(c,&info);
                if(strcmp(info.path,d->opening_path)){
                    d->error=info.error;
                    if(d->allocated){int failed=d->opening;clear(s,(unsigned)failed);if(d->selected==failed)d->selected=d->previous;
                        surface_app_state_t *previous=surface_editor_session(s);if(previous)previous->status=s->status;}
                }
                d->opening=-1;
            }
            d->worker=-1;s->shell->dirty=1;
        }}
    for(unsigned i=0;i<DOCUMENT_COUNT;++i)if(d->used[i]==2&&!tiku_paged_storage_pending(d->slots[i].store)){
        surface_editor_poll(&d->slots[i]);clear(s,i);s->shell->dirty=1;
    }
}
/** @brief Retire the selected session and select another retained document. */
static void close_selected(surface_app_state_t *s)
{
    struct surface_editor_documents *d=s->documents;int selected=d->selected;if(selected<0)return;
    surface_app_state_t *c=&d->slots[selected];c->stopped=1;tiku_paged_storage_close(c->store);d->used[selected]=2;
    d->selected=-1;d->confirm=0;d->list=1;
    for(unsigned i=0;i<DOCUMENT_COUNT;++i)if(d->used[i]==1){d->selected=(int)i;break;}
}
int surface_editor_documents_back(surface_app_state_t *s)
{
    struct surface_editor_documents *d=s->documents;
    if(d->confirm)d->confirm=0;
    else if(d->list&&d->selected>=0)d->list=0;
    else return 0;
    s->shell->dirty=1;return 1;
}
int surface_editor_documents_action(surface_app_state_t *s,int action)
{
    struct surface_editor_documents *d=s->documents;
    if(s->shell->text_field.active)return 1;
    if(action==ACTION_EDITOR_KEEP)d->confirm=0;
    else if(action==ACTION_EDITOR_DISCARD&&d->confirm)close_selected(s);
    else if(d->confirm)return 1;
    else if(action==ACTION_EDITOR_SESSIONS)d->list=1;
    else if(action==ACTION_EDITOR_SESSION_CLOSE){
        if(surface_editor_modified(surface_editor_session(s)))d->confirm=1;else close_selected(s);
    }else if(action==ACTION_EDITOR_SESSION_NEW){
        unsigned i;for(i=0;i<DOCUMENT_COUNT&&d->used[i];++i){}
        if(i<DOCUMENT_COUNT)(void)create(s,i);else d->notice=s->status=TIKU_RESOURCE_LIMIT;
    }else if(action==ACTION_EDITOR_SESSION_PREVIOUS)d->page=0;
    else if(action==ACTION_EDITOR_SESSION_NEXT)d->page=1;
    else if(action>=ACTION_EDITOR_SESSION_ROW&&action<ACTION_EDITOR_SESSION_ROW+6){
        unsigned i=(unsigned)(d->page*6+action-ACTION_EDITOR_SESSION_ROW);
        if(i<DOCUMENT_COUNT&&d->used[i]==1){d->selected=(int)i;d->list=0;}
    }else {d->error=d->notice=0;return d->list;}
    s->shell->dirty=1;return 1;
}
int surface_editor_documents_key(surface_app_state_t *s,const tiku_input_event_t *e)
{
    if(e->type==TIKU_EVENT_KEY_DOWN&&(e->modifiers&TIKU_MOD_CMD)&&e->key=='n')return surface_editor_documents_action(s,ACTION_EDITOR_SESSION_NEW);
    if(e->type==TIKU_EVENT_KEY_DOWN&&e->key==TIKU_KEY_ESCAPE&&surface_editor_documents_back(s))return 1;
    return s->documents->list||s->documents->confirm;
}
void surface_editor_documents_view(surface_app_state_t *s,surface_view_t *v)
{
    struct surface_editor_documents *d=s->documents;surface_app_state_t *c=surface_editor_session(s);
    if(c)surface_editor_view(c,v);
    if(d->error)v->editor_error=d->error;
    v->editor_sessions=1;v->editor_list=d->list;v->editor_confirm=d->confirm;v->editor_page=(unsigned)d->page;
    for(unsigned i=0;i<DOCUMENT_COUNT;++i)if(d->used[i])++v->editor_count;
    for(unsigned row=0;row<6;++row){unsigned i=(unsigned)d->page*6+row;if(i>=DOCUMENT_COUNT||!d->used[i])continue;
        surface_editor_info_t info;surface_editor_info(&d->slots[i],&info);
        strcpy(v->editor_titles[row],info.path[0]?info.path:"Untitled");
        v->editor_states[row]=1u|(info.dirty?2u:0)|(info.busy?4u:0)|(d->selected==(int)i?8u:0)|(d->used[i]==2?16u:0);
    }
    if(d->list||d->confirm)v->status=s->status;
    if(d->notice)v->status=(tiku_resource_error_t)d->notice;
    for(unsigned i=0;i<DOCUMENT_COUNT;++i)if(d->used[i]==2)v->status=TIKU_RESOURCE_PENDING;
}
/** @brief Document rows and commands have disjoint touch targets. */
static tiku_rect_t command(unsigned i)
{return (tiku_rect_t){32+(int)i*188,388,176,44};}
int surface_editor_documents_hit(const surface_view_t *v,int x,int y)
{
    if(v->editor_confirm){if(y>=250&&y<294){if(x>=220&&x<396)return ACTION_EDITOR_KEEP;if(x>=408&&x<584)return ACTION_EDITOR_DISCARD;}return 0;}
    if(y>=112&&y<376&&x>=32&&x<768){unsigned row=(unsigned)(y-112)/44;return v->editor_states[row]&&!(v->editor_states[row]&16)?ACTION_EDITOR_SESSION_ROW+(int)row:0;}
    static const int actions[]={ACTION_EDITOR_SESSION_NEW,ACTION_EDITOR_SESSION_CLOSE,ACTION_EDITOR_SESSION_PREVIOUS,ACTION_EDITOR_SESSION_NEXT};
    for(unsigned i=0;i<4;++i){tiku_rect_t r=command(i);if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return actions[i];}return 0;
}
void surface_editor_documents_paint(tiku_surface_t *s,const surface_view_t *v)
{
    const tiku_font_t *font=tiku_font_plain();
    if(v->editor_confirm){tiku_text(s,font,80,180,"Close this document and discard its unsaved changes?",TIKU_C_TEXT);
        tiku_ui_button(s,(tiku_rect_t){220,250,176,44},"Keep document",0);tiku_ui_button(s,(tiku_rect_t){408,250,176,44},"Discard changes",0);return;}
    tiku_text(s,font,32,104,"Documents: eight retained files, up to 8 KiB each",TIKU_C_TEXT);
    for(unsigned row=0;row<6;++row){unsigned state=v->editor_states[row];if(!state)continue;char title[65];
        tiku_rect_t r={32,112+(int)row*44,736,44};tiku_ui_button(s,r,"",state&8?TIKU_S_PRESSED:0);
        (void)tiku_text_elide(0,v->editor_titles[row],500,title,sizeof title);tiku_text(s,font,44,r.y+27,title,TIKU_C_TEXT);
        tiku_text(s,font,568,r.y+27,state&16?"Closing":state&4?"Working":state&2?"Unsaved":"Saved",TIKU_C_TEXT);
    }
    static const char *labels[]={"New document","Close current","Previous","Next"};
    for(unsigned i=0;i<4;++i)tiku_ui_button(s,command(i),labels[i],0);
}
