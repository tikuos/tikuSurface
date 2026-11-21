/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * console_io.c - bounded local shell output and copied input admission.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "console_io.h"
#include "runtime.h"
#include <shell/tiku_shell_io.h>
#include <services/console/tiku_console.h>
#include <limits.h>
#if SURFACE_BASIC
#include <basic/tiku_basic.h>
#endif
#include <string.h>
/** @brief PSRAM bridge; shell output and UI calls serialize in foreground context. */
typedef struct {
    const tiku_shell_io_t *previous;
    tiku_shell_io_t backend;
    unsigned head,tail,lost;
#if SURFACE_BASIC
    tiku_basic_job_t job;
#endif
    unsigned char output[4096];
} console_t;
_Static_assert(sizeof(console_t)<=SURFACE_CONSOLE_BRIDGE_BYTES,"Console bridge exceeds runtime reservation");
static console_t *active;
/** @brief Copy a shell byte, reporting overflow without blocking the producer. */
static void put(char c)
{
    console_t *s=active;if(!s)return;
    unsigned next=(s->head+1)%sizeof s->output;
    if(next==s->tail){if(s->lost<UINT_MAX)++s->lost;}
    else{s->output[s->head]=(unsigned char)c;s->head=next;}
    if(s->previous&&s->previous->putc)s->previous->putc(c);
}
/** @brief Input is consumed by the shell through the console injection queue. */
static unsigned char ready(void){return 0;}
/** @brief The shell reads the shared console, not this output backend. */
static int get(void){return -1;}

size_t surface_os_console_size(void){return sizeof(console_t);}
void surface_os_console_init(void *p){if(p)memset(p,0,sizeof(console_t));}
int surface_os_console_open(void *p)
{
    if(!p||!surface_os_in_owner()||active||tiku_shell_io_get_backend()!=&tiku_shell_io_uart)return 0;
    console_t *s=p;s->head=s->tail=s->lost=0;s->previous=tiku_shell_io_get_backend();active=s;
    s->backend=(tiku_shell_io_t){put,ready,get,s->previous->flags,s->previous->cap};
    tiku_shell_io_set_backend(&s->backend);return 1;
}
int surface_os_console_interrupt(void *p)
{
    if(!p||active!=p||!surface_os_in_owner()||tiku_shell_io_get_backend()!=&active->backend)return 0;
    const unsigned char cancel=3;tiku_console_inject_clear();return tiku_console_inject(&cancel,1)==1;
}
int surface_os_console_close(void *p)
{
    if(!p||!surface_os_in_owner())return 0;
    if(active!=p)return 1;
    console_t *s=p;
#if SURFACE_BASIC
    tiku_basic_job_stop_owned(&s->job);
#endif
    if(tiku_shell_io_get_backend()==&active->backend){
#if SURFACE_BASIC
        if(s->job.state!=TIKU_BASIC_JOB_NONE)tiku_console_inject_clear();
        else
#endif
        if(!surface_os_console_interrupt(p))return 0;
        tiku_shell_io_set_backend(s->previous);
    }
    active=NULL;s->previous=NULL;return 1;
}
int surface_os_console_write(void *p,const void *bytes,size_t n)
{
    if(!p||active!=p||!surface_os_in_owner()||tiku_shell_io_get_backend()!=&active->backend||(!bytes&&n)||n>256)return -1;
    return (int)tiku_console_inject(bytes,n);
}
int surface_os_console_read(void *p,void *bytes,size_t n,unsigned *lost)
{
    if(!p||active!=p||!surface_os_in_owner()||tiku_shell_io_get_backend()!=&active->backend||(!bytes&&n)||n>256||!lost)return -1;
    console_t *s=p;unsigned char *out=bytes;size_t count=0;
    while(count<n&&s->tail!=s->head){out[count++]=s->output[s->tail];s->tail=(s->tail+1)%sizeof s->output;}
    *lost=s->lost;s->lost=0;return (int)count;
}

#if SURFACE_BASIC
int surface_os_program_start(void *p,const char *source,size_t length)
{
    if(!p||active!=p||!surface_os_in_owner()||tiku_shell_io_get_backend()!=&active->backend)return 0;
    if(!source||!length||length>8192||memchr(source,0,length)||source[length])return -1;
    if(tiku_basic_mode_active())return 0;
    console_t *s=p;tiku_console_inject_clear();
    return tiku_basic_job_start_owned(source,&s->backend,&s->job)==0?1:-1;
}
int surface_os_program_state(void *p)
{
    if(!p||active!=p||!surface_os_in_owner())return -1;
    return ((console_t *)p)->job.state;
}
int surface_os_program_stop(void *p)
{
    if(!p||active!=p||!surface_os_in_owner())return 0;
    tiku_basic_job_stop_owned(&((console_t *)p)->job);return 1;
}
#endif
