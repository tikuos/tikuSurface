/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * hex.c - retained binary edits and version-checked asynchronous file jobs.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_byte_model.h"
#include "tiku_word_model.h"
#include "tiku_ui.h"
#include "tiku_font.h"
#include "tiku_event.h"
#include <string.h>
#define BYTE_LIMIT 8192
/** @brief Separate incoming bytes preserve the document across refused loads. */
struct surface_hex {
    tiku_owned_buffer_t self;
    tiku_byte_model_t model;
    tiku_byte_search_t search;
    unsigned char bytes[BYTE_LIMIT],original[BYTE_LIMIT],incoming[BYTE_LIMIT];
    char path[65],target[65],parent[65],draft[129];
    size_t received,first;
    uint64_t epoch,node,after,bound_epoch,length,version;
    int loaded,job,submitted,cancelled,dialog,search_result;
    int input_page,ascii,editing,high;
};
/** @brief Validate a canonical bounded media path. */
static int path_ok(const char *path)
{
    if(!path)return 0;
    size_t n=strlen(path);if(n<2||n>64||path[0]!='/'||path[n-1]=='/')return 0;
    const char *part=path+1;
    for(size_t i=1;i<=n;++i){
        if(path[i]&&((unsigned char)path[i]<32||(unsigned char)path[i]==127||path[i]=='\\'))return 0;
        if(!path[i]||path[i]=='/'){size_t k=(size_t)(path+i-part);
            if(!k||(k==1&&part[0]=='.')||(k==2&&part[0]=='.'&&part[1]=='.'))return 0;
            part=path+i+1;}
    }
    return 1;
}
/** @brief Whether this session can publish through the storage provider. */
static int writable(const surface_app_state_t *s)
{return s->shell->config.caller.authenticated&&(s->shell->config.storage.capabilities&TIKU_RESOURCE_FEATURE_CHANGE)&&
    (s->shell->config.caller.scopes&TIKU_RESOURCE_WRITE);}
int surface_hex_start(surface_app_state_t *s)
{
    tiku_owned_buffer_t memory;
    if(!tiku_buffer_init(&memory,s->shell->config.allocator,_Alignof(struct surface_hex))||
       !tiku_buffer_resize(&memory,1,sizeof(struct surface_hex),1))return 0;
    s->byte_editor=memory.data;s->byte_editor->self=memory;s->byte_editor->high=-1;
    tiku_byte_init(&s->byte_editor->model,s->byte_editor->bytes,s->byte_editor->original,0);
    s->store=tiku_paged_storage_create(s->shell->config.allocator,s->shell->config.storage,&s->shell->config.caller,1,1024);
    return s->store!=NULL;
}
void surface_hex_dispose(surface_app_state_t *s)
{
    if(!s->byte_editor)return;
    tiku_owned_buffer_t memory=s->byte_editor->self;s->byte_editor=NULL;tiku_buffer_dispose(&memory);
}
int surface_hex_modified(const surface_app_state_t *s)
{return s&&s->byte_editor&&s->byte_editor->model.changed;}
int surface_hex_busy(const surface_app_state_t *s)
{return s&&s->byte_editor&&(s->byte_editor->job||s->byte_editor->submitted);}
/** @brief Select the parent for a paged identity lookup. */
static void parent(struct surface_hex *h,const char *path)
{
    strcpy(h->parent,path);char *slash=strrchr(h->parent,'/');
    if(slash==h->parent)slash[1]=0;else *slash=0;h->after=0;
}
/** @brief Finish a job without changing document bytes or history. */
static void finish(surface_app_state_t *s,tiku_resource_error_t status)
{s->byte_editor->job=0;s->status=status;s->shell->dirty=1;}
int surface_hex_open(surface_app_state_t *s,const char *path,uint64_t epoch,uint64_t node)
{
    if(!s||!s->byte_editor||s->stopped)return 0;
    struct surface_hex *h=s->byte_editor;
    if(surface_hex_busy(s)||h->model.changed){s->status=TIKU_RESOURCE_BUSY;s->shell->dirty=1;return 0;}
    if(!path_ok(path)){s->status=TIKU_RESOURCE_INVALID;s->shell->dirty=1;return 0;}
    strcpy(h->target,path);parent(h,path);h->epoch=epoch;h->node=node;h->received=0;
    h->length=h->version=0;h->cancelled=0;h->high=-1;h->job=epoch&&node?3:1;
    tiku_byte_search_cancel(&h->search);h->search_result=0;s->status=TIKU_RESOURCE_PENDING;s->shell->dirty=1;return 1;
}
/** @brief Acquire a lease and submit one operation; retirement releases the lease. */
static void submit(surface_app_state_t *s)
{
    struct surface_hex *h=s->byte_editor;
    if(!tiku_app_lease_begin(s->owner,&s->lease)){finish(s,TIKU_RESOURCE_LIMIT);return;}
    if(h->job==1||h->job==4)s->status=tiku_paged_storage_list(s->store,"/",0,0);
    else if(h->job==2||h->job==5)s->status=tiku_paged_storage_list(s->store,h->parent,h->epoch,h->after);
    else if(h->job==3)s->status=tiku_paged_storage_read(s->store,h->target,h->epoch,h->node,h->received);
    else s->status=tiku_paged_storage_replace(s->store,h->target,h->epoch,h->node,h->bytes,h->model.size);
    h->submitted=tiku_paged_storage_pending(s->store);
    if(!h->submitted){tiku_app_lease_release(&s->lease);finish(s,s->status);}
}
void surface_hex_poll(surface_app_state_t *s)
{
    struct surface_hex *h=s->byte_editor;if(!h)return;
    if(h->submitted){
        if(tiku_paged_storage_pending(s->store))return;
        h->submitted=0;tiku_app_lease_release(&s->lease);if(s->stopped){h->job=0;return;}
        if(h->cancelled){finish(s,TIKU_RESOURCE_CANCELLED);return;}
        size_t count;uint64_t epoch;int eof;
        const tiku_storage_request_t *r=tiku_paged_storage_result(s->store,&count,&epoch,&eof);
        if(!r||tiku_paged_storage_status(s->store)!=TIKU_RESOURCE_OK){finish(s,tiku_paged_storage_status(s->store));return;}
        h->epoch=epoch;
        if(h->job==1)h->job=2;
        else if(h->job==4){h->node=0;h->job=6;}
        else if(h->job==2||h->job==5){
            const char *wanted=h->job==2?h->target:h->path;int found=0;
            for(size_t i=0;i<count;++i)if(r->entries[i].kind==TIKU_RESOURCE_STREAM&&!strcmp(r->entries[i].path,wanted)){h->node=r->entries[i].node_id;found=1;break;}
            if(found)h->job=h->job==2?3:6;
            else if(!eof&&count)h->after=r->entries[count-1].node_id;
            else{finish(s,TIKU_RESOURCE_NOT_FOUND);return;}
        }else if(h->job==3){
            uint64_t length,version;
            if(!tiku_paged_storage_info(s->store,&length,&version)){finish(s,TIKU_RESOURCE_UNSUPPORTED);return;}
            if(length>BYTE_LIMIT||count>BYTE_LIMIT-h->received||(!count&&!eof)){finish(s,TIKU_RESOURCE_LIMIT);return;}
            if(h->received&&(length!=h->length||version!=h->version)){finish(s,TIKU_RESOURCE_STALE);return;}
            h->length=length;h->version=version;
            memcpy(h->incoming+h->received,r->bytes,count);h->received+=count;
            if(h->received>length||(eof&&h->received!=length)||(!eof&&h->received==length)){finish(s,TIKU_RESOURCE_STALE);return;}
            if(eof){memcpy(h->bytes,h->incoming,h->received);tiku_byte_init(&h->model,h->bytes,h->original,h->received);
                strcpy(h->path,h->target);h->bound_epoch=epoch;h->loaded=1;h->first=0;finish(s,TIKU_RESOURCE_OK);return;}
        }else{
            tiku_byte_saved(&h->model);strcpy(h->path,h->target);h->bound_epoch=epoch;finish(s,TIKU_RESOURCE_OK);return;
        }
        s->status=TIKU_RESOURCE_PENDING;s->shell->dirty=1;return;
    }
    if(s->stopped)return;
    if(h->job){submit(s);return;}
    if(h->search.active){
        tiku_byte_search_status_t result=tiku_byte_search_step(&h->search,&h->model);
        if(result!=TIKU_BYTE_SEARCH_PENDING){h->search_result=result==TIKU_BYTE_SEARCH_FOUND?2:3;h->first=(h->model.cursor/128)*128;s->shell->dirty=1;}
    }
}
/** @brief Queue publication with a copied destination and an immutable document. */
static int save(surface_app_state_t *s,const char *path,int create)
{
    struct surface_hex *h=s->byte_editor;
    if(!h->loaded||surface_hex_busy(s)||!writable(s)||!path_ok(path))return 0;
    if(create&&!strcmp(h->path,path))return 0;
    strcpy(h->target,path);h->cancelled=0;h->epoch=h->bound_epoch;
    parent(h,h->path);h->job=create?4:5;tiku_byte_search_cancel(&h->search);h->search_result=0;
    s->status=TIKU_RESOURCE_PENDING;s->shell->dirty=1;return 1;
}
const char *surface_hex_draft(surface_app_state_t *s)
{return s->byte_editor->draft;}
int surface_hex_accept(surface_app_state_t *s,const char *text)
{
    struct surface_hex *h=s->byte_editor;if(surface_hex_busy(s))return 0;
    if(h->dialog==1)return surface_hex_open(s,text,0,0);
    if(h->dialog==2)return save(s,text,1);
    if(!h->loaded)return 0;
    if(h->dialog==3){unsigned char bytes[64];size_t n;
        if(!tiku_byte_pattern(text,bytes,&n)||n!=1||h->model.cursor>=h->model.size)return 0;
        (void)tiku_byte_replace(&h->model,h->model.cursor,bytes[0]);
    }else if(h->dialog==4){size_t at;if(!tiku_byte_offset(text,h->model.size,&at))return 0;h->model.cursor=at;h->first=(at/128)*128;
    }else if(h->dialog==5){if(!tiku_byte_search_begin(&h->search,&h->model,text))return 0;h->search_result=1;}
    s->status=TIKU_RESOURCE_OK;s->shell->dirty=1;return 1;
}
/** @brief Copy an unsigned byte to two uppercase hexadecimal characters. */
static void byte_text(unsigned char byte,char *out)
{static const char digits[]="0123456789ABCDEF";out[0]=digits[byte>>4];out[1]=digits[byte&15];out[2]=0;}
void surface_hex_action(surface_app_state_t *s,int action)
{
    struct surface_hex *h=s->byte_editor;if(!h)return;
    h->high=-1;
    if(action==ACTION_BYTE_CANCEL){
        tiku_byte_search_cancel(&h->search);h->search_result=0;h->cancelled=1;
        if(h->submitted)tiku_paged_storage_cancel(s->store);else finish(s,TIKU_RESOURCE_CANCELLED);
        s->shell->dirty=1;return;
    }
    if(surface_hex_busy(s))return;
    if(action==ACTION_BYTE_INPUT){h->input_page=1;s->shell->dirty=1;return;}
    if(action>=ACTION_BYTE_NAVIGATION&&action<=ACTION_BYTE_DOCUMENT){
        if(action!=ACTION_BYTE_DOCUMENT){h->editing=action!=ACTION_BYTE_NAVIGATION;h->ascii=action==ACTION_BYTE_ASCII_MODE;}
        h->input_page=0;s->shell->dirty=1;return;
    }
    h->input_page=0;
    if(action==ACTION_BYTE_SAVE){if(!save(s,h->path,0))s->status=TIKU_RESOURCE_DENIED;s->shell->dirty=1;return;}
    if(action==ACTION_BYTE_OPEN||action==ACTION_BYTE_SAVE_AS||action==ACTION_BYTE_EDIT||action==ACTION_BYTE_GOTO||action==ACTION_BYTE_FIND){
        if(action==ACTION_BYTE_OPEN&&h->model.changed){s->status=TIKU_RESOURCE_BUSY;s->shell->dirty=1;return;}
        if(action!=ACTION_BYTE_OPEN&&!h->loaded)return;
        if(action==ACTION_BYTE_SAVE_AS&&!writable(s))return;
        static const unsigned dialogs[]={1,0,2,3,0,0,4,5};
        h->dialog=(int)dialogs[action-ACTION_BYTE_OPEN];
        h->draft[0]=0;
        if(h->dialog<=2)strcpy(h->draft,h->path[0]?h->path:"/file.bin");
        else if(h->dialog==3&&h->model.size)byte_text(h->bytes[h->model.cursor],h->draft);
        else if(h->dialog==4){char number[65];tiku_word_format(h->model.cursor,10,number);strcpy(h->draft,number);}
        tiku_byte_search_cancel(&h->search);h->search_result=0;surface_text_open(s->shell);return;
    }
    if(!h->loaded)return;
    tiku_byte_search_cancel(&h->search);h->search_result=0;
    if(action==ACTION_BYTE_UNDO||action==ACTION_BYTE_REDO){(void)tiku_byte_undo(&h->model,action==ACTION_BYTE_REDO);h->first=(h->model.cursor/128)*128;}
    else if(action==ACTION_BYTE_PREVIOUS){if(h->first>=128)h->first-=128;}
    else if(action==ACTION_BYTE_NEXT){if(h->first+128<h->model.size)h->first+=128;}
    else if(action==ACTION_BYTE_COPY){if(h->model.size)byte_text(h->bytes[h->model.cursor],s->shell->clipboard);}
    else if(action>=ACTION_BYTE_CELL&&action<ACTION_BYTE_CELL+128){size_t at=h->first+(unsigned)(action-ACTION_BYTE_CELL);if(at<h->model.size)h->model.cursor=at;}
    s->status=TIKU_RESOURCE_OK;s->shell->dirty=1;
}
int surface_hex_back(surface_app_state_t *s)
{
    if(!s||!s->byte_editor||!s->byte_editor->input_page)return 0;
    s->byte_editor->input_page=0;s->byte_editor->high=-1;s->shell->dirty=1;return 1;
}
void surface_hex_key(surface_app_state_t *s,const tiku_input_event_t *event)
{
    struct surface_hex *h=s->byte_editor;if(!h)return;
    if(!event||event->type==TIKU_EVENT_DEACTIVATED){h->high=-1;s->shell->dirty=1;return;}
    if(event->type!=TIKU_EVENT_KEY_DOWN||surface_hex_busy(s))return;
    if(event->modifiers&TIKU_MOD_CMD){
        int action=0;
        if(event->key=='o')action=ACTION_BYTE_OPEN;
        else if(event->key=='s')action=(event->modifiers&TIKU_MOD_SHIFT)?ACTION_BYTE_SAVE_AS:ACTION_BYTE_SAVE;
        else if(event->key=='f')action=ACTION_BYTE_FIND;
        else if(event->key=='g')action=ACTION_BYTE_GOTO;
        else if(event->key=='z')action=(event->modifiers&TIKU_MOD_SHIFT)?ACTION_BYTE_REDO:ACTION_BYTE_UNDO;
        else if(event->key=='y')action=ACTION_BYTE_REDO;
        else if(event->key=='c')action=ACTION_BYTE_COPY;
        if(action)surface_hex_action(s,action);
        return;
    }
    if(h->input_page||!h->loaded||(event->modifiers&~(TIKU_MOD_SHIFT|TIKU_MOD_CAPS)))return;
    int result=tiku_byte_key_input(&h->model,event->key,event->text,&h->high,h->ascii,!h->editing,128);
    if(result==TIKU_BYTE_INPUT_MOVED||result==TIKU_BYTE_INPUT_EDITED||result==TIKU_BYTE_INPUT_PENDING){
        tiku_byte_search_cancel(&h->search);h->search_result=0;h->first=(h->model.cursor/128)*128;
        s->status=TIKU_RESOURCE_OK;s->shell->dirty=1;
    }
}
void surface_hex_view(const surface_app_state_t *s,surface_view_t *v)
{
    const struct surface_hex *h=s->byte_editor;if(!h)return;
    strcpy(v->byte_path,h->path);v->byte_size=h->model.size;v->byte_cursor=h->model.cursor;v->byte_first=h->first;
    v->byte_count=h->model.size-h->first;if(v->byte_count>128)v->byte_count=128;
    memcpy(v->byte_cells,h->bytes+h->first,v->byte_count);v->byte_dirty=h->model.changed!=0;
    v->byte_input_page=h->input_page;v->byte_ascii=h->ascii;v->byte_editing=h->editing;v->byte_high=h->high;
    v->byte_busy=h->job;v->byte_dialog=h->dialog;v->byte_loaded=h->loaded;v->byte_writable=writable(s);v->byte_search=h->search_result;v->status=s->status;
}
/** @brief Fixed command rectangles below the 8-by-16 byte grid. */
static tiku_rect_t rect(int action)
{int n=action-ACTION_BYTE_OPEN;return (tiku_rect_t){32+(n%6)*124,316+(n/6)*52,116,44};}
/** @brief Match command admission to the copied model state. */
static int enabled(const surface_view_t *v,int action)
{
    if(action==ACTION_BYTE_CANCEL)return v->byte_busy||v->byte_search==1||v->byte_loaded;
    if(v->byte_busy)return 0;
    if(action==ACTION_BYTE_OPEN)return !v->byte_dirty;
    if(!v->byte_loaded)return 0;
    if(action==ACTION_BYTE_SAVE||action==ACTION_BYTE_SAVE_AS)return v->byte_writable;
    if(action==ACTION_BYTE_PREVIOUS)return v->byte_first>0;
    if(action==ACTION_BYTE_NEXT)return v->byte_first+128<v->byte_size;
    return v->byte_size!=0;
}
/** @brief Keyboard choices use explicit mode buttons outside the byte grid. */
static tiku_rect_t input_rect(int action)
{int n=action-ACTION_BYTE_NAVIGATION;return (tiku_rect_t){32+(n%2)*374,192+(n/2)*64,362,48};}
int surface_hex_hit(const surface_view_t *v,int x,int y)
{
    if(v->byte_input_page){
        if(v->byte_busy)return 0;
        for(int id=ACTION_BYTE_NAVIGATION;id<=ACTION_BYTE_DOCUMENT;++id){tiku_rect_t r=input_rect(id);if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return id;}
        return 0;
    }
    if(!v->byte_busy&&x>=112&&x<624&&y>=120&&y<296){int n=((y-120)/22)*16+(x-112)/32;
        if((size_t)n<v->byte_count)return ACTION_BYTE_CELL+n;}
    for(int id=ACTION_BYTE_OPEN;id<=ACTION_BYTE_CANCEL;++id){tiku_rect_t r=rect(id);
        if(enabled(v,id)&&x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return id==ACTION_BYTE_CANCEL&&!v->byte_busy&&v->byte_search!=1?ACTION_BYTE_INPUT:id;}
    return 0;
}
void surface_hex_paint(tiku_surface_t *s,const surface_view_t *v)
{
    const tiku_font_t *font=tiku_font_plain();
    if(v->byte_input_page){
        tiku_text(s,font,32,116,"Keyboard mode",TIKU_C_TEXT);
        tiku_text(s,font,32,148,"Navigation leaves bytes unchanged. Editing replaces a byte and advances.",TIKU_C_TEXT);
        static const char *names[]={"Navigation only","Hexadecimal editing","ASCII editing","Document"};
        for(int id=ACTION_BYTE_NAVIGATION;id<=ACTION_BYTE_DOCUMENT;++id){int on=id==(v->byte_editing?(v->byte_ascii?ACTION_BYTE_ASCII_MODE:ACTION_BYTE_HEX_MODE):ACTION_BYTE_NAVIGATION);tiku_ui_button(s,input_rect(id),names[id-ACTION_BYTE_NAVIGATION],on?TIKU_S_PRESSED:0);}
        return;
    }
    tiku_text(s,font,32,105,v->byte_loaded?v->byte_path:"Open a binary file, or choose Edit bytes in Tracker.",TIKU_C_TEXT);
    tiku_ui_sunken(s,(tiku_rect_t){32,114,736,186},0xffffff);
    for(unsigned row=0;row<8;++row){
        int y=120+(int)row*22;if(y>=s->clip.y+s->clip.h||y+22<s->clip.y)continue;
        if(row*16>=v->byte_count)break;
        char offset[65];tiku_word_format(v->byte_first+row*16,16,offset);tiku_text(s,font,40,y+16,offset,TIKU_C_TEXT);
        for(unsigned col=0;col<16;++col){unsigned n=row*16+col;if(n>=v->byte_count)break;
            int x=112+(int)col*32;char text[3];byte_text(v->byte_cells[n],text);
            if(v->byte_first+n==v->byte_cursor)tiku_fill(s,(tiku_rect_t){x,y,30,22},0xb8d8ef);
            if(v->byte_first+n==v->byte_cursor&&v->byte_high>=0){static const char digits[]="0123456789ABCDEF";text[0]=digits[v->byte_high];tiku_fill(s,(tiku_rect_t){x+2,y+19,8,2},0);}
            tiku_text(s,font,x+2,y+16,text,TIKU_C_TEXT);
            char ascii[2]={v->byte_cells[n]>=32&&v->byte_cells[n]<127?(char)v->byte_cells[n]:'.',0};
            tiku_text(s,font,632+(int)col*8,y+16,ascii,TIKU_C_TEXT);
        }
    }
    static const char *labels[]={"Open","Save","Save as","Edit byte","Undo","Redo","Go to","Find bytes","Previous","Next","Copy byte","Cancel"};
    for(int id=ACTION_BYTE_OPEN;id<=ACTION_BYTE_CANCEL;++id)tiku_ui_button(s,rect(id),id==ACTION_BYTE_CANCEL&&!v->byte_busy&&v->byte_search!=1?"Keyboard":labels[id-ACTION_BYTE_OPEN],enabled(v,id)?0:TIKU_S_DISABLED);
}
