/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * diff.c - retained file snapshots and paced shared line comparison.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_diff_lines_progress.h"
#include "tiku_diff_unified_progress.h"
#include "tiku_word_model.h"
#include "tiku_text_layout.h"
#include "tiku_ui.h"
#include "tiku_font.h"
#include "tiku_event.h"
#include <string.h>
#define DIFF_BYTES 8192u
#define DIFF_LINES 256u
#define PATCH_BYTES 32768u
/** @brief File bytes and line descriptors remain at stable addresses through comparison. */
typedef struct {
    char path[65];
    size_t size,lines,longest;
    uint64_t length,version;
    int binary;
    char bytes[DIFF_BYTES];
    tiku_diff_line_t line[DIFF_LINES];
} diff_file_t;
/** @brief Three file slots retain the loaded pair during a replacement read. */
struct surface_diff {
    tiku_owned_buffer_t self,workspace,patch;
    tiku_diff_unified_progress_t *writer;
    size_t patch_bytes;
    int tools,export_mode,patch_result;
    _Alignas(max_align_t) unsigned char patch_control[1024];
    tiku_diff_lines_progress_t *progress;
    char target[65],parent[65],draft[65];
    uint64_t epoch,node,after;
    size_t received,parsed,line_start,binary_at;
    unsigned slot[2],incoming,side,loaded,first,total,column;
    int job,submitted,cancelled,flags,dialog,ready,binary,equal;
    _Alignas(max_align_t) unsigned char control[1024];
    diff_file_t files[3];
    tiku_diff_run_t runs[2*DIFF_LINES+1];
};
/** @brief Validate a bounded canonical media path. */
static int path_ok(const char *path)
{
    if(!path)return 0;
    size_t n=strlen(path);if(n<2||n>64||path[0]!='/'||path[n-1]=='/')return 0;
    const char *part=path+1;
    for(size_t i=1;i<=n;++i){
        if(path[i]&&((unsigned char)path[i]<32||(unsigned char)path[i]==127||path[i]=='\\'))return 0;
        if(!path[i]||path[i]=='/'){size_t len=(size_t)(path+i-part);
            if(!len||(len==1&&part[0]=='.')||(len==2&&part[0]=='.'&&part[1]=='.'))return 0;
            part=path+i+1;}
    }
    return 1;
}
int surface_diff_start(surface_app_state_t *s)
{
    tiku_owned_buffer_t memory;
    if(!tiku_buffer_init(&memory,s->shell->config.allocator,_Alignof(struct surface_diff))||
       !tiku_buffer_resize(&memory,1,sizeof(struct surface_diff),0))return 0;
    struct surface_diff *d=memory.data;memset(d,0,offsetof(struct surface_diff,files));
    s->comparison=d;d->self=memory;d->slot[1]=1;d->incoming=2;
    d->writer=tiku_diff_unified_progress_init(d->patch_control,sizeof d->patch_control);
    if(!d->writer)return 0;
    d->progress=tiku_diff_lines_progress_init(d->control,sizeof d->control);
    size_t bytes=tiku_diff_lines_progress_workspace(DIFF_LINES,DIFF_LINES);
    if(!d->progress||!bytes||!tiku_buffer_init(&d->workspace,s->shell->config.allocator,_Alignof(max_align_t))||
       !tiku_buffer_resize(&d->workspace,1,bytes,0))return 0;
    s->store=tiku_paged_storage_create(s->shell->config.allocator,s->shell->config.storage,&s->shell->config.caller,1,1024);
    return s->store!=NULL;
}
void surface_diff_dispose(surface_app_state_t *s)
{
    if(!s->comparison)return;
    struct surface_diff *d=s->comparison;tiku_diff_lines_progress_cancel(d->progress);
    tiku_diff_unified_progress_cancel(d->writer);tiku_buffer_dispose(&d->patch);
    tiku_buffer_dispose(&d->workspace);tiku_owned_buffer_t memory=d->self;s->comparison=NULL;tiku_buffer_dispose(&memory);
}
/** @brief Retire the active job while preserving loaded snapshots. */
static void finish(surface_app_state_t *s,tiku_resource_error_t status)
{
    struct surface_diff *d=s->comparison;d->job=0;
    tiku_diff_unified_progress_cancel(d->writer);tiku_buffer_dispose(&d->patch);
    s->status=status;s->shell->dirty=1;
}
/** @brief Start comparison after both snapshots are present. */
static void compare(surface_app_state_t *s)
{
    struct surface_diff *d=s->comparison;d->patch_result=d->export_mode=0;d->ready=0;d->first=d->total=d->column=0;
    if(d->loaded!=3){finish(s,TIKU_RESOURCE_OK);return;}
    diff_file_t *a=&d->files[d->slot[0]],*b=&d->files[d->slot[1]];
    d->binary=a->binary||b->binary;d->equal=a->size==b->size;d->binary_at=0;
    if(d->binary)d->job=6;
    else if(!tiku_diff_lines_progress_begin(d->progress,a->line,(long)a->lines,b->line,(long)b->lines,
        d->flags,d->workspace.data,d->workspace.bytes,d->runs,2*DIFF_LINES+1))d->job=5;
    else{finish(s,TIKU_RESOURCE_INVALID);return;}
    s->status=TIKU_RESOURCE_PENDING;s->shell->dirty=1;
}
int surface_diff_open(surface_app_state_t *s,unsigned side,const char *path)
{
    if(!s||!s->comparison||s->stopped||side>1||!path_ok(path))return 0;
    struct surface_diff *d=s->comparison;if(d->job||d->submitted)return 0;
    d->patch_result=d->export_mode=0;d->side=side;strcpy(d->target,path);strcpy(d->parent,path);
    char *slash=strrchr(d->parent,'/');if(slash==d->parent)slash[1]=0;else *slash=0;
    diff_file_t *f=&d->files[d->incoming];memset(f,0,offsetof(diff_file_t,bytes));strcpy(f->path,path);
    d->received=d->parsed=d->line_start=0;d->epoch=d->node=d->after=0;d->cancelled=0;d->job=1;
    s->status=TIKU_RESOURCE_PENDING;s->shell->dirty=1;return 1;
}
int surface_diff_busy(const surface_app_state_t *s)
{return s&&s->comparison&&(s->comparison->job||s->comparison->submitted||(s->shell->active.state==s&&s->shell->text_field.active));}
int surface_diff_back(surface_app_state_t *s)
{
    if(!s||!s->comparison||!s->comparison->tools)return 0;
    s->comparison->tools=0;s->shell->dirty=1;return 1;
}
int surface_diff_export(surface_app_state_t *s,const char *path)
{
    struct surface_diff *d=s?s->comparison:NULL;
    if(!d||s->stopped||d->job||d->submitted||!d->ready||d->loaded!=3||(path&&!path_ok(path)))return 0;
    d->patch_result=0;
    if(d->binary){d->patch_result=4;s->status=TIKU_RESOURCE_UNSUPPORTED;s->shell->dirty=1;return 0;}
    if(!tiku_buffer_init(&d->patch,s->shell->config.allocator,1)||!tiku_buffer_resize(&d->patch,1,PATCH_BYTES,0)){
        s->status=TIKU_RESOURCE_LIMIT;s->shell->dirty=1;return 0;
    }
    const diff_file_t *a=&d->files[d->slot[0]],*b=&d->files[d->slot[1]];
    const tiku_diff_t *result=tiku_diff_lines_progress_result(d->progress);
    if(tiku_diff_unified_progress_begin(d->writer,result,a->line,(long)a->lines,b->line,(long)b->lines,a->path,strlen(a->path),b->path,strlen(b->path),3)){
        tiku_buffer_dispose(&d->patch);s->status=TIKU_RESOURCE_INVALID;return 0;
    }
    d->patch_bytes=0;d->cancelled=0;d->export_mode=path?1:2;d->job=7;
    if(path)strcpy(d->target,path);
    s->status=TIKU_RESOURCE_PENDING;s->shell->dirty=1;return 1;
}
int surface_diff_selected(surface_app_state_t *s,unsigned side,const char *path,uint64_t epoch,uint64_t node)
{
    if(!epoch||!node||!surface_diff_open(s,side,path))return 0;
    s->comparison->epoch=epoch;s->comparison->node=node;s->comparison->job=3;return 1;
}
/** @brief Submit one identity lookup or versioned read under an application lease. */
static void submit(surface_app_state_t *s)
{
    struct surface_diff *d=s->comparison;
    if(!tiku_app_lease_begin(s->owner,&s->lease)){finish(s,TIKU_RESOURCE_LIMIT);return;}
    if(d->job==8)s->status=tiku_paged_storage_list(s->store,"/",0,0);
    else if(d->job==9)s->status=tiku_paged_storage_replace(s->store,d->target,d->epoch,0,d->patch.data,d->patch_bytes);
    else if(d->job==1)s->status=tiku_paged_storage_list(s->store,"/",0,0);
    else if(d->job==2)s->status=tiku_paged_storage_list(s->store,d->parent,d->epoch,d->after);
    else s->status=tiku_paged_storage_read(s->store,d->target,d->epoch,d->node,d->received);
    d->submitted=tiku_paged_storage_pending(s->store);
    if(!d->submitted){tiku_app_lease_release(&s->lease);finish(s,s->status);}
}
/** @brief Add one validated line descriptor to the incoming slot. */
static int line(surface_app_state_t *s,size_t end)
{
    struct surface_diff *d=s->comparison;diff_file_t *f=&d->files[d->incoming];
    if(f->lines==DIFF_LINES){finish(s,TIKU_RESOURCE_LIMIT);return 0;}
    if(end-d->line_start>f->longest)f->longest=end-d->line_start;
    f->line[f->lines++]=(tiku_diff_line_t){f->bytes+d->line_start,end-d->line_start};d->line_start=end;return 1;
}
void surface_diff_poll(surface_app_state_t *s)
{
    struct surface_diff *d=s->comparison;if(!d)return;
    if(d->submitted){
        if(tiku_paged_storage_pending(s->store))return;
        d->submitted=0;tiku_app_lease_release(&s->lease);
        if(s->stopped||d->cancelled){finish(s,TIKU_RESOURCE_CANCELLED);return;}
        size_t count;uint64_t epoch;int eof;
        const tiku_storage_request_t *r=tiku_paged_storage_result(s->store,&count,&epoch,&eof);
        if(!r||tiku_paged_storage_status(s->store)!=TIKU_RESOURCE_OK){finish(s,tiku_paged_storage_status(s->store));return;}
        d->epoch=epoch;
        if(d->job==8)d->job=9;
        else if(d->job==9){d->patch_result=1;finish(s,TIKU_RESOURCE_OK);return;}
        else if(d->job==1)d->job=2;
        else if(d->job==2){
            if(count&&r->entries[0].kind==TIKU_RESOURCE_STREAM&&!strcmp(r->entries[0].path,d->target)){d->node=r->entries[0].node_id;d->job=3;}
            else if(!eof&&count)d->after=r->entries[count-1].node_id;
            else{finish(s,TIKU_RESOURCE_NOT_FOUND);return;}
        }else{
            diff_file_t *f=&d->files[d->incoming];uint64_t length,version;
            if(!tiku_paged_storage_info(s->store,&length,&version)){finish(s,TIKU_RESOURCE_UNSUPPORTED);return;}
            if(length>DIFF_BYTES||count>DIFF_BYTES-d->received||(!count&&!eof)){finish(s,TIKU_RESOURCE_LIMIT);return;}
            if(d->received&&(length!=f->length||version!=f->version)){finish(s,TIKU_RESOURCE_STALE);return;}
            f->length=length;f->version=version;
            memcpy(f->bytes+d->received,r->bytes,count);d->received+=count;
            if(d->received>length||(eof&&d->received!=length)||(!eof&&d->received==length)){finish(s,TIKU_RESOURCE_STALE);return;}
            if(eof){f->size=d->received;d->job=4;}
        }
        s->status=TIKU_RESOURCE_PENDING;return;
    }
    if(s->stopped){tiku_diff_lines_progress_cancel(d->progress);finish(s,TIKU_RESOURCE_CANCELLED);return;}
    if(d->job==8||d->job==9){submit(s);return;}
    if(d->job>=1&&d->job<=3){submit(s);return;}
    if(d->job==7){
        size_t count=0,remaining=d->patch.bytes-d->patch_bytes;
        if(!remaining){finish(s,TIKU_RESOURCE_LIMIT);return;}
        int status=tiku_diff_unified_progress_step(d->writer,(char *)d->patch.data+d->patch_bytes,remaining,64,&count);d->patch_bytes+=count;
        if(status==TIKU_DIFF_PROGRESS_DONE){
            if(d->export_mode==1)d->job=8;
            else if(d->patch_bytes>=sizeof s->shell->clipboard){d->patch_result=3;finish(s,TIKU_RESOURCE_LIMIT);}
            else{if(d->patch_bytes){memcpy(s->shell->clipboard,d->patch.data,d->patch_bytes);s->shell->clipboard[d->patch_bytes]=0;d->patch_result=2;}else d->patch_result=5;finish(s,TIKU_RESOURCE_OK);}
        }else if(status!=TIKU_DIFF_PROGRESS_PENDING)finish(s,TIKU_RESOURCE_INVALID);
        return;
    }
    if(d->job==4){
        diff_file_t *f=&d->files[d->incoming];
        for(unsigned work=0;work<64;++work){
            if(d->parsed<f->size){unsigned char c=(unsigned char)f->bytes[d->parsed++];if(!c)f->binary=1;
                if(c=='\n'&&!line(s,d->parsed))return;
            }else if(d->line_start<f->size){if(!line(s,f->size))return;}
            else{unsigned previous=d->slot[d->side];d->slot[d->side]=d->incoming;d->incoming=previous;
                d->loaded|=1u<<d->side;compare(s);return;}
        }
    }else if(d->job==5){
        int result=tiku_diff_lines_progress_step(d->progress,64);
        if(result==0){const tiku_diff_t *out=tiku_diff_lines_progress_result(d->progress);
            d->ready=1;d->equal=!out->removed&&!out->added;d->total=(unsigned)(d->files[d->slot[0]].lines+out->added);finish(s,TIKU_RESOURCE_OK);}
        else if(result!=TIKU_DIFF_PROGRESS_PENDING)finish(s,TIKU_RESOURCE_INVALID);
    }else if(d->job==6){
        const diff_file_t *a=&d->files[d->slot[0]],*b=&d->files[d->slot[1]];
        for(unsigned work=0;work<64&&d->equal&&d->binary_at<a->size;++work,++d->binary_at)
            if(a->bytes[d->binary_at]!=b->bytes[d->binary_at])d->equal=0;
        if(!d->equal||d->binary_at==a->size){d->ready=1;finish(s,TIKU_RESOURCE_OK);}
    }
}
/** @brief Cancel submitted I/O through its retirement path or stop CPU progression immediately. */
static void cancel(surface_app_state_t *s)
{
    struct surface_diff *d=s->comparison;if(!d->job)return;
    if(d->submitted){d->cancelled=1;(void)tiku_paged_storage_cancel(s->store);}
    else{tiku_diff_lines_progress_cancel(d->progress);finish(s,TIKU_RESOURCE_CANCELLED);}
    s->shell->dirty=1;
}
/** @brief Visit the next or previous change run without leaving the loaded comparison. */
static void change(surface_app_state_t *s,int forward)
{
    struct surface_diff *d=s->comparison;const tiku_diff_t *result=tiku_diff_lines_progress_result(d->progress);
    if(!d->ready||d->binary||!result)return;
    unsigned row=0,target=d->first;
    for(long i=0;i<result->nrun;++i){const tiku_diff_run_t *r=&result->run[i];
        if(r->kind!=TIKU_DIFF_SAME){if(forward&&row>d->first){target=row;break;}if(!forward&&row<d->first)target=row;}
        row+=(unsigned)r->n;
    }
    d->first=target;
}
void surface_diff_action(surface_app_state_t *s,int action)
{
    struct surface_diff *d=s->comparison;if(!d||s->stopped)return;
    if(action==ACTION_DIFF_CANCEL){cancel(s);return;}
    if(action==ACTION_DIFF_TOOLS){d->tools=1;s->shell->dirty=1;return;}
    if(action==ACTION_DIFF_DOCUMENT){d->tools=0;s->shell->dirty=1;return;}
    if(d->job)return;
    if(action==ACTION_DIFF_COPY_PATCH){(void)surface_diff_export(s,NULL);return;}
    if(action==ACTION_DIFF_SAVE_PATCH){
        if(d->ready&&d->loaded==3&&!d->binary){d->dialog=3;strcpy(d->draft,"/changes.patch");surface_text_open(s->shell);}
        return;
    }
    if(action==ACTION_DIFF_LEFT||action==ACTION_DIFF_RIGHT){d->dialog=action==ACTION_DIFF_LEFT?1:2;
        unsigned side=(unsigned)d->dialog-1;strcpy(d->draft,d->loaded&(1u<<side)?d->files[d->slot[side]].path:side?"/right.txt":"/left.txt");surface_text_open(s->shell);return;}
    if(action==ACTION_DIFF_SPACE||action==ACTION_DIFF_CR){d->flags^=action==ACTION_DIFF_SPACE?TIKU_DIFF_SPACE:TIKU_DIFF_CR;compare(s);return;}
    if(action==ACTION_DIFF_SWAP){unsigned previous=d->slot[0];d->slot[0]=d->slot[1];d->slot[1]=previous;
        d->loaded=((d->loaded&1u)<<1)|((d->loaded&2u)>>1);compare(s);return;}
    if(d->loaded!=3||!d->ready||d->binary)return;
    if(action==ACTION_DIFF_UP){if(d->first>=8)d->first-=8;else d->first=0;}
    else if(action==ACTION_DIFF_DOWN){if(d->first+8<d->total)d->first+=8;}
    else if(action==ACTION_DIFF_TEXT_LEFT){d->column=d->column>=32?d->column-32:0;}
    else if(action==ACTION_DIFF_TEXT_RIGHT){size_t longest=d->files[d->slot[0]].longest;
        if(d->files[d->slot[1]].longest>longest)longest=d->files[d->slot[1]].longest;
        if(d->loaded==3&&d->column+32<longest)d->column+=32;}
    else if(action==ACTION_DIFF_PREVIOUS||action==ACTION_DIFF_NEXT)change(s,action==ACTION_DIFF_NEXT);
    s->shell->dirty=1;
}
void surface_diff_key(surface_app_state_t *s,const tiku_input_event_t *e)
{
    if(!e||e->type!=TIKU_EVENT_KEY_DOWN)return;
    if((e->modifiers&TIKU_MOD_CMD)&&e->key=='s')surface_diff_action(s,ACTION_DIFF_SAVE_PATCH);
    else if((e->modifiers&TIKU_MOD_CMD)&&e->key=='c')surface_diff_action(s,ACTION_DIFF_COPY_PATCH);
    else if((e->modifiers&TIKU_MOD_CMD)&&e->key=='o')surface_diff_action(s,e->modifiers&TIKU_MOD_SHIFT?ACTION_DIFF_RIGHT:ACTION_DIFF_LEFT);
    else if(e->key==TIKU_KEY_UP||e->key==TIKU_KEY_PAGE_UP)surface_diff_action(s,ACTION_DIFF_UP);
    else if(e->key==TIKU_KEY_DOWN||e->key==TIKU_KEY_PAGE_DOWN)surface_diff_action(s,ACTION_DIFF_DOWN);
    else if(e->key==TIKU_KEY_LEFT)surface_diff_action(s,e->modifiers&TIKU_MOD_SHIFT?ACTION_DIFF_TEXT_LEFT:ACTION_DIFF_PREVIOUS);
    else if(e->key==TIKU_KEY_RIGHT)surface_diff_action(s,e->modifiers&TIKU_MOD_SHIFT?ACTION_DIFF_TEXT_RIGHT:ACTION_DIFF_NEXT);
}
int surface_diff_accept(surface_app_state_t *s,const char *text)
{
    if(!s||!s->comparison)return 0;
    struct surface_diff *d=s->comparison;int ok=d->dialog==3?surface_diff_export(s,text):d->dialog>=1&&d->dialog<=2&&surface_diff_open(s,(unsigned)d->dialog-1,text);
    if(ok)d->dialog=0;
    return ok;
}
const char *surface_diff_draft(surface_app_state_t *s){return s->comparison?s->comparison->draft:"";}
/** @brief Copy a bounded line prefix, retaining complete UTF-8 sequences at its boundary. */
static void preview(const tiku_diff_line_t *line,unsigned column,char out[80])
{
    if(column>=line->len){out[0]=0;return;}
    size_t start=column;
    while(start<line->len&&start-column<4&&((unsigned char)line->text[start]&0xc0)==0x80)++start;
    const char *source=line->text+start;size_t length=line->len-start;
    size_t n=length;if(n>79)n=79;
    for(size_t i=0;i<n;++i){unsigned char c=(unsigned char)source[i];out[i]=c<32||c==127?' ':(char)c;}
    if(n<length&&n){size_t lead=n-1;while(lead&&((unsigned char)out[lead]&0xc0)==0x80)--lead;
        unsigned char c=(unsigned char)out[lead];size_t need=c>=0xf0?4:c>=0xe0?3:c>=0xc0?2:1;if(lead+need>n)n=lead;}
    while(n&&(out[n-1]==' '))--n;
    out[n]=0;
}
void surface_diff_view(const surface_app_state_t *s,surface_view_t *v)
{
    const struct surface_diff *d=s->comparison;if(!d)return;
    v->diff_loaded=d->loaded;v->diff_busy=d->job!=0;v->diff_ready=d->ready;v->diff_flags=d->flags;v->diff_dialog=d->dialog;
    v->diff_tools=d->tools;v->diff_export=d->export_mode;v->diff_patch_result=d->patch_result;
    v->diff_column=d->column;
    for(unsigned side=0;side<2;++side)if((d->loaded&(1u<<side))&&d->files[d->slot[side]].longest>v->diff_columns)v->diff_columns=(unsigned)d->files[d->slot[side]].longest;
    v->diff_first=d->first;v->diff_total=d->total;v->diff_binary=d->binary;v->diff_equal=d->equal;v->status=s->status;
    for(unsigned side=0;side<2;++side)if(d->loaded&(1u<<side))strcpy(v->diff_path[side],d->files[d->slot[side]].path);
    if(!d->ready||d->binary)return;
    const tiku_diff_t *result=tiku_diff_lines_progress_result(d->progress);if(!result)return;
    v->diff_removed=(unsigned)result->removed;v->diff_added=(unsigned)result->added;
    unsigned row=0;
    for(long k=0;k<result->nrun&&v->diff_rows<8;++k){const tiku_diff_run_t *r=&result->run[k];
        if(row+(unsigned)r->n<=d->first){row+=(unsigned)r->n;continue;}
        for(long i=0;i<r->n&&v->diff_rows<8;++i,++row){if(row<d->first)continue;
            unsigned index=v->diff_rows++;v->diff_kind[index]=(unsigned)r->kind;
            for(unsigned side=0;side<2;++side){if((side==0&&r->kind==TIKU_DIFF_ADD)||(side==1&&r->kind==TIKU_DIFF_DEL))continue;
                unsigned number=(unsigned)(side?r->b:r->a)+(unsigned)i;v->diff_line[side][index]=number+1;
                preview(&d->files[d->slot[side]].line[number],d->column,v->diff_text[side][index]);}
        }
    }
}
/** @brief Patch tools retain separate 44-pixel touch targets. */
static const int patch_actions[]={ACTION_DIFF_SAVE_PATCH,ACTION_DIFF_COPY_PATCH,ACTION_DIFF_DOCUMENT,ACTION_DIFF_CANCEL};
/** @brief Two columns keep export controls outside comparison rows. */
static tiku_rect_t patch_rect(unsigned index)
{return (tiku_rect_t){32+(int)(index%2)*376,128+(int)(index/2)*60,360,44};}
/** @brief Command geometry surrounding the two text columns. */
static tiku_rect_t rect(int action)
{
    if(action==ACTION_DIFF_TOOLS)return (tiku_rect_t){592,346,176,44};
    int n=action-ACTION_DIFF_LEFT;
    if(n<2)return (tiku_rect_t){32+n*376,92,176,44};
    if(n<4)return (tiku_rect_t){220+(n-2)*376,92,172,44};
    return (tiku_rect_t){32+(n-4)*92,390,88,44};
}
/** @brief Match touch admission to the copied comparison state. */
static int enabled(const surface_view_t *v,int action)
{
    if(action==ACTION_DIFF_TOOLS||action==ACTION_DIFF_DOCUMENT)return 1;
    if(action==ACTION_DIFF_SAVE_PATCH||action==ACTION_DIFF_COPY_PATCH)return !v->diff_busy&&v->diff_loaded==3&&v->diff_ready&&!v->diff_binary;
    if(action==ACTION_DIFF_CANCEL)return v->diff_busy;
    if(v->diff_busy)return 0;
    if(action<=ACTION_DIFF_CR)return 1;
    if(action==ACTION_DIFF_SWAP)return v->diff_loaded!=0;
    if(!v->diff_ready||v->diff_binary)return 0;
    if(action==ACTION_DIFF_TEXT_LEFT)return v->diff_column>0;
    if(action==ACTION_DIFF_TEXT_RIGHT)return v->diff_column+32<v->diff_columns;
    if(action==ACTION_DIFF_UP)return v->diff_first>0;
    if(action==ACTION_DIFF_DOWN)return v->diff_first+8<v->diff_total;
    return v->diff_removed||v->diff_added;
}
int surface_diff_hit(const surface_view_t *v,int x,int y)
{
    if(v->diff_tools){for(unsigned i=0;i<4;++i){tiku_rect_t r=patch_rect(i);if(enabled(v,patch_actions[i])&&x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return patch_actions[i];}return 0;}
    tiku_rect_t tools=rect(ACTION_DIFF_TOOLS);if(x>=tools.x&&x<tools.x+tools.w&&y>=tools.y&&y<tools.y+tools.h)return ACTION_DIFF_TOOLS;
    for(int action=ACTION_DIFF_LEFT;action<=ACTION_DIFF_SWAP;++action){tiku_rect_t r=rect(action);
        if(enabled(v,action)&&x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return action;}
    return 0;
}
/** @brief Draw a single fitted text label in the supplied column. */
static void text(tiku_surface_t *s,int x,int y,int width,const char *value)
{char fitted[160];tiku_text_elide(0,value,width,fitted,sizeof fitted);tiku_text(s,tiku_font_plain(),x,y,fitted,TIKU_C_TEXT);}
void surface_diff_paint(tiku_surface_t *s,const surface_view_t *v)
{
    if(v->diff_tools){static const char *labels[]={"Save patch","Copy patch","Comparison","Cancel operation"};
        text(s,32,108,736,"Unified patch with three context lines. Save to an unused media path.");
        for(unsigned i=0;i<4;++i)tiku_ui_button(s,patch_rect(i),labels[i],enabled(v,patch_actions[i])?0:TIKU_S_DISABLED);
        text(s,32,284,736,v->diff_binary?"Binary files do not have a text patch.":"Copy admits 255 bytes. Larger patches can be saved to storage.");return;
    }
    static const char *labels[]={"Left file","Right file","Ignore spaces","Ignore final CR","Page up","Page down","Prev diff","Next diff","Cancel","Text <","Text >","Swap"};
    for(int action=ACTION_DIFF_LEFT;action<=ACTION_DIFF_SWAP;++action){int flags=enabled(v,action)?0:TIKU_S_DISABLED;
        if((action==ACTION_DIFF_SPACE&&(v->diff_flags&TIKU_DIFF_SPACE))||(action==ACTION_DIFF_CR&&(v->diff_flags&TIKU_DIFF_CR)))flags|=TIKU_S_PRESSED;
        tiku_ui_button(s,rect(action),labels[action-ACTION_DIFF_LEFT],flags);}
    for(unsigned side=0;side<2;++side){int x=32+(int)side*376;
        text(s,x,156,360,v->diff_loaded&(1u<<side)?v->diff_path[side]:side?"Choose the right file":"Choose the left file");
        tiku_ui_sunken(s,(tiku_rect_t){x,164,360,180},0xffffff);
    }
    for(unsigned row=0;row<v->diff_rows;++row){int y=168+(int)row*22;
        if(y>=s->clip.y+s->clip.h||y+22<s->clip.y)continue;
        for(unsigned side=0;side<2;++side){if(!v->diff_line[side][row])continue;int x=36+(int)side*376;
            if(v->diff_kind[row]!=TIKU_DIFF_SAME)tiku_fill(s,(tiku_rect_t){x,y,352,22},side?0xd9f0d4:0xf4d8d8);
            char number[65];tiku_word_format(v->diff_line[side][row],10,number);text(s,x,y+16,36,number);text(s,x+40,y+16,308,v->diff_text[side][row]);}
    }
    tiku_ui_button(s,rect(ACTION_DIFF_TOOLS),"Patch tools",0);
    if(v->diff_ready){
        if(v->diff_binary)text(s,32,376,540,v->diff_equal?"Binary files are identical.":"Binary files differ. Use TikuHex to inspect their bytes.");
        else if(v->diff_equal)text(s,32,376,540,"Files are identical under the selected comparison options.");
        else{char removed[65],added[65];tiku_word_format(v->diff_removed,10,removed);tiku_word_format(v->diff_added,10,added);
            text(s,32,376,130,"Removed lines:");text(s,160,376,90,removed);text(s,284,376,130,"Added lines:");text(s,402,376,90,added);}
    }
}
