/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * editor.c - retained documents, bounded file jobs and explicit save ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_edit_search.h"
#if SURFACE_EDITOR_LINES
#include "tiku_edit_lines.h"
#endif
#include "tiku_event.h"
#include "tiku_ui.h"
#include "tiku_font.h"
#include "tiku_text_layout.h"
#include <string.h>
#if SURFACE_NOTES
#include "tiku_note_format.h"
#endif
#define EDIT_BYTES 8192
#define EDIT_RECORD (EDIT_BYTES+(SURFACE_NOTES?257:0))
#define EDIT_LAST (SURFACE_BASIC ? ACTION_EDITOR_RUN : ACTION_EDITOR_NEWLINE)
/** @brief One Editor document and its provider-owned working storage. */
struct surface_editor {
    tiku_owned_buffer_t self;
    int context;
    tiku_editdoc_t *doc;
    tiku_edit_control_t control;
    unsigned char input[EDIT_RECORD+1],output[EDIT_RECORD+1];
    char path[65],destination[65],parent[65];
    size_t bytes,save_bytes;
    uint64_t epoch,node,after,saved_state,bound_epoch;
    int job,submitted,dialog,selecting;
#if SURFACE_NOTES
    int note,exists,deleted,deleting;
    tiku_note_metadata_t metadata;
    uint64_t metadata_state,metadata_saved,metadata_snapshot,verify_version;
    size_t baseline_bytes,verify_bytes;
#endif
    char query[256],replacement[256];
    unsigned search_flags;
    int tools,wrapped;
    size_t replaced;
    tiku_edit_status_t error;
};
/** @brief Note saves freeze their output while the retained document accepts edits. */
static int input_locked(const struct surface_editor *e)
{
#if SURFACE_NOTES
    if(e->note&&e->deleted)return 1;
    if(e->note&&!e->deleting&&e->job!=1)return 0;
#endif
    return e->job!=0;
}
/** @brief Validate the filesystem's canonical absolute path bound. */
static int path_ok(const char *path)
{
    size_t n=strlen(path);if(n<2||n>64||path[0]!='/'||path[n-1]=='/')return 0;
    const char *part=path+1;
    for(size_t i=1;i<=n;++i){
        if(i<n&&((unsigned char)path[i]<32||(unsigned char)path[i]==127||path[i]=='\\'))return 0;
        if(!path[i]||path[i]=='/'){size_t len=(size_t)(path+i-part);
            if(!len||(len==1&&*part=='.')||(len==2&&part[0]=='.'&&part[1]=='.'))return 0;
            part=path+i+1;}
    }
    return 1;
}
int surface_editor_start(surface_app_state_t *s)
{
    tiku_owned_buffer_t memory;
    if(!tiku_buffer_init(&memory,s->shell->config.allocator,_Alignof(struct surface_editor))||
       !tiku_buffer_resize(&memory,1,sizeof(struct surface_editor),1))return 0;
    s->editor=memory.data;struct surface_editor *e=s->editor;e->self=memory;
    if(!surface_editor_context_acquire(s->shell))return 0;
    e->context=1;
    if(tiku_editdoc_create(&(tiku_editdoc_limits_t){EDIT_BYTES,16384},&e->doc)||
       tiku_edit_control_init(&e->control,e->doc))return 0;
    s->store=tiku_paged_storage_create(s->shell->config.allocator,s->shell->config.storage,&s->shell->config.caller,16,1024);
    return s->store!=NULL;
}
void surface_editor_dispose(surface_app_state_t *s)
{
#if SURFACE_EDITOR_DOCUMENTS
    if(s&&s->documents){surface_editor_documents_dispose(s);return;}
#endif
    struct surface_editor *e=s->editor;if(!e)return;
    tiku_edit_control_dispose(&e->control);tiku_editdoc_release(e->doc);
    if(e->context)surface_editor_context_release(s->shell);
    tiku_owned_buffer_t memory=e->self;s->editor=NULL;tiku_buffer_dispose(&memory);
}
int surface_editor_modified(const surface_app_state_t *s)
{
#if SURFACE_EDITOR_DOCUMENTS
    if(s&&s->documents)return surface_editor_documents_modified(s);
#endif
    if(!s||!s->editor)return 0;
#if SURFACE_NOTES
    if(s->editor->note&&(s->editor->deleted||s->editor->metadata_state!=s->editor->metadata_saved))return !s->editor->deleted;
#endif
    return tiku_editdoc_modified(s->editor->doc);
}
int surface_editor_busy(const surface_app_state_t *s)
{
#if SURFACE_EDITOR_DOCUMENTS
    if(s&&s->documents)s=surface_editor_session((surface_app_state_t *)s);
#endif
    return s&&s->editor&&s->editor->job;
}
#if SURFACE_EDITOR_DOCUMENTS
void surface_editor_info(const surface_app_state_t *s,surface_editor_info_t *info)
{
    memset(info,0,sizeof *info);if(!s||!s->editor)return;
    strcpy(info->path,s->editor->path);info->bytes=(size_t)tiku_editdoc_length(s->editor->doc);
    info->dirty=surface_editor_modified(s);info->busy=surface_editor_busy(s);info->error=s->editor->error;
}
#else
int surface_editor_can_open(const surface_app_state_t *s,const char *path)
{return !surface_editor_modified(s)&&!surface_editor_busy(s);}
#endif
int surface_editor_back(surface_app_state_t *s)
{
#if SURFACE_EDITOR_DOCUMENTS
    if(s&&s->documents&&surface_editor_documents_back(s))return 1;
    s=surface_editor_session(s);
#endif
    if(!s||!s->editor||!s->editor->tools)return 0;
    s->editor->tools=0;s->shell->dirty=1;return 1;
}
#if SURFACE_BASIC
const char *surface_editor_program(surface_app_state_t *s,size_t *length)
{
#if SURFACE_EDITOR_DOCUMENTS

    s=surface_editor_session(s);
#endif
    if(!s||!s->editor||s->editor->job||!length)return NULL;
    struct surface_editor *e=s->editor;
    e->error=tiku_editdoc_read(e->doc,(tiku_edit_range_t){0,tiku_editdoc_length(e->doc)},
        (char *)e->output,sizeof e->output,length);
    if(e->error||!*length||memchr(e->output,0,*length)){s->status=TIKU_RESOURCE_INVALID;return NULL;}
    e->output[*length]=0;return (char *)e->output;
}
#endif
int surface_editor_open(surface_app_state_t *s,const char *path,uint64_t epoch,uint64_t node)
{
#if SURFACE_EDITOR_DOCUMENTS
    if(s&&s->documents)return surface_editor_documents_open(s,path,epoch,node);
#endif
    struct surface_editor *e=s->editor;
    int modified=surface_editor_modified(s);
#if SURFACE_NOTES
    if(e&&e->note&&!e->exists&&e->metadata_state==1&&!tiku_editdoc_length(e->doc))modified=0;
#endif
    if(!e||e->job||modified||!path_ok(path)||!epoch||!node)return 0;
    memcpy(e->destination,path,strlen(path)+1);e->epoch=epoch;e->node=node;e->bytes=0;e->job=1;e->submitted=0;e->error=TIKU_EDIT_OK;
    s->status=TIKU_RESOURCE_PENDING;return 1;
}
/** @brief Submit one operation while an application lease protects the session. */
static void submit(surface_app_state_t *s)
{
    struct surface_editor *e=s->editor;
    if(!tiku_app_lease_begin(s->owner,&s->lease)){s->status=TIKU_RESOURCE_LIMIT;e->job=0;return;}
    #if SURFACE_NOTES
    if(e->job==5)s->status=tiku_paged_storage_list(s->store,"/",0,0);
    else if(e->job==6)s->status=tiku_paged_storage_list(s->store,e->parent,e->epoch,e->after);
    else if(e->job==7)s->status=tiku_paged_storage_read(s->store,e->path,e->epoch,e->node,e->verify_bytes);
    else if(e->job==10)s->status=tiku_paged_storage_delete(s->store,e->path,e->epoch,e->node);
    else
#endif
    if(e->job==1)s->status=tiku_paged_storage_read(s->store,e->destination,e->epoch,e->node,e->bytes);
    else if(e->job==2)s->status=tiku_paged_storage_list(s->store,"/",0,0);
    else if(e->job==3)s->status=tiku_paged_storage_list(s->store,e->parent,e->epoch,e->after);
    else s->status=tiku_paged_storage_replace(s->store,e->destination,e->epoch,e->node,e->output,e->save_bytes);
    e->submitted=tiku_paged_storage_pending(s->store);
    if(!e->submitted){tiku_app_lease_release(&s->lease);e->job=0;}
}
void surface_editor_poll(surface_app_state_t *s)
{
#if SURFACE_EDITOR_DOCUMENTS
    if(s&&s->documents){surface_editor_documents_poll(s);return;}
#endif
    struct surface_editor *e=s->editor;if(!e||!s->store)return;
    if(e->submitted&&!tiku_paged_storage_pending(s->store)){
        e->submitted=0;tiku_app_lease_release(&s->lease);
        if(s->stopped){e->job=0;return;}
        s->status=tiku_paged_storage_status(s->store);size_t count;uint64_t epoch;int eof;
        const tiku_storage_request_t *r=tiku_paged_storage_result(s->store,&count,&epoch,&eof);
        if(!r||s->status!=TIKU_RESOURCE_OK){e->job=0;s->shell->dirty=1;return;}
        e->epoch=epoch;
        if(e->job==1){
            size_t limit=EDIT_BYTES;
#if SURFACE_NOTES
            if(e->note)limit=EDIT_RECORD;
#endif
            if(count>limit-e->bytes||(!eof&&!count)){s->status=TIKU_RESOURCE_LIMIT;e->job=0;}
            else{
                unsigned char *load=e->input;
#if SURFACE_NOTES
                if(e->note)load=e->output;
#endif
                memcpy(load+e->bytes,r->bytes,count);e->bytes+=count;
                if(eof){
                    #if SURFACE_NOTES
                    if(e->note){
                        tiku_note_metadata_t metadata;size_t offset,n;
                        if(!tiku_note_parse((char *)load,e->bytes,&metadata,&offset,&n)||n>EDIT_BYTES)e->error=TIKU_EDIT_INVALID_TEXT;
                        else{
                            e->error=tiku_editdoc_load(e->doc,(char *)load+offset,n);
                            if(!e->error){e->metadata=metadata;e->metadata_saved=++e->metadata_state;e->exists=1;
                                memcpy(e->input,load,e->bytes);e->baseline_bytes=e->bytes;}
                        }
                    }else
#endif
                    e->error=tiku_editdoc_load(e->doc,(char *)e->input,e->bytes);
                    if(!e->error){memcpy(e->path,e->destination,strlen(e->destination)+1);e->bound_epoch=e->epoch;e->wrapped=0;e->replaced=0;}
                    e->job=0;
                }else if(e->bytes==limit){s->status=TIKU_RESOURCE_LIMIT;e->job=0;}
            }
        }
#if SURFACE_NOTES
        else if(e->job==5){e->job=6;e->after=0;}
        else if(e->job==6){
            int found=0;for(size_t i=0;i<count;++i)if(!strcmp(r->entries[i].path,e->path)&&r->entries[i].kind==TIKU_RESOURCE_STREAM){e->node=r->entries[i].node_id;found=1;break;}
            if(found){e->verify_bytes=0;e->verify_version=0;e->job=7;}
            else if(!eof&&count)e->after=r->entries[count-1].node_id;
            else{s->status=TIKU_RESOURCE_STALE_CURSOR;e->job=0;}
        }else if(e->job==7){
            uint64_t length,version;
            if(!tiku_paged_storage_info(s->store,&length,&version)||length!=e->baseline_bytes||
               (e->verify_bytes&&version!=e->verify_version)||count>e->baseline_bytes-e->verify_bytes||
               memcmp(e->input+e->verify_bytes,r->bytes,count)||(!eof&&!count)){
                s->status=TIKU_RESOURCE_STALE_CURSOR;e->job=0;
            }else{
                e->verify_version=version;e->verify_bytes+=count;
                if(eof){if(e->verify_bytes==e->baseline_bytes)e->job=e->deleting?10:4;
                    else{s->status=TIKU_RESOURCE_STALE_CURSOR;e->job=0;}}
            }
        }else if(e->job==10){e->deleted=1;e->job=0;}
#endif
        else if(e->job==2){e->node=0;e->job=4;}
        else if(e->job==3){
            int found=0;for(size_t i=0;i<count;++i)if(!strcmp(r->entries[i].path,e->path)&&r->entries[i].kind==TIKU_RESOURCE_STREAM){e->node=r->entries[i].node_id;found=1;break;}
            if(found)e->job=4;
            else if(!eof&&count)e->after=r->entries[count-1].node_id;
            else{s->status=TIKU_RESOURCE_STALE_CURSOR;e->job=0;}
        }else{
            #if SURFACE_NOTES
            if(e->note){memcpy(e->input,e->output,e->save_bytes);e->baseline_bytes=e->save_bytes;e->exists=1;e->metadata_saved=e->metadata_snapshot;}
#endif
            tiku_editdoc_saved(e->doc,e->saved_state);e->bound_epoch=e->epoch;memcpy(e->path,e->destination,strlen(e->destination)+1);e->job=0;
        }
        if(e->job)s->status=TIKU_RESOURCE_PENDING;
        s->shell->dirty=1;
    }
    if(!s->stopped&&e->job&&!e->submitted)submit(s);
}
/** @brief Snapshot save input and retain it until the provider retires. */
static int save(surface_app_state_t *s,const char *path,int create)
{
    struct surface_editor *e=s->editor;if(e->job||!path_ok(path))return 0;
    e->error=tiku_editdoc_read(e->doc,(tiku_edit_range_t){0,tiku_editdoc_length(e->doc)},(char *)e->output,sizeof e->output,&e->save_bytes);
    if(e->error)return 0;
    memcpy(e->destination,path,strlen(path)+1);e->saved_state=tiku_editdoc_state(e->doc);e->after=0;
    if(create)e->job=2;
    else{
        memcpy(e->parent,e->path,strlen(e->path)+1);char *slash=strrchr(e->parent,'/');if(slash==e->parent)slash[1]=0;else *slash=0;
        e->epoch=e->bound_epoch;e->job=3;
    }
    s->status=TIKU_RESOURCE_PENDING;s->shell->dirty=1;return 1;
}
void surface_editor_dialog(surface_app_state_t *s,int dialog)
{
#if SURFACE_EDITOR_DOCUMENTS

    s=surface_editor_session(s);
#endif
    if(!s||!s->editor||input_locked(s->editor))return;
    s->editor->dialog=dialog;surface_text_open(s->shell);
}
const char *surface_editor_draft(surface_app_state_t *s)
{
#if SURFACE_EDITOR_DOCUMENTS

    s=surface_editor_session(s);
#endif
    struct surface_editor *e=s?s->editor:NULL;
    if(!e)return "";
    if(e->dialog==2)return e->path[0]?e->path:"/untitled.txt";
    if(e->dialog==3)return e->query;
    if(e->dialog==4||e->dialog==5)return e->replacement;
    return "";
}
/** @brief Search from the selected edge using the retained literal query. */
static void find(struct surface_editor *e,int backward)
{
    tiku_edit_pos_t a,b;tiku_edit_range_t found;
    e->wrapped=0;e->replaced=0;
    (void)tiku_editdoc_view_selection(e->doc,e->control.view,&a,&b);
    if(a>b){tiku_edit_pos_t swap=a;a=b;b=swap;}
    e->error=tiku_edit_find(e->doc,&(tiku_edit_query_t){e->query,strlen(e->query),e->search_flags},backward?a:b,backward,&found,&e->wrapped);
    if(!e->error)(void)tiku_editdoc_view_select(e->doc,e->control.view,found.start,found.end);
}
int surface_editor_accept(surface_app_state_t *s,const char *text)
{
#if SURFACE_EDITOR_DOCUMENTS

    s=surface_editor_session(s);
#endif
    struct surface_editor *e=s?s->editor:NULL;if(!e||input_locked(e))return 0;
    if(e->dialog==2){
#if SURFACE_NOTES
        if(e->note)return 0;
#endif
        return save(s,text,1);
    }
    e->wrapped=0;e->replaced=0;
    if(e->dialog==3){
        tiku_edit_pos_t a,b;tiku_edit_range_t found;int wrapped;
        (void)tiku_editdoc_view_selection(e->doc,e->control.view,&a,&b);
        if(strlen(text)>=sizeof e->query)return 0;
        e->error=tiku_edit_find(e->doc,&(tiku_edit_query_t){text,strlen(text),e->search_flags},a>b?a:b,0,&found,&wrapped);
        if(!e->error||e->error==TIKU_EDIT_NOTHING)strcpy(e->query,text);
        if(!e->error){e->wrapped=wrapped;(void)tiku_editdoc_view_select(e->doc,e->control.view,found.start,found.end);}
        s->shell->dirty=1;return !e->error||e->error==TIKU_EDIT_NOTHING;
    }else if(e->dialog==4||e->dialog==5){
        tiku_edit_query_t query={e->query,strlen(e->query),e->search_flags};
        if(strlen(text)>=sizeof e->replacement)return 0;
        if(e->dialog==5)e->error=tiku_edit_replace_all(e->doc,&query,text,strlen(text),e->control.view,&e->replaced);
        else{
            tiku_edit_pos_t a,b;(void)tiku_editdoc_view_selection(e->doc,e->control.view,&a,&b);
            if(a>b){tiku_edit_pos_t swap=a;a=b;b=swap;}
            if(!tiku_edit_is_match(e->doc,&query,(tiku_edit_range_t){a,b}))e->error=TIKU_EDIT_NOTHING;
            else{e->error=tiku_edit_control_insert(&e->control,text,strlen(text));if(!e->error)e->replaced=1;}
        }
        if(!e->error)strcpy(e->replacement,text);
    }else if(e->dialog==6){
        int64_t line=0,lines=tiku_editdoc_lines(e->doc);e->error=TIKU_EDIT_INVALID_RANGE;
        if(!*text)return 0;
        for(const char *p=text;*p;++p){if(*p<'0'||*p>'9'||line>lines/10)return 0;line=line*10+*p-'0';if(line>lines)return 0;}
        if(!line)return 0;
        tiku_edit_pos_t pos=tiku_editdoc_line_start(e->doc,line-1);e->error=tiku_editdoc_view_select(e->doc,e->control.view,pos,pos);
    }else e->error=tiku_edit_control_insert(&e->control,text,strlen(text));
    s->shell->dirty=1;return !e->error;
}
/** @brief Copy a complete selection without truncating UTF-8 or changing the document. */
static void clipboard(surface_app_state_t *s,int cut)
{
    struct surface_editor *e=s->editor;tiku_edit_pos_t a,b;size_t n;
    if(tiku_editdoc_view_selection(e->doc,e->control.view,&a,&b))return;
    if(a>b){tiku_edit_pos_t swap=a;a=b;b=swap;}
    if(a==b)return;
    e->error=tiku_editdoc_read(e->doc,(tiku_edit_range_t){a,b},s->shell->clipboard,sizeof s->shell->clipboard,&n);
    if(!e->error&&cut)e->error=tiku_edit_control_insert(&e->control,NULL,0);
}
void surface_editor_key(surface_app_state_t *s,const tiku_input_event_t *event)
{
#if SURFACE_EDITOR_DOCUMENTS
    if(s&&s->documents&&surface_editor_documents_key(s,event))return;
    s=surface_editor_session(s);
#endif
    struct surface_editor *e=s?s->editor:NULL;if(!e||input_locked(e))return;
    tiku_input_event_t key=*event;
    if(e->tools&&key.type!=TIKU_EVENT_KEY_DOWN)return;
    e->wrapped=0;e->replaced=0;
    if(key.type==TIKU_EVENT_KEY_DOWN){
        if(e->tools&&key.key==TIKU_KEY_ESCAPE){e->tools=0;s->shell->dirty=1;return;}
#if SURFACE_EDITOR_LINES
        if(!e->tools&&key.key==TIKU_KEY_TAB&&!(key.modifiers&(TIKU_MOD_CMD|TIKU_MOD_OPTION))){
            e->error=(key.modifiers&TIKU_MOD_SHIFT)?tiku_edit_indent(e->doc,e->control.view,4,4,1):tiku_edit_tab(e->doc,e->control.view,4,4);
            if(e->error==TIKU_EDIT_NOTHING)e->error=TIKU_EDIT_OK;
            s->shell->dirty=1;return;
        }
#endif
        if(key.modifiers&TIKU_MOD_CMD){
            if(key.key=='s'){surface_editor_action(s,ACTION_EDITOR_SAVE);return;}
            if(key.key=='f'){surface_editor_dialog(s,3);return;}
            if(key.key=='g'){find(e,!!(key.modifiers&TIKU_MOD_SHIFT));s->shell->dirty=1;return;}
            if(key.key=='l'){surface_editor_dialog(s,6);return;}
            if(key.key=='h'){surface_editor_dialog(s,(key.modifiers&TIKU_MOD_SHIFT)?5:4);return;}
            if(!e->tools&&(key.key=='c'||key.key=='x')){clipboard(s,key.key=='x');s->shell->dirty=1;return;}
            if(!e->tools&&key.key=='v'){e->error=tiku_edit_control_insert(&e->control,s->shell->clipboard,strlen(s->shell->clipboard));s->shell->dirty=1;return;}
        }
        if(e->tools)return;
        if(key.key>=32&&key.key<127&&!key.text[0])key.text[0]=(char)key.key;
    }
    e->error=tiku_edit_control_key(&e->control,&key);s->shell->dirty=1;
}
void surface_editor_action(surface_app_state_t *s,int action)
{
#if SURFACE_EDITOR_DOCUMENTS
    if(s&&s->documents&&surface_editor_documents_action(s,action))return;
    s=surface_editor_session(s);
#endif
    struct surface_editor *e=s?s->editor:NULL;if(!e||input_locked(e))return;
    e->wrapped=0;e->replaced=0;
    #if SURFACE_NOTES
    if(e->note&&action==ACTION_EDITOR_SAVE){(void)surface_editor_note_save(s);return;}
    if(e->note&&(action==ACTION_EDITOR_SAVE_AS||action==ACTION_EDITOR_RUN))return;
#endif
    if(action==ACTION_EDITOR_SAVE){if(e->path[0])(void)save(s,e->path,0);else surface_editor_dialog(s,2);}
    else if(action==ACTION_EDITOR_SAVE_AS)surface_editor_dialog(s,2);
    else if(action==ACTION_EDITOR_TYPE)surface_editor_dialog(s,1);
    else if(action==ACTION_EDITOR_FIND)surface_editor_dialog(s,3);
    else if(action==ACTION_EDITOR_TOOLS){e->tools=1;e->error=TIKU_EDIT_OK;}
    else if(action==ACTION_EDITOR_DOCUMENT)e->tools=0;
#if SURFACE_EDITOR_LINES
    else if(action==ACTION_EDITOR_LINES){e->tools=2;e->error=TIKU_EDIT_OK;}
    else if(action==ACTION_EDITOR_INDENT||action==ACTION_EDITOR_UNINDENT){
        e->error=tiku_edit_indent(e->doc,e->control.view,4,4,action==ACTION_EDITOR_UNINDENT);
        if(!e->error)e->tools=0;
    }
    else if(action>=ACTION_EDITOR_DUPLICATE&&action<=ACTION_EDITOR_DELETE_LINES){
        e->error=tiku_edit_lines(e->doc,e->control.view,(tiku_edit_line_command_t)(action-ACTION_EDITOR_DUPLICATE),"\n");
        if(!e->error)e->tools=0;
    }
#endif
    else if(action==ACTION_EDITOR_NEXT||action==ACTION_EDITOR_PREVIOUS)find(e,action==ACTION_EDITOR_PREVIOUS);
    else if(action==ACTION_EDITOR_REPLACE||action==ACTION_EDITOR_REPLACE_ALL)surface_editor_dialog(s,action==ACTION_EDITOR_REPLACE?4:5);
    else if(action==ACTION_EDITOR_GOTO)surface_editor_dialog(s,6);
    else if(action==ACTION_EDITOR_CASE){e->search_flags^=TIKU_EDIT_MATCH_CASE;e->error=TIKU_EDIT_OK;}
    else if(action==ACTION_EDITOR_WORD){e->search_flags^=TIKU_EDIT_WHOLE_WORD;e->error=TIKU_EDIT_OK;}
    else if(action==ACTION_EDITOR_ALL){e->error=tiku_editdoc_view_select(e->doc,e->control.view,0,tiku_editdoc_length(e->doc));e->tools=0;}
    else if(action==ACTION_EDITOR_UNDO)e->error=tiku_editdoc_undo(e->doc);
    else if(action==ACTION_EDITOR_REDO)e->error=tiku_editdoc_redo(e->doc);
    else if(action==ACTION_EDITOR_SELECT)e->selecting^=1;
    else if(action==ACTION_EDITOR_COPY)clipboard(s,0);
    else if(action==ACTION_EDITOR_CUT)clipboard(s,1);
    else if(action==ACTION_EDITOR_PASTE)e->error=tiku_edit_control_insert(&e->control,s->shell->clipboard,strlen(s->shell->clipboard));
    else if(action==ACTION_EDITOR_NEWLINE)e->error=tiku_edit_control_insert(&e->control,"\n",1);
    else if(action>=ACTION_EDITOR_CELL&&action<ACTION_EDITOR_CELL+TIKU_EDIT_GRID_ROWS*TIKU_EDIT_GRID_COLS){
        unsigned cell=(unsigned)(action-ACTION_EDITOR_CELL);tiku_edit_control_point(&e->control,cell/TIKU_EDIT_GRID_COLS,cell%TIKU_EDIT_GRID_COLS,e->selecting);
    }
    s->shell->dirty=1;
}
void surface_editor_view(surface_app_state_t *s,surface_view_t *v)
{
#if SURFACE_EDITOR_DOCUMENTS
    if(s&&s->documents){surface_editor_documents_view(s,v);return;}
#endif
    struct surface_editor *e=s->editor;if(!e)return;
    tiku_edit_control_grid(&e->control,&v->editor_grid);v->editor_dirty=surface_editor_modified(s);
    v->editor_busy=e->job;v->editor_dialog=e->dialog;v->editor_error=e->error;v->editor_select=e->selecting;
    v->editor_bytes=(size_t)tiku_editdoc_length(e->doc);memcpy(v->editor_path,e->path,65);v->status=s->status;
    v->editor_tools=e->tools;v->editor_search_flags=e->search_flags;v->editor_wrapped=e->wrapped;v->editor_replaced=e->replaced;strcpy(v->editor_query,e->query);
}
/** @brief Search commands occupy two columns on the tools page. */
static const int search_actions[]={ACTION_EDITOR_FIND,ACTION_EDITOR_NEXT,ACTION_EDITOR_PREVIOUS,ACTION_EDITOR_REPLACE,ACTION_EDITOR_REPLACE_ALL,ACTION_EDITOR_GOTO,ACTION_EDITOR_CASE,ACTION_EDITOR_WORD,ACTION_EDITOR_ALL,ACTION_EDITOR_DOCUMENT};
/** @brief Search command touch targets retain the document toolbar dimensions. */
static tiku_rect_t search_rect(int index)
{return (tiku_rect_t){32+(index%2)*374,120+(index/2)*56,362,44};}
/** @brief Additional commands beside the text grid. */
static const int side_actions[]={ACTION_EDITOR_TOOLS,ACTION_EDITOR_NEXT,ACTION_EDITOR_GOTO};
/** @brief Side commands remain outside the text cell grid. */
static tiku_rect_t side_rect(int index)
{return (tiku_rect_t){540,120+index*56,220,44};}
/** @brief Return a fixed touch target for one Editor command. */
static tiku_rect_t rect(int id)
{
    if(id<ACTION_EDITOR_SAVE||id>EDIT_LAST)return (tiku_rect_t){0};
    int n=id-ACTION_EDITOR_SAVE;return (tiku_rect_t){32+(n%6)*124,316+(n/6)*52,116,44};
}
#if SURFACE_EDITOR_LINES
/** @brief Whole-line editing commands use the existing tools-page grid. */
static const int line_actions[]={ACTION_EDITOR_DUPLICATE,ACTION_EDITOR_MOVE_UP,ACTION_EDITOR_MOVE_DOWN,ACTION_EDITOR_DELETE_LINES,ACTION_EDITOR_INDENT,ACTION_EDITOR_UNINDENT,ACTION_EDITOR_DOCUMENT};
#endif
int surface_editor_hit(const surface_view_t *v,int x,int y)
{
#if SURFACE_EDITOR_DOCUMENTS
    if(v->editor_list||v->editor_confirm)return surface_editor_documents_hit(v,x,y);
    if(v->editor_sessions&&!v->editor_tools){tiku_rect_t r=side_rect(1);if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return ACTION_EDITOR_SESSIONS;}
#endif
#if SURFACE_EDITOR_LINES
    if(v->editor_tools==2){for(unsigned i=0;i<7;++i){tiku_rect_t r=search_rect((int)i);if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return line_actions[i];}return 0;}
    if(!v->editor_tools
#if SURFACE_EDITOR_DOCUMENTS
       &&!v->editor_list&&!v->editor_confirm
#endif
       ){tiku_rect_t r=side_rect(2);if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return ACTION_EDITOR_LINES;}
#endif
    if(v->editor_tools){for(int i=0;i<10;++i){tiku_rect_t r=search_rect(i);if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return search_actions[i];}return 0;}
    for(int i=0;i<3;++i){tiku_rect_t r=side_rect(i);if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return side_actions[i];}
    if(x>=40&&x<520&&y>=120&&y<296)return ACTION_EDITOR_CELL+((y-120)/22)*60+(x-40)/8;
    for(int id=ACTION_EDITOR_SAVE;id<=EDIT_LAST;++id){tiku_rect_t r=rect(id);if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return id;}
    return 0;
}
void surface_editor_paint(tiku_surface_t *s,const surface_view_t *v)
{
#if SURFACE_EDITOR_DOCUMENTS
    if(v->editor_list||v->editor_confirm){surface_editor_documents_paint(s,v);return;}
#endif
    const tiku_font_t *font=tiku_font_plain();
#if SURFACE_EDITOR_LINES
    if(v->editor_tools==2){static const char *labels[]={"Duplicate lines","Move lines up","Move lines down","Delete lines","Indent (4 spaces)","Unindent","Document"};
        tiku_text(s,font,32,105,"Edit the selected lines. Each command is one Undo step.",TIKU_C_TEXT);
        for(unsigned i=0;i<7;++i)tiku_ui_button(s,search_rect((int)i),labels[i],0);
        return;
    }
#endif
    if(v->editor_tools){
        char query[256];(void)tiku_text_elide(0,v->editor_query[0]?v->editor_query:"Use Find text to set a literal query.",736,query,sizeof query);
        tiku_text(s,font,32,105,query,TIKU_C_TEXT);
        static const char *labels[]={"Find text","Find next","Find previous","Replace selected","Replace all","Go to line","Match case","Whole word","Select all","Document"};
        for(int i=0;i<10;++i){int pressed=(i==6&&(v->editor_search_flags&TIKU_EDIT_MATCH_CASE))||(i==7&&(v->editor_search_flags&TIKU_EDIT_WHOLE_WORD));tiku_ui_button(s,search_rect(i),labels[i],pressed?TIKU_S_PRESSED:0);}
        return;
    }
    tiku_text(s,font,32,105,v->editor_path[0]?v->editor_path:"Untitled",TIKU_C_TEXT);
    uint32_t paper=0xffffff;
#if SURFACE_NOTES
    if(v->foreground==SURFACE_NOTES_APP){static const uint32_t colors[]={0xffee8c,0xc8f5aa,0xb4dcff,0xffc8dc};paper=colors[v->notes_paper%4];}
#endif
    tiku_ui_sunken(s,(tiku_rect_t){32,114,736,186},paper);
    for(unsigned row=0;row<TIKU_EDIT_GRID_ROWS;++row){
        if(120+(int)(row+1)*22<s->clip.y||120+(int)row*22>=s->clip.y+s->clip.h)continue;
        for(unsigned col=0;col<TIKU_EDIT_GRID_COLS;++col){
            int x=40+(int)col*8,y=120+(int)row*22;char text[2]={v->editor_grid.rows[row][col],0};
            if(v->editor_grid.selected[row]&(UINT64_C(1)<<col))tiku_fill(s,(tiku_rect_t){x,y,8,22},0xb8d8ef);
            if(text[0])tiku_text(s,font,x,y+16,text,TIKU_C_TEXT);
        }
    }
    if(!v->editor_busy)tiku_fill(s,(tiku_rect_t){40+v->editor_grid.caret_x*8,120+v->editor_grid.caret_y*22,1,20},0);
    static const char *side_labels[]={"Search tools","Find next","Go to line"};
    for(int i=0;i<3;++i){const char *label=side_labels[i];int busy=v->editor_busy;
#if SURFACE_EDITOR_DOCUMENTS
        if(v->editor_sessions&&i==1){label="Documents";busy=0;}
#endif
#if SURFACE_EDITOR_LINES
        if(i==2)label="Edit tools";
#endif
        tiku_ui_button(s,side_rect(i),label,busy?TIKU_S_DISABLED:0);
    }
    static const char *labels[]={"Save","Save as","Type","Find","Undo","Redo","Select","Copy","Cut","Paste","New line"
#if SURFACE_BASIC
        ,"Run BASIC"
#endif
    };
    for(int id=ACTION_EDITOR_SAVE;id<=EDIT_LAST;++id){const char *label=labels[id-ACTION_EDITOR_SAVE];int busy=v->editor_busy;
#if SURFACE_NOTES
        if(v->foreground==SURFACE_NOTES_APP){if(id==ACTION_EDITOR_SAVE_AS)label="Notes list";if(id==ACTION_EDITOR_RUN)label="Note tools";busy=0;}
#endif
        tiku_ui_button(s,rect(id),label,busy?TIKU_S_DISABLED:(id==ACTION_EDITOR_SELECT&&v->editor_select)?TIKU_S_PRESSED:0);
    }
}
#if SURFACE_NOTES
int surface_editor_note(surface_app_state_t *s,const char *path)
{
    struct surface_editor *e=s?s->editor:NULL;
    if(!e||e->job||e->note||tiku_editdoc_length(e->doc)||!path_ok(path))return 0;
    e->note=1;e->metadata_state=1;strcpy(e->path,path);return 1;
}
/** @brief Prepare checked publication or deletion without changing the baseline record. */
static void note_parent(struct surface_editor *e)
{
    strcpy(e->parent,e->path);char *slash=strrchr(e->parent,'/');
    if(slash==e->parent)slash[1]=0;else *slash=0;
    strcpy(e->destination,e->path);e->after=0;e->job=e->exists?5:2;
}
int surface_editor_note_save(surface_app_state_t *s)
{
    struct surface_editor *e=s?s->editor:NULL;
    if(!e||!e->note||e->job||e->deleted)return 0;
    size_t header=tiku_note_header(e->metadata,(char *)e->output,sizeof e->output),body;
    if(!header)return 0;
    e->error=tiku_editdoc_read(e->doc,(tiku_edit_range_t){0,tiku_editdoc_length(e->doc)},
        (char *)e->output+header,sizeof e->output-header-1,&body);
    if(e->error)return 0;
    e->output[header+body]='\n';e->save_bytes=header+body+1;
    e->saved_state=tiku_editdoc_state(e->doc);e->metadata_snapshot=e->metadata_state;
    e->deleting=0;note_parent(e);s->status=TIKU_RESOURCE_PENDING;s->shell->dirty=1;return 1;
}
int surface_editor_note_delete(surface_app_state_t *s)
{
    struct surface_editor *e=s?s->editor:NULL;
    if(!e||!e->note||e->job||e->deleted)return 0;
    if(!e->exists){e->deleted=1;s->shell->dirty=1;return 1;}
    e->deleting=1;note_parent(e);s->status=TIKU_RESOURCE_PENDING;s->shell->dirty=1;return 1;
}
int surface_editor_note_copy(surface_app_state_t *source,surface_app_state_t *destination)
{
    struct surface_editor *a=source?source->editor:NULL,*b=destination?destination->editor:NULL;
    if(!a||!b||a==b||!a->note||!b->note||a->job||b->job||b->exists||tiku_editdoc_length(b->doc))return 0;
    size_t bytes;
    if(tiku_editdoc_read(a->doc,(tiku_edit_range_t){0,tiku_editdoc_length(a->doc)},(char *)a->output,sizeof a->output,&bytes)||
       tiku_editdoc_load(b->doc,(char *)a->output,bytes))return 0;
    b->metadata=a->metadata;++b->metadata_state;destination->shell->dirty=1;return 1;
}
void surface_editor_note_metadata(surface_app_state_t *s,unsigned paper,unsigned important)
{
    struct surface_editor *e=s?s->editor:NULL;
    if(!e||!e->note||input_locked(e)||paper>3||important>1||e->deleted)return;
    if(e->metadata.paper==paper&&e->metadata.important==important)return;
    e->metadata=(tiku_note_metadata_t){paper,important};++e->metadata_state;s->shell->dirty=1;
}
void surface_editor_note_info(surface_app_state_t *s,surface_note_info_t *info)
{
    memset(info,0,sizeof *info);struct surface_editor *e=s?s->editor:NULL;if(!e||!e->note)return;
    info->revision=tiku_editdoc_state(e->doc);info->metadata=e->metadata_state;
    info->paper=e->metadata.paper;info->important=e->metadata.important;
    info->exists=e->exists;info->deleted=e->deleted;info->busy=e->job;info->dirty=surface_editor_modified(s);
    info->bytes=(size_t)tiku_editdoc_length(e->doc);
    size_t n=info->bytes;if(n>64)n=64;
    if(n){size_t read;char text[65];
        if(!tiku_editdoc_read(e->doc,(tiku_edit_range_t){0,n},text,sizeof text,&read)){
            size_t end=0;while(end<read&&text[end]!='\n'&&text[end]!='\r')++end;
            if(end==64){while(end&&((unsigned char)text[end-1]&0xc0)==0x80)--end;
                if(end&&(unsigned char)text[end-1]>=0xc0)--end;}
            memcpy(info->title,text,end);info->title[end]=0;
        }
    }
    if(!info->title[0])strcpy(info->title,"Untitled");
}
#endif
