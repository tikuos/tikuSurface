/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * inspector.c - local namespace navigation, explicit reads and reviewed writes.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_font.h"
#include "tiku_text_layout.h"
#include "tiku_ui.h"
#include <string.h>
/** @brief One Inspector owns its copied metadata, preview and pending operation. */
struct surface_inspector {
    tiku_owned_buffer_t self;
    surface_vfs_node_t rows[6],node;
    char path[64],value[1025],before[256],input[256];
    size_t count,bytes,total,before_bytes;
    unsigned offset;
    int more,dynamic,reading,dialog,phase,error,loaded,saved;
};
/** @brief Validate a path entered in the field before queueing a lookup. */
static int path_ok(const char *path)
{
    size_t n=strlen(path);if(!n||n>63||path[0]!='/')return 0;
    const char *part=path+1;
    for(size_t i=1;i<=n;++i){
        if(path[i]&&((unsigned char)path[i]<32||(unsigned char)path[i]==127))return 0;
        if(!path[i]||path[i]=='/'){size_t length=(size_t)(path+i-part);
            if((!length&&n>1)||(length==1&&*part=='.')||(length==2&&part[0]=='.'&&part[1]=='.'))return 0;
            part=path+i+1;}
    }
    return 1;
}
int surface_inspector_start(surface_app_state_t *s)
{
    tiku_owned_buffer_t memory;
    if(!tiku_buffer_init(&memory,s->shell->config.allocator,_Alignof(struct surface_inspector))||
       !tiku_buffer_resize(&memory,1,sizeof(struct surface_inspector),1))return 0;
    struct surface_inspector *v=memory.data;s->inspector=v;v->self=memory;memcpy(v->path,"/",2);v->phase=1;return 1;
}
void surface_inspector_dispose(surface_app_state_t *s)
{
    if(!s->inspector)return;
    tiku_owned_buffer_t memory=s->inspector->self;s->inspector=NULL;tiku_buffer_dispose(&memory);
}
/** @brief Queue metadata first; it does not consume a node value. */
static void lookup(surface_app_state_t *s)
{
    struct surface_inspector *v=s->inspector;v->phase=1;v->loaded=v->saved=v->error=0;
    v->count=v->bytes=v->total=0;v->value[0]=0;
    v->reading=v->dynamic=v->more=0;memset(&v->node,0,sizeof v->node);s->shell->dirty=1;
}
int surface_inspector_open(surface_app_state_t *s,const char *path)
{
    if(!s||s->stopped||!path||!path_ok(path))return 0;
    if(s->inspector&&s->inspector->phase)return 0;
    if(!s->inspector&&!surface_inspector_start(s))return 0;
    struct surface_inspector *v=s->inspector;strcpy(v->path,path);v->offset=0;v->dialog=0;lookup(s);return 1;
}

void surface_inspector_poll(surface_app_state_t *s)
{
    struct surface_inspector *v=s->inspector;if(!v||s->stopped||!v->phase)return;
    surface_vfs_provider_t p=s->shell->config.vfs;int phase=v->phase;v->phase=0;int result=-1;
    if(phase==1&&p.stat){
        result=p.stat(p.context,v->path,&v->node);
        if(!result){v->reading=!(v->node.flags&SURFACE_NODE_DIRECTORY);if(!v->reading)v->phase=2;}
    }else if(phase==2&&p.list){
        result=p.list(p.context,v->path,v->offset,v->rows,6,&v->count,&v->more,&v->dynamic);
        if(v->count>6){v->count=0;result=-1;}
    }else if(phase==3&&p.read){
        result=p.read(p.context,v->path,v->node.id,v->value,sizeof v->value,&v->total);
        if(result>=0){
            if((size_t)result>=sizeof v->value||v->total<(size_t)result){result=-1;v->bytes=0;}
            else{v->bytes=(size_t)result;v->value[v->bytes]=0;v->loaded=1;
                if(v->bytes<sizeof v->before&&v->total==v->bytes&&!memchr(v->value,0,v->bytes)){
                    memcpy(v->before,v->value,v->bytes+1);v->before_bytes=v->bytes;
                }else v->node.flags&=~SURFACE_NODE_EDIT;
                result=0;
            }
        }
    }else if(phase==4&&p.write){
        result=p.write(p.context,v->path,v->node.id,v->input,strlen(v->input),v->before,v->before_bytes);
        if(!result){v->saved=1;v->loaded=0;v->phase=3;}
    }
    v->error=result;s->status=result<0?TIKU_RESOURCE_UNAVAILABLE:TIKU_RESOURCE_OK;s->shell->dirty=1;
}
int surface_inspector_back(surface_app_state_t *s)
{
    struct surface_inspector *v=s->inspector;if(!v||!strcmp(v->path,"/"))return 0;
    char *slash=strrchr(v->path,'/');if(slash==v->path)slash[1]=0;else *slash=0;
    v->offset=0;lookup(s);return 1;
}
void surface_inspector_action(surface_app_state_t *s,int action)
{
    struct surface_inspector *v=s->inspector;if(!v||s->stopped||v->phase)return;
    if(action==ACTION_INSPECTOR_PATH){v->dialog=1;surface_text_open(s->shell);}
    else if(action==ACTION_INSPECTOR_EDIT&&v->reading&&v->loaded&&(v->node.flags&SURFACE_NODE_EDIT)&&!v->error){v->dialog=2;surface_text_open(s->shell);}
    else if(action==ACTION_INSPECTOR_REFRESH)lookup(s);
    else if(action==ACTION_INSPECTOR_PREVIOUS&&v->offset>=6){v->offset-=6;lookup(s);}
    else if(action==ACTION_INSPECTOR_NEXT&&v->more){v->offset+=6;lookup(s);}
    else if(action==ACTION_INSPECTOR_READ&&v->reading&&(v->node.flags&SURFACE_NODE_READ)){v->saved=v->loaded=0;v->phase=3;}
    else if(action==ACTION_INSPECTOR_COPY&&v->loaded){
        if(v->total!=v->bytes||v->bytes>=sizeof s->shell->clipboard||memchr(v->value,0,v->bytes))v->error=-6;
        else memcpy(s->shell->clipboard,v->value,v->bytes+1);
    }else if(action>=ACTION_INSPECTOR_ROW&&action<ACTION_INSPECTOR_ROW+6&&!v->reading){
        unsigned i=(unsigned)(action-ACTION_INSPECTOR_ROW);if(i>=v->count)return;
        size_t n=strlen(v->path),m=strlen(v->rows[i].name);int slash=n>1;
        if(n+(size_t)slash+m>=sizeof v->path){v->error=-6;return;}
        if(slash)v->path[n++]='/';
        memcpy(v->path+n,v->rows[i].name,m+1);v->offset=0;lookup(s);
    }
    s->shell->dirty=1;
}
const char *surface_inspector_draft(surface_app_state_t *s)
{
    struct surface_inspector *v=s->inspector;if(v->dialog==1)return v->path;
    memcpy(v->input,v->before,v->before_bytes+1);size_t n=v->before_bytes;
    while(n&&(v->input[n-1]=='\r'||v->input[n-1]=='\n'))v->input[--n]=0;
    return v->input;
}
int surface_inspector_accept(surface_app_state_t *s,const char *text)
{
    struct surface_inspector *v=s->inspector;if(!v||v->phase||!text)return 0;
    size_t n=strlen(text);
    if(v->dialog==1){if(!path_ok(text))return 0;memcpy(v->path,text,n+1);v->offset=0;lookup(s);return 1;}
    if(v->dialog!=2||!(v->node.flags&SURFACE_NODE_EDIT)||!v->loaded||!n||n>=sizeof v->input)return 0;
    for(size_t i=0;i<n;++i)if((unsigned char)text[i]<32||(unsigned char)text[i]==127)return 0;
    memcpy(v->input,text,n+1);v->phase=4;v->saved=0;s->shell->dirty=1;return 1;
}
void surface_inspector_view(const surface_app_state_t *s,surface_view_t *out)
{
    const struct surface_inspector *v=s->inspector;if(!v)return;
    memcpy(out->inspector_rows,v->rows,sizeof v->rows);out->inspector_node=v->node;
    memcpy(out->inspector_path,v->path,sizeof v->path);memcpy(out->inspector_value,v->value,sizeof v->value);
    out->inspector_total=v->total;out->inspector_bytes=v->bytes;out->inspector_count=(unsigned)v->count;out->inspector_offset=v->offset;
    out->inspector_more=v->more;out->inspector_dynamic=v->dynamic;out->inspector_reading=v->reading;out->inspector_dialog=v->dialog;
    out->inspector_phase=v->phase;out->inspector_error=v->error;out->inspector_loaded=v->loaded;out->inspector_saved=v->saved;out->status=s->status;
}
int surface_inspector_hit(const surface_view_t *v,int x,int y)
{
    if(v->inspector_phase)return 0;
    if(y>=100&&y<124&&x>=32&&x<768)return ACTION_INSPECTOR_PATH;
    if(!v->inspector_reading&&y>=128&&y<392&&x>=32&&x<768){unsigned row=(unsigned)(y-128)/44;return row<v->inspector_count?ACTION_INSPECTOR_ROW+(int)row:0;}
    if(y<394||y>=436||x<32||x>=768||(x-32)%188>=172)return 0;
    int column=(x-32)/188;
    if(!v->inspector_reading)return column==0?ACTION_INSPECTOR_REFRESH:column==1?ACTION_INSPECTOR_PATH:column==2?(v->inspector_offset?ACTION_INSPECTOR_PREVIOUS:0):(v->inspector_more?ACTION_INSPECTOR_NEXT:0);
    return column==0?((v->inspector_node.flags&SURFACE_NODE_READ)?ACTION_INSPECTOR_READ:0):column==1?(v->inspector_loaded&&(v->inspector_node.flags&SURFACE_NODE_EDIT)&&!v->inspector_error?ACTION_INSPECTOR_EDIT:0):column==2?(v->inspector_loaded?ACTION_INSPECTOR_COPY:0):ACTION_INSPECTOR_REFRESH;
}
void surface_inspector_paint(tiku_surface_t *s,const surface_view_t *v)
{
    const tiku_font_t *font=tiku_font_plain();tiku_ui_button(s,(tiku_rect_t){32,100,736,24},v->inspector_path,0);
    if(!v->inspector_reading){
        for(unsigned i=0;i<v->inspector_count;++i){char label[69];
            const char *prefix=v->inspector_rows[i].flags&SURFACE_NODE_DIRECTORY?"DIR ":v->inspector_rows[i].flags&SURFACE_NODE_WRITE?"rw  ":"r   ";
            memcpy(label,prefix,4);memcpy(label+4,v->inspector_rows[i].name,64);label[68]=0;
            tiku_ui_button(s,(tiku_rect_t){32,128+(int)i*44,736,40},label,0);}
        static const char *labels[]={"Refresh","Path","Previous","Next"};
        for(int i=0;i<4;++i)tiku_ui_button(s,(tiku_rect_t){32+i*188,394,172,42},labels[i],(i==2&&!v->inspector_offset)||(i==3&&!v->inspector_more)?TIKU_S_DISABLED:0);
    }else{
        static const char *policies[]={"No read handler","Read observes the current value","Read samples hardware","Read consumes queued data","Read changes device state","Read behavior is device-defined"};
        unsigned policy=v->inspector_node.policy;
        tiku_text(s,font,32,165,policy<6?policies[policy]:"Read behavior unavailable",TIKU_C_TEXT);
        tiku_text(s,font,32,190,(v->inspector_node.flags&SURFACE_NODE_EDIT)?"Editable setting; firmware validates the value":(v->inspector_node.flags&SURFACE_NODE_WRITE)?"Writable node; editing policy not admitted":"Read-only node",TIKU_C_TEXT);
        tiku_ui_sunken(s,(tiku_rect_t){32,202,736,176},0xffffff);
        size_t at=0;for(unsigned line=0;line<8&&at<v->inspector_bytes;++line){char text[89];unsigned n=0;
            while(at<v->inspector_bytes&&n<88){unsigned char c=(unsigned char)v->inspector_value[at++];if(c=='\n')break;if(c=='\r')continue;text[n++]=c>=32&&c<127?(char)c:'.';}
            text[n]=0;tiku_text(s,font,40,222+(int)line*21,text,TIKU_C_TEXT);
        }
        if(!v->inspector_loaded)tiku_text(s,font,40,222,"Choose Read once to inspect this value.",TIKU_C_TEXT);
        else if(v->inspector_total>v->inspector_bytes)tiku_text(s,font,32,389,"Preview truncated to 1024 bytes.",TIKU_C_TEXT);
        const char *labels[]={"Read once","Edit setting","Copy value","Refresh"};
        for(int i=0;i<4;++i)tiku_ui_button(s,(tiku_rect_t){32+i*188,394,172,42},labels[i],
            (i==0&&!(v->inspector_node.flags&SURFACE_NODE_READ))||(i==1&&(!(v->inspector_node.flags&SURFACE_NODE_EDIT)||!v->inspector_loaded||v->inspector_error))||(i==2&&!v->inspector_loaded)?TIKU_S_DISABLED:0);
    }
}
