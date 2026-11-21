/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * transfers.c - service-owned commands and retained desktop transfer controls.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_font.h"
#include "tiku_ui.h"
#include "tiku_word_model.h"
#include <string.h>
/** @brief A complete provider supports command submission and result retirement. */
static int transfer_provider(const surface_t *s)
{ return s->config.transfers.observe&&s->config.transfers.submit&&s->config.transfers.take; }
/** @brief Consume one result without canceling the independently owned service operation. */
static void transfer_take(surface_t *s,uint64_t *token)
{
    if(!*token)return;
    int result=SURFACE_TRANSFER_FAILED;
    int rc=s->config.transfers.take(s->config.transfers.context,*token,&result);
    if(rc==SURFACE_TRANSFER_PENDING||rc==SURFACE_TRANSFER_BUSY)return;
    *token=0;s->transfer_result=rc?rc:result;s->transfer_completed=1;s->dirty=1;
}
void surface_transfers_poll(surface_t *s)
{
    if(!transfer_provider(s))return;
    unsigned phase=s->transfer_phase++%3;
    if(phase==0){transfer_take(s,&s->transfer_enable);return;}
    if(phase==1){transfer_take(s,&s->transfer_stop);return;}
    if(s->stopped||(!s->clock_failed&&s->now<s->transfer_next))return;
    surface_transfer_snapshot_t state={0};int rc=s->config.transfers.observe(s->config.transfers.context,&state);
    if(rc){state.available=0;state.error=rc;}
    if(memcmp(&state,&s->transfer,sizeof state)){s->transfer=state;s->dirty=1;}
    s->transfer_next=s->now>UINT64_MAX-250000?UINT64_MAX:s->now+250000;
}
/** @brief Enable requires an idle, provisioned service and available completion capacity. */
static int transfer_can_enable(const surface_view_t *v)
{
    return v->transfer.available&&v->transfer.generation&&v->transfer.state==SURFACE_TRANSFER_STOPPED&&
        !v->transfer_enable_pending&&!v->transfer_stop_pending;
}
/** @brief Stop remains available during Enable and failed cleanup. */
static int transfer_can_stop(const surface_view_t *v)
{ return !v->transfer_stop_pending&&(v->transfer_enable_pending||v->transfer.cleanup_required); }
/** @brief Submit a generation-checked command and retain its result independently of Settings. */
static void transfer_submit(surface_t *s,surface_transfer_command_t command,uint64_t generation)
{
    uint64_t token=0;int rc=s->config.transfers.submit(s->config.transfers.context,command,generation,&token);
    s->transfer_confirm=0;s->transfer_completed=rc!=0;s->transfer_result=rc;
    if(!rc&&token){if(command==SURFACE_TRANSFER_ENABLE)s->transfer_enable=token;else s->transfer_stop=token;}
    else if(!rc){s->transfer_result=SURFACE_TRANSFER_FAILED;s->transfer_completed=1;}
    s->transfer_next=0;s->dirty=1;
}
int surface_transfers_action(surface_app_state_t *a,int action)
{
    surface_t *s=a->shell;
    if(action==ACTION_TRANSFERS_OPEN){a->transfers_page=1;s->transfer_next=0;s->dirty=1;return 1;}
    if(!a->transfers_page)return 0;
    if(action==ACTION_TRANSFERS_REFRESH){s->transfer_next=0;s->dirty=1;return 1;}
    if(action==ACTION_TRANSFERS_KEEP){s->transfer_confirm=0;s->dirty=1;return 1;}
    if(!transfer_provider(s))return 1;
    if(action==ACTION_TRANSFERS_CONFIRM&&s->transfer_confirm){
        if(!s->transfer_stop)transfer_submit(s,SURFACE_TRANSFER_STOP,s->transfer_confirm_generation);
        return 1;
    }
    if(s->transfer_confirm)return 1;
    if(action==ACTION_TRANSFERS_ENABLE&&s->transfer.available&&s->transfer.generation&&
       s->transfer.state==SURFACE_TRANSFER_STOPPED&&!s->transfer_enable&&!s->transfer_stop)
        transfer_submit(s,SURFACE_TRANSFER_ENABLE,s->transfer.generation);
    if(action==ACTION_TRANSFERS_STOP&&!s->transfer_stop&&(s->transfer_enable||s->transfer.cleanup_required)){
        if(s->transfer.connected||s->transfer.uploading||s->transfer.reading){
            s->transfer_confirm=1;s->transfer_confirm_generation=s->transfer.generation;s->dirty=1;
        }else transfer_submit(s,SURFACE_TRANSFER_STOP,s->transfer.generation);
    }
    return 1;
}
/** @brief Native button rectangles in logical display coordinates. */
static tiku_rect_t transfer_rect(int action)
{
    if(action==ACTION_TRANSFERS_OPEN)return (tiku_rect_t){600,220,168,44};
    if(action==ACTION_TRANSFERS_CONFIRM)return (tiku_rect_t){408,340,360,48};
    if(action==ACTION_TRANSFERS_KEEP)return (tiku_rect_t){32,340,360,48};
    return (tiku_rect_t){32+(action-ACTION_TRANSFERS_ENABLE)*252,340,232,48};
}
/** @brief Half-open logical button hit test. */
static int transfer_contains(tiku_rect_t r,int x,int y)
{ return x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h; }
int surface_transfers_hit(const surface_view_t *v,int x,int y)
{
    if(!v->transfers_page)return transfer_contains(transfer_rect(ACTION_TRANSFERS_OPEN),x,y)?ACTION_TRANSFERS_OPEN:0;
    if(v->transfer_confirm){
        if(transfer_contains(transfer_rect(ACTION_TRANSFERS_CONFIRM),x,y))return ACTION_TRANSFERS_CONFIRM;
        return transfer_contains(transfer_rect(ACTION_TRANSFERS_KEEP),x,y)?ACTION_TRANSFERS_KEEP:0;
    }
    if(transfer_can_enable(v)&&transfer_contains(transfer_rect(ACTION_TRANSFERS_ENABLE),x,y))return ACTION_TRANSFERS_ENABLE;
    if(transfer_can_stop(v)&&transfer_contains(transfer_rect(ACTION_TRANSFERS_STOP),x,y))return ACTION_TRANSFERS_STOP;
    return transfer_contains(transfer_rect(ACTION_TRANSFERS_REFRESH),x,y)?ACTION_TRANSFERS_REFRESH:0;
}
/** @brief Draw a labeled 64-bit byte count with the shared fixed-buffer formatter. */
static void transfer_count(tiku_surface_t *s,int y,const char *label,uint64_t value)
{
    char number[65];(void)tiku_word_format(value,10,number);
    tiku_text(s,tiku_font_plain(),32,y,label,TIKU_C_TEXT);tiku_text(s,tiku_font_bold(),440,y,number,TIKU_C_TEXT);
}
void surface_transfers_paint(tiku_surface_t *s,const surface_view_t *v)
{
    if(!v->transfers_page){tiku_ui_button(s,transfer_rect(ACTION_TRANSFERS_OPEN),"Transfers",0);return;}
    const surface_transfer_snapshot_t *t=&v->transfer;const tiku_font_t *font=tiku_font_plain();
    if(v->transfer_confirm){
        tiku_text(s,tiku_font_bold(),32,124,"Stop transfers?",TIKU_C_TEXT);
        tiku_text(s,font,32,172,"This disconnects the current peer and cancels its active transfer.",TIKU_C_TEXT);
        tiku_text(s,font,32,208,"Committed upload data remains available for resume.",TIKU_C_TEXT);
        tiku_text(s,font,32,244,"Uncommitted input is discarded.",TIKU_C_TEXT);
        tiku_ui_button(s,transfer_rect(ACTION_TRANSFERS_KEEP),"Keep transferring",0);
        tiku_ui_button(s,transfer_rect(ACTION_TRANSFERS_CONFIRM),"Stop transfers",0);return;
    }
    const char *status=!t->available?"Transfers unavailable in this configuration":v->transfer_stop_pending||t->state==SURFACE_TRANSFER_STOPPING?"Stopping transfers":
        v->transfer_enable_pending||t->state==SURFACE_TRANSFER_STARTING?"Starting transfers":t->recovering?"Recovering the transfer session":
        t->error||t->state==SURFACE_TRANSFER_ERROR?"Transfer service needs attention":t->ready?(t->connected?"Peer connected":"Listening for a peer"):
        t->state==SURFACE_TRANSFER_UNCONFIGURED?"Provision a listener from the computer console":"Transfers stopped";
    tiku_text(s,tiku_font_bold(),32,124,status,TIKU_C_TEXT);
    if(t->uploading){transfer_count(s,176,"Upload input accepted (bytes)",t->accepted);transfer_count(s,216,"Upload committed (bytes)",t->committed);transfer_count(s,256,"Expected file length (bytes)",t->expected);}
    else if(t->reading){transfer_count(s,176,"Read prepared (bytes)",t->prepared);transfer_count(s,216,"File length (bytes)",t->read_length);
        tiku_text(s,font,32,256,"Prepared bytes do not report peer acknowledgement.",TIKU_C_TEXT);}
    else {tiku_text(s,font,32,176,"Enable uses the endpoint, key and volume provisioned through the console.",TIKU_C_TEXT);
        tiku_text(s,font,32,216,"Home, Back and Close leave the transfer service running.",TIKU_C_TEXT);}
    if(v->transfer_completed)tiku_text(s,font,32,300,!v->transfer_result?"Command completed":v->transfer_result==SURFACE_TRANSFER_CANCELLED?"Enable canceled by Stop":
        v->transfer_result==SURFACE_TRANSFER_STALE?"Configuration changed. Refresh before retrying.":v->transfer_result==SURFACE_TRANSFER_BUSY?"Service busy. Refresh before retrying.":"Command failed. Refresh the service state before retrying.",TIKU_C_TEXT);
    tiku_ui_button(s,transfer_rect(ACTION_TRANSFERS_ENABLE),"Enable transfers",transfer_can_enable(v)?0:TIKU_S_DISABLED);
    tiku_ui_button(s,transfer_rect(ACTION_TRANSFERS_STOP),"Stop transfers",transfer_can_stop(v)?0:TIKU_S_DISABLED);
    tiku_ui_button(s,transfer_rect(ACTION_TRANSFERS_REFRESH),"Refresh",0);
}
