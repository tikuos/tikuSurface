/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * terminal.c - retained shell output, copied command input and cancellation.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_event.h"
#include "tiku_font.h"
#include "tiku_ui.h"
#include <string.h>
/** @brief One console owner with bounded output history and pending input. */
struct surface_terminal {
    tiku_owned_buffer_t self;
    tiku_terminal_model_t model;
    tiku_terminal_provider_t provider;
    unsigned char input[256];
    size_t count,offset;
    int ready,error,lost;
#if SURFACE_BASIC
    int program_state,program_refused;
#endif
};
int surface_terminal_start(surface_app_state_t *s)
{
    tiku_terminal_provider_t p=s->shell->config.terminal;tiku_owned_buffer_t memory;
    if(!p.ops||!p.ops->open||!p.ops->close||!p.ops->read||!p.ops->write||!p.ops->interrupt||
       !tiku_buffer_init(&memory,s->shell->config.allocator,_Alignof(struct surface_terminal))||
       !tiku_buffer_resize(&memory,1,sizeof(struct surface_terminal),1))return 0;
    struct surface_terminal *t=memory.data;s->terminal=t;t->self=memory;t->provider=p;tiku_terminal_init(&t->model);
    if(!tiku_app_lease_begin(s->owner,&s->lease))return 0;
    if(!p.ops->open(p.context)){tiku_app_lease_release(&s->lease);return 0;}
    t->ready=1;return 1;
}
void surface_terminal_dispose(surface_app_state_t *s)
{
    if(!s->terminal)return;
    tiku_owned_buffer_t memory=s->terminal->self;s->terminal=NULL;tiku_buffer_dispose(&memory);
}
/** @brief Queue complete copied input without accepting a partial command. */
static int send(surface_app_state_t *s,const void *data,size_t n)
{
    struct surface_terminal *t=s->terminal;
    if(!t||!t->ready||t->error||n>sizeof t->input-(t->count-t->offset)){s->status=TIKU_RESOURCE_BUSY;return 0;}
#if SURFACE_BASIC
    if(s->shell->config.program.state&&s->shell->config.program.state(s->shell->config.program.context)==SURFACE_PROGRAM_RUNNING){s->status=TIKU_RESOURCE_BUSY;return 0;}
#endif
    size_t pending=t->count-t->offset;memmove(t->input,t->input+t->offset,pending);
    memcpy(t->input+pending,data,n);t->count=pending+n;t->offset=0;s->shell->dirty=1;return 1;
}
int surface_terminal_command(surface_app_state_t *s,const char *command)
{
    size_t n=strlen(command);unsigned char input[256];if(n>=sizeof input)return 0;
    memcpy(input,command,n);input[n]='\n';return send(s,input,n+1);
}
#if SURFACE_BASIC
int surface_terminal_program(surface_app_state_t *s,const char *source,size_t length)
{
    struct surface_terminal *t=s->terminal;surface_program_provider_t p=s->shell->config.program;
    if(!t||!t->ready||t->error||t->count||!p.start||!p.state||!p.stop||p.state(p.context)==SURFACE_PROGRAM_RUNNING){if(t)t->program_refused=5;s->status=TIKU_RESOURCE_BUSY;return 0;}
    int result=p.start(p.context,source,length);t->program_state=p.state(p.context);t->program_refused=result==1?0:result==0?5:6;
    s->status=result==1?TIKU_RESOURCE_OK:result==0?TIKU_RESOURCE_BUSY:TIKU_RESOURCE_INVALID;
    s->shell->dirty=1;return result==1;
}
#endif
void surface_terminal_poll(surface_app_state_t *s)
{
    struct surface_terminal *t=s->terminal;if(!t||!t->ready)return;
    if(s->stopped){
#if SURFACE_BASIC
        surface_program_provider_t p=s->shell->config.program;
        if(p.state&&p.stop&&p.state(p.context)==SURFACE_PROGRAM_RUNNING&&!p.stop(p.context))return;
#endif
        if(t->provider.ops->close(t->provider.context)){t->ready=0;t->count=t->offset=0;tiku_app_lease_release(&s->lease);}
        return;
    }
    int changed=0;
#if SURFACE_BASIC
    surface_program_provider_t program=s->shell->config.program;
    if(program.state){int current=program.state(program.context);if(current!=t->program_state){t->program_state=current;changed=1;}}
#endif
    if(t->count){
        size_t n=t->count-t->offset;if(n>32)n=32;
        int wrote=t->provider.ops->write(t->provider.context,t->input+t->offset,n);
        if(wrote<0||(size_t)wrote>n){t->error=1;t->count=t->offset=0;changed=1;}
        else if(wrote){t->offset+=(size_t)wrote;if(t->offset==t->count)t->count=t->offset=0;changed=1;}
    }
    unsigned char output[16];unsigned lost=0;
    int n=t->provider.ops->read(t->provider.context,output,sizeof output,&lost);
    if(n<0||(size_t)n>sizeof output){if(!t->error)changed=1;t->error=1;}
    else if(n){(void)tiku_terminal_feed(&t->model,output,(size_t)n);changed=1;}
    if(lost){t->lost=1;changed=1;}
    s->status=t->error?TIKU_RESOURCE_UNAVAILABLE:t->count?TIKU_RESOURCE_PENDING:TIKU_RESOURCE_OK;
    if(changed)s->shell->dirty=1;
}
void surface_terminal_action(surface_app_state_t *s,int action)
{
    struct surface_terminal *t=s->terminal;if(!t)return;
    if(action==ACTION_TERM_COMMAND){surface_text_open(s->shell);return;}
    if(action==ACTION_TERM_CANCEL){
        t->count=t->offset=0;
#if SURFACE_BASIC
        surface_program_provider_t p=s->shell->config.program;
        if(p.state&&p.stop&&p.state(p.context)==SURFACE_PROGRAM_RUNNING){if(!p.stop(p.context))t->error=1;}
        else
#endif
        if(!t->provider.ops->interrupt(t->provider.context))t->error=1;
    }else if(action==ACTION_TERM_UP)tiku_terminal_scroll(&t->model,8);
    else if(action==ACTION_TERM_DOWN)tiku_terminal_scroll(&t->model,-8);
    else if(action==ACTION_TERM_RESET){tiku_terminal_init(&t->model);t->lost=0;}
    else if(action==ACTION_TERM_COPY){
        const tiku_terminal_cell_t *row=tiku_terminal_row(&t->model,t->model.view?0:(unsigned)t->model.row);
        size_t n=80;while(n&&row[n-1].ch==' ')--n;
        for(size_t i=0;i<n;++i)s->shell->clipboard[i]=row[i].ch;
        s->shell->clipboard[n]=0;
    }
    s->shell->dirty=1;
}
void surface_terminal_key(surface_app_state_t *s,const tiku_input_event_t *e)
{
    if(e->type!=TIKU_EVENT_TEXT_INPUT&&e->type!=TIKU_EVENT_KEY_DOWN)return;
    if(e->type==TIKU_EVENT_TEXT_INPUT){if(memchr(e->text,0,sizeof e->text))(void)send(s,e->text,strlen(e->text));return;}
    if(e->modifiers&TIKU_MOD_CMD){
        if(e->key=='c')surface_terminal_action(s,ACTION_TERM_CANCEL);
        else if(e->key=='v')(void)send(s,s->shell->clipboard,strlen(s->shell->clipboard));
        return;
    }
    unsigned k=e->key;const char *text=NULL;
    if(k==TIKU_KEY_RETURN)text="\n";
    else if(k==TIKU_KEY_BACKSPACE||k==TIKU_KEY_DELETE)text="\177";
    else if(k==TIKU_KEY_TAB)text="\t";
    else if(k==TIKU_KEY_UP)text="\033[A";else if(k==TIKU_KEY_DOWN)text="\033[B";
    else if(k==TIKU_KEY_LEFT)text="\033[D";else if(k==TIKU_KEY_RIGHT)text="\033[C";
    else if(k==TIKU_KEY_PAGE_UP){surface_terminal_action(s,ACTION_TERM_UP);return;}
    else if(k==TIKU_KEY_PAGE_DOWN){surface_terminal_action(s,ACTION_TERM_DOWN);return;}
    if(text)(void)send(s,text,strlen(text));
    else if(e->text[0]&&memchr(e->text,0,sizeof e->text))(void)send(s,e->text,strlen(e->text));
    else if(k>=32&&k<127){unsigned char c=(unsigned char)k;(void)send(s,&c,1);}
}
void surface_terminal_view(const surface_app_state_t *s,surface_view_t *v)
{
    const struct surface_terminal *t=s->terminal;if(!t)return;
    for(unsigned y=0;y<TIKU_TERMINAL_ROWS;++y)memcpy(v->terminal_cells[y],tiku_terminal_row(&t->model,y),sizeof v->terminal_cells[y]);
    v->terminal_row=t->model.row;v->terminal_col=t->model.col;v->terminal_cursor=t->model.cursor_on;
    v->terminal_view=t->model.view;v->terminal_error=t->error;v->terminal_lost=t->lost;
#if SURFACE_BASIC
    v->program_state=t->program_refused?t->program_refused:t->program_state;
#endif
    v->terminal_pending=(unsigned)(t->count-t->offset);v->status=s->status;
}
int surface_terminal_hit(int x,int y)
{ return x>=32&&x<768&&(x-32)%124<116&&y>=402&&y<442?ACTION_TERM_COMMAND+(x-32)/124:0; }
void surface_terminal_paint(tiku_surface_t *s,const surface_view_t *v)
{
    static const unsigned colors[9]={0x101010,0xb21818,0x189a18,0xa08400,0x285abe,0xa03caa,0x0096a0,0xbebebe,0xe0e0e0};
    const tiku_font_t *font=tiku_font_plain();tiku_ui_sunken(s,(tiku_rect_t){32,108,736,288},0x101010);
#if SURFACE_BASIC
    static const char *states[]={"Local shell","BASIC running","BASIC finished","BASIC error","BASIC stopped","BASIC busy or unavailable","BASIC source rejected"};
    const char *status=v->program_state>=0&&v->program_state<=6?states[v->program_state]:"BASIC unavailable";
    tiku_text(s,font,32,103,v->terminal_error?"Shell connection unavailable":v->terminal_lost?"Output was truncated":v->terminal_view?"Scrollback":v->terminal_pending?"Sending input":status,TIKU_C_TEXT);
#else
    tiku_text(s,font,32,103,v->terminal_error?"Shell connection unavailable":v->terminal_lost?"Output was truncated":v->terminal_view?"Scrollback":v->terminal_pending?"Sending input":"Local shell",TIKU_C_TEXT);
#endif
    for(unsigned y=0;y<TIKU_TERMINAL_ROWS;++y){
        int top=112+(int)y*14;if(top+14<=s->clip.y||top>=s->clip.y+s->clip.h)continue;
        for(unsigned x=0;x<TIKU_TERMINAL_COLS;++x){
            tiku_terminal_cell_t c=v->terminal_cells[y][x];unsigned fg=colors[c.fg<9?c.fg:8],bg=c.bg<8?colors[c.bg]:0x101010;
            if(c.attr&2){unsigned swap=fg;fg=bg;bg=swap;}
            int left=40+(int)x*8;tiku_fill(s,(tiku_rect_t){left,top,8,14},bg);
            if(c.ch&&c.ch!=' '){char text[2]={c.ch,0};tiku_text(s,c.attr&1?tiku_font_bold():font,left,top+11,text,fg);}
        }
    }
    if(v->terminal_cursor&&!v->terminal_view)tiku_fill(s,(tiku_rect_t){40+v->terminal_col*8,112+v->terminal_row*14+12,8,2},0xe0e0e0);
    static const char *labels[]={"Command","Stop","Older","Newer","Copy line","Clear"};
    for(int i=0;i<6;++i)tiku_ui_button(s,(tiku_rect_t){32+i*124,402,116,40},labels[i],0);
}
