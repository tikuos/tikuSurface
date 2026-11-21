/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * device.c - paced local telemetry with retained shared sensor history.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_sensor.h"
#include "tiku_word_model.h"
#include "tiku_font.h"
#include "tiku_ui.h"
#include <string.h>
/** @brief One observation cycle and a bounded history in application storage. */
struct surface_device {
    tiku_owned_buffer_t self;
    tiku_history_t history;
    tiku_sample_t samples[120];
    uint32_t values[7];
    unsigned valid,index;
    uint64_t next;
    int sampling,paused,mode,process_sampling;
    unsigned process_index,present,process_valid,page,selected;
    surface_process_info_t processes[8];
};
static const char *const paths[]={"/sys/uptime","/sys/cpu/freq","/sys/mem/used",
    "/sys/mem/free","/sys/mem/stack_free","/proc/count","/sys/mem/sram"};
int surface_device_start(surface_app_state_t *s)
{
    tiku_owned_buffer_t memory;
    if(!tiku_buffer_init(&memory,s->shell->config.allocator,_Alignof(struct surface_device))||
       !tiku_buffer_resize(&memory,1,sizeof(struct surface_device),1))return 0;
    struct surface_device *d=memory.data;s->device=d;d->self=memory;d->sampling=1;
    return tiku_history_init(&d->history,d->samples,120);
}
void surface_device_dispose(surface_app_state_t *s)
{
    if(!s->device)return;
    tiku_owned_buffer_t memory=s->device->self;s->device=NULL;tiku_buffer_dispose(&memory);
}
/** @brief Accept complete unsigned decimal telemetry with optional trailing line ends. */
static int number(char *text,size_t length,uint32_t *out)
{
    while(length&&(text[length-1]=='\n'||text[length-1]=='\r'))--length;
    if(!length)return 0;
    for(size_t i=0;i<length;++i)if(text[i]<'0'||text[i]>'9')return 0;
    text[length]=0;uint64_t value;
    if(!tiku_word_parse(text,10,64,&value)||value>UINT32_MAX)return 0;
    *out=(uint32_t)value;return 1;
}
void surface_device_poll(surface_app_state_t *s)
{
    struct surface_device *d=s->device;if(!d||s->stopped||s->shell->clock_failed)return;
    uint64_t now=s->shell->now;
    if(d->process_sampling){
        unsigned i=d->process_index;surface_device_provider_t p=s->shell->config.device;
        memset(&d->processes[i],0,sizeof d->processes[i]);
        int result=p.process?p.process(p.context,i,&d->processes[i]):-1;
        d->processes[i].name[31]=0;d->processes[i].state[15]=0;
        d->process_valid&=~(1u<<i);d->present&=~(1u<<i);
        if(result==0||result==1)d->process_valid|=1u<<i;
        if(result==1)d->present|=1u<<i;
        if(++d->process_index==8)d->process_sampling=0;
        s->shell->dirty=1;return;
    }
    if(!d->sampling){if(d->paused||now<d->next)return;d->sampling=1;d->index=0;}
    surface_device_provider_t p=s->shell->config.device;char text[48];unsigned i=d->index;
    int count=p.read?p.read(p.context,paths[i],text,sizeof text-1):-1;
    d->valid&=~(1u<<i);
    if(count>0&&(size_t)count<sizeof text-1&&number(text,(size_t)count,&d->values[i]))d->valid|=1u<<i;
    if(++d->index==7){
        if(now<=INT64_MAX)(void)tiku_history_push(&d->history,(int64_t)now,(float)d->values[2],d->valid&(1u<<2));
        if(d->mode){d->process_sampling=1;d->process_index=0;}
        d->sampling=0;d->next=now>UINT64_MAX-1000000?UINT64_MAX:now+1000000;
    }
    s->status=d->valid?TIKU_RESOURCE_OK:TIKU_RESOURCE_UNAVAILABLE;s->shell->dirty=1;
}
void surface_device_action(surface_app_state_t *s,int action)
{
    struct surface_device *d=s->device;if(!d||s->stopped)return;
    if(action==ACTION_MONITOR_REFRESH){d->sampling=1;d->index=0;d->process_sampling=0;}
    else if(action==ACTION_MONITOR_PAUSE){d->paused^=1;if(d->paused)d->sampling=d->process_sampling=0;else d->next=0;}
    else if(action==ACTION_MONITOR_CLEAR)(void)tiku_history_init(&d->history,d->samples,120);
    else if(action==ACTION_MONITOR_PROCESSES){d->mode=1;d->process_sampling=1;d->process_index=0;}
    else if(action==ACTION_MONITOR_OVERVIEW){d->mode=0;d->process_sampling=0;}
    else if(action==ACTION_MONITOR_PREVIOUS){if(d->mode==2&&d->selected){--d->selected;d->page=(d->selected/4)*4;}else d->page=0;}
    else if(action==ACTION_MONITOR_NEXT){if(d->mode==2&&d->selected<7){++d->selected;d->page=(d->selected/4)*4;}else d->page=4;}
    else if(action>=ACTION_MONITOR_ROW&&action<ACTION_MONITOR_ROW+4){
        d->selected=d->page+(unsigned)(action-ACTION_MONITOR_ROW);d->mode=2;
    }
    s->shell->dirty=1;
}
int surface_device_back(surface_app_state_t *s)
{
    struct surface_device *d=s->device;if(!d||!d->mode)return 0;
    if(d->mode==2)d->mode=1;else{d->mode=0;d->process_sampling=0;}
    s->shell->dirty=1;return 1;
}
void surface_device_view(const surface_app_state_t *s,surface_view_t *v)
{
    const struct surface_device *d=s->device;if(!d)return;
    memcpy(v->monitor_values,d->values,sizeof d->values);v->monitor_valid=d->valid;
    v->monitor_paused=d->paused;v->monitor_sampling=d->sampling;v->monitor_count=(unsigned)d->history.count;
    for(unsigned i=0;i<v->monitor_count;++i){const tiku_sample_t *sample=tiku_history_at(&d->history,i);v->monitor_history[i]=sample->value;v->monitor_gaps[i]=(unsigned char)!sample->valid;}
    memcpy(v->monitor_processes,d->processes,sizeof d->processes);
    v->monitor_process_present=d->present;v->monitor_process_valid=d->process_valid;
    v->monitor_page=d->page;v->monitor_selected=d->selected;v->monitor_mode=d->mode;
    v->status=s->status;
}
int surface_device_hit(const surface_view_t *v,int x,int y)
{
    if(!v->monitor_mode){
        if(x>=600&&x<768&&y>=270&&y<310)return ACTION_MONITOR_PROCESSES;
        return y>=394&&y<436&&x>=32&&x<768&&(x-32)%252<232?ACTION_MONITOR_REFRESH+(x-32)/252:0;
    }
    if(y>=100&&y<142){if(x>=32&&x<264)return ACTION_MONITOR_OVERVIEW;if(x>=284&&x<516)return ACTION_MONITOR_PROCESSES;}
    if(v->monitor_mode==1&&x>=32&&x<768&&y>=166&&y<390)return ACTION_MONITOR_ROW+(y-166)/56;
    if(y>=394&&y<436&&x>=32&&x<768&&(x-32)%188<172){
        int column=(x-32)/188;return column==0?ACTION_MONITOR_REFRESH:column==1?ACTION_MONITOR_PAUSE:column==2?((v->monitor_mode==2?v->monitor_selected:v->monitor_page)?ACTION_MONITOR_PREVIOUS:0):((v->monitor_mode==2?v->monitor_selected==7:v->monitor_page!=0)?0:ACTION_MONITOR_NEXT);
    }
    return 0;
}
/** @brief Draw one process counter with the shared fixed-buffer number formatter. */
static void counter(tiku_surface_t *s,int x,int y,uint32_t value)
{char text[65];(void)tiku_word_format(value,10,text);tiku_text(s,tiku_font_bold(),x,y,text,TIKU_C_TEXT);}
/** @brief Render four process slots or one selected process's complete observations. */
static void process_paint(tiku_surface_t *s,const surface_view_t *v)
{
    const tiku_font_t *font=tiku_font_plain();
    tiku_ui_button(s,(tiku_rect_t){32,100,232,42},"Overview",0);
    tiku_ui_button(s,(tiku_rect_t){284,100,232,42},"Processes",v->monitor_mode==1?TIKU_S_PRESSED:0);
    tiku_text(s,font,536,124,v->monitor_paused?"Paused observations":"Refreshes once per second",TIKU_C_TEXT);
    if(v->monitor_mode==1){
        tiku_text(s,font,40,160,"PID    Process                                      State                 SRAM bytes",TIKU_C_TEXT);
        for(unsigned row=0;row<4;++row){unsigned i=v->monitor_page+row;int y=166+(int)row*56;
            tiku_ui_button(s,(tiku_rect_t){32,y,736,52},"",0);counter(s,44,y+31,i);
            const surface_process_info_t *p=&v->monitor_processes[i];
            const char *name=!(v->monitor_process_valid&(1u<<i))?"Unavailable":!(v->monitor_process_present&(1u<<i))?"Empty slot":p->name;
            tiku_text(s,font,100,y+31,name,TIKU_C_TEXT);
            if(v->monitor_process_present&(1u<<i)){tiku_text(s,font,430,y+31,p->state,TIKU_C_TEXT);counter(s,650,y+31,p->sram);}
        }
    }else{
        unsigned i=v->monitor_selected;const surface_process_info_t *p=&v->monitor_processes[i];
        if(!(v->monitor_process_valid&(1u<<i)))tiku_text(s,font,32,180,"Process observation unavailable. Refresh to retry.",TIKU_C_TEXT);
        else if(!(v->monitor_process_present&(1u<<i)))tiku_text(s,font,32,180,"This registry slot is empty.",TIKU_C_TEXT);
        else{
            tiku_text(s,tiku_font_bold(),32,176,p->name,TIKU_C_TEXT);tiku_text(s,font,408,176,p->state,TIKU_C_TEXT);
            const char *labels[]={"PID","SRAM bytes","Other-tier bytes","Uptime seconds","Wake count","Queued events","Restarts"};
            uint32_t values[]={i,p->sram,p->nvm,p->uptime,p->wakes,p->events,p->restarts};
            for(unsigned j=0;j<7;++j){int x=32+(int)(j%2)*376,y=212+(int)(j/2)*42;tiku_text(s,font,x,y,labels[j],TIKU_C_TEXT);counter(s,x+200,y,values[j]);}
            tiku_text(s,font,32,380,"Counters may wrap. Queued events exclude broadcasts.",TIKU_C_TEXT);
        }
    }
    const char *labels[]={"Refresh",v->monitor_paused?"Resume":"Pause","Previous","Next"};
    for(unsigned i=0;i<4;++i)tiku_ui_button(s,(tiku_rect_t){32+(int)i*188,394,172,42},labels[i],(i==2&&!(v->monitor_mode==2?v->monitor_selected:v->monitor_page))||(i==3&&(v->monitor_mode==2?v->monitor_selected==7:v->monitor_page!=0))?TIKU_S_DISABLED:0);
}
void surface_device_paint(tiku_surface_t *s,const surface_view_t *v)
{
    if(v->monitor_mode){process_paint(s,v);return;}
    static const char *labels[]={"Uptime (seconds)","CPU frequency (Hz)","Process SRAM used (bytes)",
        "Process SRAM free (bytes)","Stack headroom (bytes)","Processes","SRAM capacity (bytes)"};
    const tiku_font_t *font=tiku_font_plain();
    for(unsigned i=0;i<7;++i){int x=32+(int)(i%2)*376,y=118+(int)(i/2)*48;char value[65];
        if(v->monitor_valid&(1u<<i))(void)tiku_word_format(v->monitor_values[i],10,value);else memcpy(value,"Unavailable",12);
        tiku_text(s,font,x,y,labels[i],TIKU_C_TEXT);tiku_text(s,tiku_font_bold(),x,y+21,value,TIKU_C_TEXT);
    }
    tiku_text(s,font,408,262,v->monitor_paused?"Paused":v->monitor_sampling?"Sampling":v->monitor_valid==127?"Live observations":"Some readings unavailable",TIKU_C_TEXT);
    tiku_ui_button(s,(tiku_rect_t){600,270,168,40},"Processes",0);
    tiku_text(s,font,32,312,"Process SRAM history; kernel statics and stack excluded",TIKU_C_TEXT);
    tiku_ui_sunken(s,(tiku_rect_t){32,320,736,64},0xffffff);
    float maximum=1;for(unsigned i=0;i<v->monitor_count;++i)if(!v->monitor_gaps[i]&&v->monitor_history[i]>maximum)maximum=v->monitor_history[i];
    for(unsigned i=0;i<v->monitor_count;++i){int x=40+(int)i*6;
        if(v->monitor_gaps[i])continue;
        int y=376-(int)(v->monitor_history[i]/maximum*48);
        tiku_fill(s,(tiku_rect_t){x,y,4,377-y},0x2d5f91);
    }
    tiku_ui_button(s,(tiku_rect_t){32,394,232,42},"Refresh",0);
    tiku_ui_button(s,(tiku_rect_t){284,394,232,42},v->monitor_paused?"Resume":"Pause",0);
    tiku_ui_button(s,(tiku_rect_t){536,394,232,42},"Clear history",0);
}
