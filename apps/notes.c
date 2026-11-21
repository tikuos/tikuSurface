/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * notes.c - retained notes, quiet autosave and queued catalog ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_event.h"
#include "tiku_ui.h"
#include "tiku_font.h"
#include "tiku_text_layout.h"
#include <stdio.h>
#include <limits.h>
#include <string.h>
#define NOTE_COUNT 24
/** @brief One independent model keeps its draft and save clock across selection. */
typedef struct {
    surface_app_state_t session;
    char path[65];
    uint64_t node,revision,metadata,due;
    unsigned used,force;
} note_slot_t;
/** @brief Catalog requests and child leases share the outer application's lifetime. */
struct surface_notes {
    tiku_owned_buffer_t self;
    note_slot_t slots[NOTE_COUNT];
    uint64_t epoch,after;
    unsigned phase,submitted,loading,cursor,next_number,page,count;
    int selected,tools,confirm,new_pending,directory,allocation_error;
};
/** @brief Return the selected retained document, or NULL on the overview. */
surface_app_state_t *surface_notes_editor(surface_app_state_t *s)
{
    struct surface_notes *n=s?s->notes:NULL;
    return n&&n->selected>=0?&n->slots[n->selected].session:NULL;
}
/** @brief Dispose one idle child after its storage provider has retired. */
static void clear(note_slot_t *slot)
{
    tiku_paged_storage_close(slot->session.store);
    (void)tiku_paged_storage_destroy(&slot->session.store);
    surface_editor_dispose(&slot->session);memset(slot,0,sizeof *slot);
}
/** @brief Allocate a retained note session without starting a storage request. */
static int child(surface_app_state_t *s,unsigned index,const char *path)
{
    note_slot_t *slot=&s->notes->slots[index];
    slot->session=(surface_app_state_t){.shell=s->shell,.owner=s->owner,.id=SURFACE_NOTES_APP};
    if(!surface_editor_start(&slot->session)||!surface_editor_note(&slot->session,path)){clear(slot);s->notes->allocation_error=1;s->status=TIKU_RESOURCE_LIMIT;return 0;}
    strcpy(slot->path,path);slot->used=1;++s->notes->count;return 1;
}
/** @brief Allocate an unused native filename after explicit creation was requested. */
static void new_note(surface_app_state_t *s)
{
    struct surface_notes *n=s->notes;unsigned i;
    for(i=0;i<NOTE_COUNT&&n->slots[i].used;++i){}
    if(i==NOTE_COUNT||!n->next_number){s->status=TIKU_RESOURCE_LIMIT;return;}
    char path[65];snprintf(path,sizeof path,"/Notes/note-%03u.txt",n->next_number);
    if(child(s,i,path)){++n->next_number;n->allocation_error=0;n->selected=(int)i;n->tools=0;n->slots[i].due=s->shell->now+500000;}
    s->shell->dirty=1;
}
int surface_notes_start(surface_app_state_t *s)
{
    tiku_owned_buffer_t memory;
    if(!tiku_buffer_init(&memory,s->shell->config.allocator,_Alignof(struct surface_notes))||
       !tiku_buffer_resize(&memory,1,sizeof(struct surface_notes),1))return 0;
    s->notes=memory.data;s->notes->self=memory;s->notes->selected=-1;s->notes->next_number=1;s->notes->phase=4;
    s->store=tiku_paged_storage_create(s->shell->config.allocator,s->shell->config.storage,&s->shell->config.caller,16,1024);
    s->status=TIKU_RESOURCE_PENDING;return s->store!=NULL;
}
void surface_notes_stop(surface_app_state_t *s)
{
    if(!s->notes)return;
    for(unsigned i=0;i<NOTE_COUNT;++i){note_slot_t *slot=&s->notes->slots[i];slot->session.stopped=1;tiku_paged_storage_close(slot->session.store);}
}
void surface_notes_dispose(surface_app_state_t *s)
{
    struct surface_notes *n=s->notes;if(!n)return;
    for(unsigned i=0;i<NOTE_COUNT;++i)clear(&n->slots[i]);
    tiku_owned_buffer_t memory=n->self;s->notes=NULL;tiku_buffer_dispose(&memory);
}
int surface_notes_modified(const surface_app_state_t *s)
{
    if(!s||!s->notes)return 0;
    for(unsigned i=0;i<NOTE_COUNT;++i)if(s->notes->slots[i].used&&surface_editor_modified(&s->notes->slots[i].session))return 1;
    return 0;
}
/** @brief Admit the native note filename and extract a bounded numeric suffix. */
static int filename(const char *path,unsigned *number)
{
    const char *name=strrchr(path,'/');if(!name)return 0;++name;
    size_t len=strlen(name);if(len<10||strncmp(name,"note-",5)||strcmp(name+len-4,".txt"))return 0;
    unsigned value=0;for(size_t i=5;i<len-4;++i){if(name[i]<'0'||name[i]>'9'||value>(UINT_MAX-(unsigned)(name[i]-'0'))/10){*number=0;return 1;}value=value*10+(unsigned)(name[i]-'0');}
    *number=value;return 1;
}
/** @brief Advance one catalog request; provider buffers stay behind the outer lease. */
static void catalog(surface_app_state_t *s)
{
    struct surface_notes *n=s->notes;
    if(n->submitted&&!tiku_paged_storage_pending(s->store)){
        n->submitted=0;tiku_app_lease_release(&s->lease);
        if(s->stopped){n->phase=0;return;}
        s->status=tiku_paged_storage_status(s->store);size_t count;uint64_t epoch;int eof;
        const tiku_storage_request_t *r=tiku_paged_storage_result(s->store,&count,&epoch,&eof);
        if(!r||s->status!=TIKU_RESOURCE_OK){n->phase=0;n->new_pending=0;s->shell->dirty=1;return;}
        n->epoch=epoch;
        if(n->phase==1){
            n->directory=1;
            for(size_t j=0;j<count;++j){unsigned number;
                if(r->entries[j].kind!=TIKU_RESOURCE_STREAM||!filename(r->entries[j].path,&number))continue;
                if(n->next_number&&number>=n->next_number)n->next_number=number==UINT_MAX?0:number+1;
                unsigned index;
                for(index=0;index<NOTE_COUNT;++index)if(n->slots[index].used&&!strcmp(n->slots[index].path,r->entries[j].path))break;
                if(index<NOTE_COUNT){if(n->slots[index].used==4){n->slots[index].node=r->entries[j].node_id;n->slots[index].used=2;}continue;}
                for(index=0;index<NOTE_COUNT&&n->slots[index].used;++index){}
                if(index==NOTE_COUNT){s->status=TIKU_RESOURCE_LIMIT;continue;}
                if(child(s,index,r->entries[j].path)){n->slots[index].node=r->entries[j].node_id;n->slots[index].used=2;}
            }
            if(!eof&&count)n->after=r->entries[count-1].node_id;
            else{n->phase=0;n->loading=1;}
        }else if(n->phase==4){
            int found=0;for(size_t j=0;j<count;++j)if(!strcmp(r->entries[j].path,"/Notes")&&r->entries[j].kind==TIKU_RESOURCE_DIRECTORY)found=1;
            if(found){n->directory=1;n->after=0;n->phase=1;}
            else if(!eof&&count)n->after=r->entries[count-1].node_id;
            else n->phase=n->new_pending?3:0;
        }else if(n->phase==2)n->phase=3;
        else{n->phase=0;n->new_pending=0;n->directory=1;new_note(s);}
        s->shell->dirty=1;
    }
    if(s->stopped||!n->phase||n->submitted)return;
    if(!tiku_app_lease_begin(s->owner,&s->lease)){s->status=TIKU_RESOURCE_LIMIT;n->phase=0;return;}
    if(n->phase==1)s->status=tiku_paged_storage_list(s->store,"/Notes",n->epoch,n->after);
    else if(n->phase==4)s->status=tiku_paged_storage_list(s->store,"/",n->epoch,n->after);
    else if(n->phase==2)s->status=tiku_paged_storage_list(s->store,"/",0,0);
    else s->status=tiku_paged_storage_mkdir(s->store,"/Notes",n->epoch);
    n->submitted=tiku_paged_storage_pending(s->store);
    if(!n->submitted){tiku_app_lease_release(&s->lease);n->phase=0;n->new_pending=0;}
}
void surface_notes_poll(surface_app_state_t *s)
{
    struct surface_notes *n=s?s->notes:NULL;if(!n)return;
    catalog(s);
    unsigned index=n->cursor++%NOTE_COUNT;note_slot_t *slot=&n->slots[index];
    int busy=surface_editor_busy(&slot->session);surface_editor_poll(&slot->session);
    if(s->stopped)return;
    if(busy&&!surface_editor_busy(&slot->session)){s->status=slot->session.status;s->shell->dirty=1;
        if(slot->used==3){surface_note_info_t info;surface_editor_note_info(&slot->session,&info);slot->used=info.exists?1:4;}}
    if(n->phase||n->submitted)return;
    if(n->loading){
        for(unsigned i=0;i<NOTE_COUNT;++i)if(n->slots[i].used==3)return;
        for(unsigned i=0;i<NOTE_COUNT;++i)if(n->slots[i].used==2){
            note_slot_t *load=&n->slots[i];
            if(surface_editor_open(&load->session,load->path,n->epoch,load->node)){load->used=3;s->status=TIKU_RESOURCE_PENDING;}
            else load->used=4;
            return;
        }
        n->loading=0;if(n->new_pending){n->new_pending=0;new_note(s);}s->shell->dirty=1;
    }
    if(slot->used!=1)return;
    surface_note_info_t info;surface_editor_note_info(&slot->session,&info);
    if(info.deleted){clear(slot);--n->count;if(n->selected==(int)index)n->selected=-1;s->shell->dirty=1;return;}
    if(info.revision!=slot->revision||info.metadata!=slot->metadata){slot->revision=info.revision;slot->metadata=info.metadata;slot->due=s->shell->now+500000;}
    if(info.dirty&&!info.busy&&!s->shell->clock_failed&&(slot->force||s->shell->now>=slot->due)){
        for(unsigned i=0;i<NOTE_COUNT;++i)if(surface_editor_busy(&n->slots[i].session))return;
        slot->force=0;slot->due=s->shell->now+5000000;
        if(surface_editor_note_save(&slot->session))s->status=TIKU_RESOURCE_PENDING;
    }
}
int surface_notes_back(surface_app_state_t *s)
{
    struct surface_notes *n=s->notes;if(!n)return 0;
    if(n->confirm)n->confirm=0;
    else if(n->tools)n->tools=0;
    else if(n->selected>=0){if(surface_editor_back(surface_notes_editor(s)))return 1;n->selected=-1;}
    else return 0;
    s->shell->dirty=1;return 1;
}
void surface_notes_key(surface_app_state_t *s,const tiku_input_event_t *event)
{
    struct surface_notes *n=s->notes;if(!n||!event)return;
    if(event->type==TIKU_EVENT_KEY_DOWN&&(event->modifiers&TIKU_MOD_CMD)&&event->key=='n'){surface_notes_action(s,ACTION_NOTES_NEW);return;}
    if(n->selected>=0&&!n->tools&&!n->confirm&&n->slots[n->selected].used==1)surface_editor_key(surface_notes_editor(s),event);
}
void surface_notes_action(surface_app_state_t *s,int action)
{
    struct surface_notes *n=s->notes;if(!n)return;
    surface_app_state_t *e=surface_notes_editor(s);surface_note_info_t info;surface_editor_note_info(e,&info);
    if(action==ACTION_NOTES_KEEP)n->confirm=0;
    else if(action==ACTION_NOTES_DELETE_CONFIRMED&&n->confirm){if(surface_editor_note_delete(e)){n->confirm=0;n->tools=0;}}
    else if(n->confirm)return;
    else if(action==ACTION_NOTES_DELETE&&e)n->confirm=1;
    else if(action==ACTION_NOTES_REFRESH){
        if(n->phase||n->loading)return;
        for(unsigned i=0;i<NOTE_COUNT;++i)if(surface_editor_busy(&n->slots[i].session))return;
        n->allocation_error=0;n->phase=4;n->epoch=n->after=0;s->status=TIKU_RESOURCE_PENDING;
    }else if(action==ACTION_NOTES_DUPLICATE&&e){
        if(info.busy||n->phase||n->loading||n->count==NOTE_COUNT)return;
        int previous=n->selected;new_note(s);
        if(n->selected!=previous&&!surface_editor_note_copy(e,surface_notes_editor(s))){clear(&n->slots[n->selected]);--n->count;n->selected=previous;s->status=TIKU_RESOURCE_LIMIT;}
    }else if(action==ACTION_NOTES_NEW){
        if(n->phase||n->loading||n->count==NOTE_COUNT)return;
        if(n->directory)new_note(s);else{n->phase=4;n->epoch=n->after=0;n->new_pending=1;s->status=TIKU_RESOURCE_PENDING;}
    }else if(action==ACTION_NOTES_SAVE_ALL){for(unsigned i=0;i<NOTE_COUNT;++i)n->slots[i].force=(unsigned)surface_editor_modified(&n->slots[i].session);}
    else if(action==ACTION_NOTES_LIST||action==ACTION_EDITOR_SAVE_AS){n->selected=-1;n->tools=0;}
    else if(action==ACTION_NOTES_PREVIOUS){if(n->page)--n->page;}
    else if(action==ACTION_NOTES_NEXT){if(n->page<3)++n->page;}
    else if(action>=ACTION_NOTES_ROW&&action<ACTION_NOTES_ROW+6){unsigned index=n->page*6+(unsigned)(action-ACTION_NOTES_ROW);if(n->slots[index].used==1){n->selected=(int)index;n->tools=0;}}
    else if(action==ACTION_EDITOR_RUN||action==ACTION_NOTES_TOOLS){if(e)n->tools=1;}
    else if(action==ACTION_NOTES_DOCUMENT)n->tools=0;
    else if(action==ACTION_NOTES_IMPORTANT&&e)surface_editor_note_metadata(e,info.paper,!info.important);
    else if(action>=ACTION_NOTES_PAPER&&action<ACTION_NOTES_PAPER+4&&e)surface_editor_note_metadata(e,(unsigned)(action-ACTION_NOTES_PAPER),info.important);
    else if(e&&n->slots[n->selected].used==1)surface_editor_action(e,action);
    s->shell->dirty=1;
}
void surface_notes_view(surface_app_state_t *s,surface_view_t *v)
{
    struct surface_notes *n=s->notes;if(!n)return;
    v->notes_selected=n->selected;v->notes_page=n->page;v->notes_count=n->count;v->notes_tools=n->tools;v->notes_confirm=n->confirm;v->notes_loading=n->phase||n->loading;
    if(n->selected>=0){surface_editor_view(surface_notes_editor(s),v);surface_note_info_t info;surface_editor_note_info(surface_notes_editor(s),&info);v->notes_paper=info.paper;v->notes_important=info.important;}
    for(unsigned i=0;i<6;++i){note_slot_t *slot=&n->slots[n->page*6+i];if(!slot->used)continue;
        surface_note_info_t info;surface_editor_note_info(&slot->session,&info);strcpy(v->notes_titles[i],info.title);
        v->notes_state[i]=1u|(info.dirty?2u:0u)|(info.important?4u:0u)|(slot->used==4?8u:0u)|(info.busy?16u:0u)|((info.dirty&&slot->session.status!=TIKU_RESOURCE_OK&&slot->session.status!=TIKU_RESOURCE_PENDING)?32u:0u);v->notes_colors[i]=info.paper;
    }
    int busy=v->notes_loading;
    for(unsigned i=0;i<NOTE_COUNT;++i)busy|=surface_editor_busy(&n->slots[i].session);
    v->status=busy?TIKU_RESOURCE_PENDING:n->allocation_error?TIKU_RESOURCE_LIMIT:s->status;
    if(!busy)for(unsigned i=0;i<NOTE_COUNT;++i){note_slot_t *slot=&n->slots[i];
        if(slot->used&&surface_editor_modified(&slot->session)&&slot->session.status!=TIKU_RESOURCE_OK&&slot->session.status!=TIKU_RESOURCE_PENDING)v->status=slot->session.status;
    }
}
/** @brief Six overview cards provide separate 236 by 112 pixel touch targets. */
static tiku_rect_t card(unsigned i)
{return (tiku_rect_t){32+(int)(i%3)*248,122+(int)(i/3)*124,236,112};}
/** @brief Hit-test one fixed rectangle. */
static int inside(tiku_rect_t r,int x,int y)
{return x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h;}
int surface_notes_hit(const surface_view_t *v,int x,int y)
{
    if(v->notes_confirm){if(inside((tiku_rect_t){180,250,200,44},x,y))return ACTION_NOTES_KEEP;if(inside((tiku_rect_t){420,250,200,44},x,y))return ACTION_NOTES_DELETE_CONFIRMED;return 0;}
    if(v->notes_selected>=0){
        if(!v->notes_tools)return surface_editor_hit(v,x,y);
        for(int i=0;i<4;++i)if(inside((tiku_rect_t){32+i*186,140,174,56},x,y))return ACTION_NOTES_PAPER+i;
        if(inside((tiku_rect_t){32,220,236,44},x,y))return ACTION_NOTES_IMPORTANT;
        if(inside((tiku_rect_t){280,220,236,44},x,y))return ACTION_NOTES_DELETE;
        if(inside((tiku_rect_t){528,220,236,44},x,y))return ACTION_NOTES_DOCUMENT;
        if(inside((tiku_rect_t){32,284,236,44},x,y))return ACTION_NOTES_DUPLICATE;
        return 0;
    }
    for(unsigned i=0;i<6;++i)if(v->notes_state[i]&&inside(card(i),x,y))return ACTION_NOTES_ROW+(int)i;
    static const int ids[]={ACTION_NOTES_NEW,ACTION_NOTES_SAVE_ALL,ACTION_NOTES_PREVIOUS,ACTION_NOTES_NEXT,ACTION_NOTES_REFRESH};
    for(int i=0;i<5;++i)if(inside((tiku_rect_t){32+i*148,380,140,44},x,y))return ids[i];
    return 0;
}
void surface_notes_paint(tiku_surface_t *s,const surface_view_t *v)
{
    static const uint32_t colors[]={0xffee8c,0xc8f5aa,0xb4dcff,0xffc8dc};
    const tiku_font_t *font=tiku_font_plain();
    if(v->notes_selected>=0){
        if(!v->notes_tools)surface_editor_paint(s,v);
        else{
            tiku_text(s,font,32,108,"Paper color",TIKU_C_TEXT);static const char *papers[]={"Yellow","Green","Blue","Pink"};
            for(int i=0;i<4;++i)tiku_ui_button(s,(tiku_rect_t){32+i*186,140,174,56},papers[i],v->notes_paper==(unsigned)i?TIKU_S_PRESSED:0);
            tiku_ui_button(s,(tiku_rect_t){32,220,236,44},v->notes_important?"Important: on":"Important: off",0);
            tiku_ui_button(s,(tiku_rect_t){280,220,236,44},"Delete note",0);tiku_ui_button(s,(tiku_rect_t){528,220,236,44},"Document",0);
            tiku_ui_button(s,(tiku_rect_t){32,284,236,44},"Duplicate note",0);
        }
    }else{
        char heading[96];snprintf(heading,sizeof heading,"%u notes | Page %u of 4 | 8 KiB per note",v->notes_count,v->notes_page+1);tiku_text(s,font,32,105,heading,TIKU_C_TEXT);
        for(unsigned i=0;i<6;++i){if(!v->notes_state[i])continue;tiku_rect_t r=card(i);tiku_ui_sunken(s,r,colors[v->notes_colors[i]%4]);
            char title[65];(void)tiku_text_elide(0,v->notes_titles[i],r.w-20,title,sizeof title);tiku_text(s,tiku_font_bold(),r.x+10,r.y+32,title,TIKU_C_TEXT);
            tiku_text(s,font,r.x+10,r.y+64,(v->notes_state[i]&8)?"Load failed":(v->notes_state[i]&16)?"Saving...":(v->notes_state[i]&32)?"Save failed":(v->notes_state[i]&2)?"Unsaved":"Saved",TIKU_C_TEXT);
            if(v->notes_state[i]&4)tiku_text(s,font,r.x+10,r.y+92,"Important",TIKU_C_TEXT);
        }
        static const char *labels[]={"New note","Save all","Previous","Next","Refresh"};
        for(int i=0;i<5;++i)tiku_ui_button(s,(tiku_rect_t){32+i*148,380,140,44},labels[i],v->notes_loading?TIKU_S_DISABLED:0);
    }
    if(v->notes_confirm){(void)tiku_ui_window(s,(tiku_rect_t){140,140,520,180},"Delete this note?",1);tiku_text(s,font,168,208,"The saved note and its unsaved edits will be removed.",TIKU_C_TEXT);tiku_ui_button(s,(tiku_rect_t){180,250,200,44},"Keep note",0);tiku_ui_button(s,(tiku_rect_t){420,250,200,44},"Delete note",0);}
}
