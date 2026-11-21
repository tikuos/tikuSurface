/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_process_io.c - copied process observations with the production registry.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "device_io.h"
#include <kernel/process/tiku_process.h>
#include <kernel/memory/tiku_mem.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int owner=1;
static tiku_clock_time_t ticks;
volatile uint8_t tiku_s31_in_trap,tiku_crit_held;
int surface_os_in_owner(void){return owner;}
void tiku_atomic_enter(void){}
void tiku_atomic_exit(void){}
tiku_clock_time_t tiku_clock_time(void){return ticks;}
void tiku_sched_notify(void){}
void tiku_vfs_unwatch_all(struct tiku_process *p){(void)p;}
tiku_mem_err_t tiku_arena_stats(const tiku_arena_t *a,tiku_mem_stats_t *s)
{(void)a;memset(s,0,sizeof *s);return TIKU_MEM_ERR_INVALID;}
int main(void)
{
    tiku_process_init();surface_process_info_t info;memset(&info,0xa5,sizeof info);
    assert(surface_os_process_read(NULL,0,&info)==0&&!info.name[0]&&!info.sram);
    struct tiku_process p={.pid=-1,.name="worker",.is_running=1,.state=TIKU_PROCESS_STATE_WAITING,.sram_used=123,.fram_used=456,.start_time=100,.wake_count=65535,.restart_total=7};
    assert(tiku_process_register("worker",&p)==0);
    p.state=TIKU_PROCESS_STATE_WAITING;p.is_running=1;p.start_time=100;p.wake_count=65535;
    ticks=100+TIKU_CLOCK_SECOND*10;
    assert(tiku_process_post(&p,TIKU_EVENT_CONTINUE,NULL));
    assert(tiku_process_post(&p,TIKU_EVENT_CONTINUE,NULL));
    assert(tiku_process_post(TIKU_PROCESS_BROADCAST,TIKU_EVENT_CONTINUE,NULL));
    assert(surface_os_process_read(NULL,0,&info)==1&&!strcmp(info.name,"worker")&&!strcmp(info.state,"waiting"));
    assert(info.sram==123&&info.nvm==456&&info.uptime==10&&info.wakes==65535&&info.events==2&&info.restarts==7);
    p.name="abcdefghijklmnopqrstuvwxyz0123456789";assert(surface_os_process_read(NULL,0,&info)==1&&strlen(info.name)==31);
    p.name=NULL;assert(surface_os_process_read(NULL,0,&info)==1&&!strcmp(info.name,"(unnamed)"));
    p.state=TIKU_PROCESS_STATE_STOPPED;p.is_running=0;assert(surface_os_process_read(NULL,0,&info)==1&&!strcmp(info.state,"stopped"));
    tiku_process_init();assert(surface_os_process_read(NULL,0,&info)==0&&!info.name[0]&&!info.events);
    assert(surface_os_process_read(NULL,8,&info)<0&&surface_os_process_read(NULL,0,NULL)<0);
    owner=0;assert(surface_os_process_read(NULL,0,&info)<0);
    puts("Process provider: real registry, copied labels, empty/stopped slots, counters, bounded queue observation and owner admission passed");return 0;
}
