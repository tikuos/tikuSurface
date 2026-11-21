/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_transfers.c - typed commands, confirmation, navigation and retained completion ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host.h"
#include "surface_internal.h"
#include "tiku_event.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/** @brief Independent service fixture with two retained completion slots. */
typedef struct {
    surface_transfer_snapshot_t state;
    struct {uint64_t token;int result,done;} slots[2];
    uint64_t sequence;
    unsigned submits,stops,takes,observes;
    int observe_error,submit_error;
} transfer_fixture_t;
/** @brief Copy state without advancing pending commands. */
static int observe(void *p,surface_transfer_snapshot_t *out)
{ transfer_fixture_t *f=p;++f->observes;if(f->observe_error)return f->observe_error;*out=f->state;return 0; }
/** @brief Validate the exact provisioned generation and retain a command outcome slot. */
static int submit(void *p,surface_transfer_command_t command,uint64_t generation,uint64_t *token)
{
    transfer_fixture_t *f=p;++f->submits;if(f->submit_error)return f->submit_error;
    if(generation!=f->state.generation)return SURFACE_TRANSFER_STALE;
    unsigned i=0;while(i<2&&f->slots[i].token)++i;if(i==2)return SURFACE_TRANSFER_BUSY;
    f->slots[i].token=*token=++f->sequence;f->slots[i].done=0;
    f->state.state=command==SURFACE_TRANSFER_ENABLE?SURFACE_TRANSFER_STARTING:SURFACE_TRANSFER_STOPPING;
    f->state.cleanup_required=1;if(command==SURFACE_TRANSFER_STOP)++f->stops;return 0;
}
/** @brief Consume completed results exactly once, preserving unfinished tokens. */
static int take(void *p,uint64_t token,int *result)
{
    transfer_fixture_t *f=p;
    for(unsigned i=0;i<2;++i)if(f->slots[i].token==token){
        if(!f->slots[i].done)return SURFACE_TRANSFER_PENDING;
        *result=f->slots[i].result;f->slots[i].token=0;++f->takes;return 0;
    }
    return SURFACE_TRANSFER_STALE;
}
/** @brief Complete an admitted token independently of desktop navigation. */
static void complete(transfer_fixture_t *f,uint64_t token,int result)
{
    for(unsigned i=0;i<2;++i)if(f->slots[i].token==token){f->slots[i].done=1;f->slots[i].result=result;return;}
    assert(0);
}
/** @brief Route one full pointer gesture through the shell hit tests. */
static void transfer_click(surface_t *s,int x,int y)
{
    tiku_input_event_t e={.type=TIKU_EVENT_POINTER_DOWN,.button=1,.x=x,.y=y};assert(surface_post(s,&e));
    e.type=TIKU_EVENT_POINTER_UP;assert(surface_post(s,&e));surface_step(s);
}
/** @brief Refresh observations and consume available outcomes in a fixed number of owner turns. */
static void turns(surface_host_t *h,surface_t *s)
{ for(unsigned i=0;i<9;++i){h->now+=100000;surface_step(s);} }
/** @brief Open the settings transfer page through its visible button. */
static void open_transfers(surface_host_t *h,surface_t *s)
{
    assert(surface_launch(s,SURFACE_SETTINGS));surface_step(s);turns(h,s);
    if(!s->active.state->transfers_page)transfer_click(s,650,240);
    surface_view_t v;surface_view(s,&v);assert(v.transfers_page);
}
/** @brief Retire the shell after its provider tokens have completed. */
static void finish(surface_host_t *h,surface_t **s)
{
    surface_stop(*s);for(unsigned i=0;!surface_destroy(s);++i){assert(i<1000);turns(h,*s);}
    assert(!h->memory.live);
}
/** @brief Preserve typed command results while backgrounds and application instances change. */
static void commands(const char *capture)
{
    transfer_fixture_t f={.state={.available=1,.generation=0x100000007ULL,.state=SURFACE_TRANSFER_STOPPED}};
    surface_host_t h;surface_config_t c=surface_host_config(&h);c.transfers=(surface_transfer_provider_t){&f,observe,submit,take};
    surface_t *s=surface_create(&c);assert(s);open_transfers(&h,s);
    transfer_click(s,100,360);assert(f.submits==1&&s->transfer_enable);
    transfer_click(s,100,360);assert(f.submits==1);
    transfer_click(s,350,360);assert(f.submits==2&&f.stops==1&&s->transfer_stop&&!s->transfer_confirm);
    complete(&f,s->transfer_enable,SURFACE_TRANSFER_CANCELLED);complete(&f,s->transfer_stop,0);
    f.state.state=SURFACE_TRANSFER_STOPPED;f.state.cleanup_required=0;turns(&h,s);
    assert(!s->transfer_enable&&!s->transfer_stop&&f.takes==2);
    transfer_click(s,100,360);complete(&f,s->transfer_enable,0);
    f.state.state=SURFACE_TRANSFER_READY;f.state.ready=f.state.connected=f.state.uploading=1;
    f.state.accepted=0x100000001ULL;f.state.committed=0x100000000ULL;f.state.expected=0x200000002ULL;turns(&h,s);
    surface_view_t v;surface_view(s,&v);assert(v.transfer.accepted==f.state.accepted&&v.transfer.committed==f.state.committed);
    static uint32_t full[800*480],rows[800*480];tiku_surface_t a={.px=full,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=rows;
    unsigned before=f.observes+f.submits+f.takes;surface_paint(&a,&v);
    for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}
    assert(!memcmp(full,rows,sizeof full)&&f.observes+f.submits+f.takes==before);
    if(capture){FILE *file=fopen(capture,"wb");assert(file);fprintf(file,"P6\n800 480\n255\n");
        for(unsigned i=0;i<800*480;++i){fputc(full[i]>>16&255,file);fputc(full[i]>>8&255,file);fputc(full[i]&255,file);}assert(!fclose(file));}
    before=f.submits;transfer_click(s,350,360);assert(s->transfer_confirm&&f.submits==before);
    ++f.state.generation;turns(&h,s);transfer_click(s,500,360);
    assert(!s->transfer_confirm&&!s->transfer_stop&&s->transfer_result==SURFACE_TRANSFER_STALE&&f.stops==1);
    transfer_click(s,350,360);assert(s->transfer_confirm);transfer_click(s,100,360);assert(!s->transfer_confirm&&f.stops==1);
    transfer_click(s,350,360);assert(s->transfer_confirm);transfer_click(s,70,32);assert(!s->transfer_confirm&&f.stops==1);
    surface_view(s,&v);assert(v.foreground==SURFACE_HOME&&f.state.ready);open_transfers(&h,s);
    assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);turns(&h,s);assert(f.state.ready&&f.stops==1);
    f.state=(surface_transfer_snapshot_t){.available=1,.generation=77,.state=SURFACE_TRANSFER_STOPPED};open_transfers(&h,s);
    transfer_click(s,100,360);uint64_t pending=s->transfer_enable;assert(pending);
    assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);turns(&h,s);assert(s->transfer_enable==pending);
    complete(&f,pending,0);f.state.state=SURFACE_TRANSFER_READY;f.state.ready=1;turns(&h,s);
    assert(!s->transfer_enable&&f.stops==1);
    f.state=(surface_transfer_snapshot_t){.available=1,.generation=88,.state=SURFACE_TRANSFER_STOPPED};open_transfers(&h,s);
    transfer_click(s,100,360);pending=s->transfer_enable;surface_stop(s);assert(!surface_destroy(&s));turns(&h,s);assert(s);
    complete(&f,pending,0);f.state.ready=1;finish(&h,&s);assert(f.state.ready&&f.stops==1);
}
/** @brief Refuse missing configuration/providers and expose failed observation or admission. */
static void unavailable(void)
{
    surface_host_t h;surface_config_t c=surface_host_config(&h);surface_t *s=surface_create(&c);assert(s);
    open_transfers(&h,s);surface_view_t v;surface_view(s,&v);assert(!v.transfer.available);
    transfer_click(s,100,360);assert(!s->transfer_enable);finish(&h,&s);
    transfer_fixture_t f={.state={.available=1,.state=SURFACE_TRANSFER_UNCONFIGURED}};
    c=surface_host_config(&h);c.transfers=(surface_transfer_provider_t){&f,observe,submit,take};s=surface_create(&c);assert(s);
    open_transfers(&h,s);transfer_click(s,100,360);assert(!f.submits);
    f.state.state=SURFACE_TRANSFER_STOPPED;f.state.generation=9;f.submit_error=SURFACE_TRANSFER_BUSY;turns(&h,s);
    transfer_click(s,100,360);assert(s->transfer_completed&&s->transfer_result==SURFACE_TRANSFER_BUSY&&!s->transfer_enable);
    f.observe_error=SURFACE_TRANSFER_UNAVAILABLE;turns(&h,s);surface_view(s,&v);assert(!v.transfer.available&&!v.transfer.ready);
    finish(&h,&s);
}
int main(int argc,char **argv)
{
    commands(argc==2?argv[1]:NULL);unavailable();
    puts("Transfer controls: generations, confirmation, Home/Close, retirement and native painting passed");return 0;
}
