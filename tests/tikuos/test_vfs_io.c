/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_vfs_io.c - bounded namespace metadata and reviewed writes in the real VFS.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "vfs_io.h"
#include <kernel/vfs/tiku_vfs.h>
#include <shell/tiku_shell_io.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int write_error;
static int owner=1;static unsigned reads,writes,dynamic_lists;static char value[256]="board";
const tiku_shell_io_t tiku_shell_io_uart={.cap=TIKU_VFS_CAP_SYS};
int surface_os_in_owner(void){return owner;}
static int read_value(char *out,size_t capacity){++reads;return snprintf(out,capacity,"%s",value);}
static int write_value(const char *text,size_t n){++writes;if(write_error)return TIKU_VFS_EIO;assert(n<sizeof value);memcpy(value,text,n);value[n]=0;return 0;}
static int read_mode(char *out,size_t cap){return snprintf(out,cap,"reboot\n");}
static int read_rates(char *out,size_t cap){return snprintf(out,cap,"40000000 80000000\n");}
static void dynamic_list(tiku_vfs_dyn_list_cb cb,void *ctx){++dynamic_lists;cb("hidden",ctx);}
int main(void)
{
    tiku_vfs_desc_t desc={.vtype=TIKU_VFS_T_STR,.fresh=TIKU_VFS_FRESH_CACHED};
    tiku_vfs_node_t device[]={{.name="name",.type=TIKU_VFS_FILE,.read=read_value,.write=write_value,.desc=&desc,.req_cap=TIKU_VFS_CAP_SYS}};
    tiku_vfs_node_t sys[9]={{.name="device",.type=TIKU_VFS_DIR,.children=device,.child_count=1}};
    char names[8][8];for(unsigned i=1;i<9;++i){snprintf(names[i-1],8,"value%u",i);sys[i]=(tiku_vfs_node_t){.name=names[i-1],.type=TIKU_VFS_FILE,.read=read_value,.desc=&desc};}
    tiku_vfs_dynops_t dyn={.list=dynamic_list};tiku_vfs_node_t roots[]={{.name="sys",.type=TIKU_VFS_DIR,.children=sys,.child_count=9},
        {.name="data",.type=TIKU_VFS_DIR,.dyn=&dyn}};
    tiku_vfs_node_t root={.name="",.type=TIKU_VFS_DIR,.children=roots,.child_count=2};tiku_vfs_init(&root);
    const tiku_vfs_node_t mounted={.name="mounted",.type=TIKU_VFS_FILE,.read=read_value};assert(!tiku_vfs_mount("/sys",&mounted));
    assert(tiku_vfs_child_at(NULL,0)==NULL&&tiku_vfs_child_at(&device[0],0)==NULL&&tiku_vfs_child_at(&roots[0],9)==&mounted&&tiku_vfs_child_at(&roots[0],10)==NULL);
    surface_vfs_node_t rows[6],node;size_t count,total;int more,dynamic;
    assert(!surface_os_vfs_list(NULL,"/sys",0,rows,6,&count,&more,&dynamic)&&count==6&&more&&!dynamic&&!reads);
    assert(!surface_os_vfs_list(NULL,"/sys",6,rows,6,&count,&more,&dynamic)&&count==4&&!more&&!strcmp(rows[3].name,"mounted")&&!reads);
    assert(!surface_os_vfs_list(NULL,"/data",0,rows,6,&count,&more,&dynamic)&&!count&&dynamic&&!dynamic_lists&&!reads);
    assert(!surface_os_vfs_stat(NULL,"/sys/device/name",&node)&&!reads&&(node.flags&SURFACE_NODE_EDIT)&&node.policy==SURFACE_READ_POLL);
    char out[1025];assert(surface_os_vfs_read(NULL,"/sys/device/name",node.id,out,sizeof out,&total)==5&&total==5&&!strcmp(out,"board"));
    tiku_vfs_caller_cap_set(TIKU_VFS_CAP_NONE);
    assert(!surface_os_vfs_write(NULL,"/sys/device/name",node.id,"desk",4,"board",5)&&writes==1&&!strcmp(value,"desk"));
    assert(tiku_vfs_caller_cap_get()==TIKU_VFS_CAP_NONE);
    assert(surface_os_vfs_write(NULL,"/sys/device/name",node.id,"next",4,"board",5)==TIKU_VFS_EBUSY&&writes==1);
    assert(surface_os_vfs_write(NULL,"/sys/device/name",node.id+1,"next",4,"desk",4)==TIKU_VFS_ENOENT&&writes==1);
    device[0].req_cap=TIKU_VFS_CAP_NET;assert(surface_os_vfs_write(NULL,"/sys/device/name",node.id,"next",4,"desk",4)==TIKU_VFS_EPERM&&writes==1);device[0].req_cap=TIKU_VFS_CAP_SYS;
    const char *bad[]={"name with spaces","abcdefghijklmnopqrstuvwxyz012345","bad/name","name\n"};
    for(unsigned i=0;i<4;++i)assert(surface_os_vfs_write(NULL,"/sys/device/name",node.id,bad[i],strlen(bad[i]),"desk",4)<0&&writes==1);
    assert(surface_os_vfs_stat(NULL,"/sys/../device/name",&node)<0);owner=0;assert(surface_os_vfs_stat(NULL,"/sys/device/name",&node)<0);owner=1;
    tiku_vfs_node_t cpu[]={
        {.name="freq_target",.type=TIKU_VFS_FILE,.read=read_value,.write=write_value},
        {.name="freq_change_mode",.type=TIKU_VFS_FILE,.read=read_mode},
        {.name="freq_available",.type=TIKU_VFS_FILE,.read=read_rates}};
    tiku_vfs_node_t crypto={.name="mode",.type=TIKU_VFS_FILE,.read=read_value,.write=write_value};
    tiku_vfs_node_t radio={.name="txpower",.type=TIKU_VFS_FILE,.read=read_value,.write=write_value};
    tiku_vfs_node_t settings[]={
        {.name="cpu",.type=TIKU_VFS_DIR,.children=cpu,.child_count=3},
        {.name="crypto",.type=TIKU_VFS_DIR,.children=&crypto,.child_count=1},
        {.name="radio",.type=TIKU_VFS_DIR,.children=&radio,.child_count=1},
        {.name="time",.type=TIKU_VFS_FILE,.read=read_value,.write=write_value}};
    roots[0].children=settings;roots[0].child_count=4;
    const char *paths[]={"/sys/cpu/freq_target","/sys/crypto/mode","/sys/radio/txpower","/sys/time"};
    const char *accepted[]={"80000000","sw","-20","4294967295"};
    const char *refused[][4]={{"90000000","40000000oops","42949672960","0"},{"auto-extra","sw-x","hw","SW"},{"-101","31","-","1x"},{"4294967296","-1","1.2","123x"}};
    for(unsigned i=0;i<4;++i){
        strcpy(value,i==3?"1000":"old");assert(!surface_os_vfs_stat(NULL,paths[i],&node));unsigned prior=writes;
        for(unsigned j=0;j<4;++j)assert(surface_os_vfs_write(NULL,paths[i],node.id,refused[i][j],strlen(refused[i][j]),value,strlen(value))<0&&writes==prior);
        char before[256];strcpy(before,value);assert(!surface_os_vfs_write(NULL,paths[i],node.id,accepted[i],strlen(accepted[i]),before,strlen(before))&&writes==prior+1);
    }
    assert(!surface_os_vfs_stat(NULL,"/sys/time",&node));strcpy(value,"1200\n");
    assert(!surface_os_vfs_write(NULL,"/sys/time",node.id,"2000",4,"1000\n",5));
    strcpy(value,"1400");assert(surface_os_vfs_write(NULL,"/sys/time",node.id,"2000",4,"1000",4)==TIKU_VFS_EBUSY);
    strcpy(value,"900");assert(surface_os_vfs_write(NULL,"/sys/time",node.id,"2000",4,"1000",4)==TIKU_VFS_EBUSY);
    write_error=1;tiku_vfs_caller_cap_set(TIKU_VFS_CAP_NET);
    assert(surface_os_vfs_write(NULL,"/sys/time",node.id,"2000",4,"900",3)==TIKU_VFS_EIO&&tiku_vfs_caller_cap_get()==TIKU_VFS_CAP_NET);
    desc.flags=TIKU_VFS_DF_SECRET;settings[3].desc=&desc;
    unsigned prior=reads;assert(!surface_os_vfs_stat(NULL,"/sys/time",&node)&&(node.flags&SURFACE_NODE_SECRET)&&reads==prior);
    puts("VFS Inspector provider: bounded static/mounted pages, metadata-only lookup, explicit read, value/identity/permission conflicts and validation passed");return 0;
}
