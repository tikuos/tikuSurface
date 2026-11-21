/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * process_io.c - owner-context process observations through kernel contracts.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "device_io.h"
#include "runtime.h"
#include <kernel/process/tiku_process.h>
#include <kernel/timers/tiku_clock.h>
#include <string.h>
/** @brief Copy a terminated label within its presentation bound. */
static void label(char *out,size_t capacity,const char *text)
{
    size_t n=0;if(text)while(n+1<capacity&&text[n]){out[n]=text[n];++n;}out[n]=0;
}
int surface_os_process_read(void *context,unsigned index,surface_process_info_t *out)
{
    (void)context;
    if(!surface_os_in_owner()||!out||index>=8)return -1;
    memset(out,0,sizeof *out);
    struct tiku_process *p=tiku_process_get((int8_t)index);if(!p)return 0;
    label(out->name,sizeof out->name,p->name?p->name:"(unnamed)");
    label(out->state,sizeof out->state,tiku_process_state_str(p->state));
    out->sram=tiku_process_sram_used(p);out->nvm=tiku_process_fram_used(p);
    out->uptime=(tiku_clock_time_t)(tiku_clock_time()-p->start_time)/TIKU_CLOCK_SECOND;
    out->wakes=p->wake_count;out->restarts=tiku_process_restarts(p);
    uint8_t count=tiku_process_queue_length();
    for(uint8_t i=0;i<count;++i){struct tiku_process *target=NULL;
        if(!tiku_process_queue_peek(i,NULL,&target)&&target==p)++out->events;}
    return 1;
}
