/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_device_io.c - passive local observations through the production VFS.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "device_io.h"
#include <kernel/vfs/tiku_vfs.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int owner=1;static unsigned reads;
int surface_os_in_owner(void){return owner;}
static int value(char *out,size_t n){++reads;return snprintf(out,n,"123\n");}
int main(void)
{
    tiku_vfs_desc_t desc={.vtype=TIKU_VFS_T_U32,.fresh=TIKU_VFS_FRESH_CACHED};
    tiku_vfs_node_t sys[]={{.name="uptime",.type=TIKU_VFS_FILE,.read=value,.desc=&desc}};
    tiku_vfs_node_t proc[]={{.name="count",.type=TIKU_VFS_FILE,.read=value}};
    tiku_vfs_node_t roots[]={{.name="sys",.type=TIKU_VFS_DIR,.children=sys,.child_count=1},
        {.name="proc",.type=TIKU_VFS_DIR,.children=proc,.child_count=1}};
    tiku_vfs_node_t root={.name="",.type=TIKU_VFS_DIR,.children=roots,.child_count=2};tiku_vfs_init(&root);
    char out[128];assert(surface_os_device_read(NULL,"/sys/uptime",out,sizeof out)==4&&!strcmp(out,"123\n"));
    unsigned before=reads;assert(surface_os_device_read(NULL,"/dev/adc",out,sizeof out)==TIKU_VFS_EACCES&&reads==before);
    const unsigned flags[]={TIKU_VFS_DF_SECRET,TIKU_VFS_DF_READ_EFFECT,TIKU_VFS_DF_READ_CONSUMES};
    for(unsigned i=0;i<3;++i){desc.flags=flags[i];assert(surface_os_device_read(NULL,"/sys/uptime",out,sizeof out)==TIKU_VFS_EACCES&&reads==before);}
    desc.flags=0;desc.fresh=TIKU_VFS_FRESH_LIVE;assert(surface_os_device_read(NULL,"/sys/uptime",out,sizeof out)==TIKU_VFS_EACCES&&reads==before);
    desc.fresh=TIKU_VFS_FRESH_CACHED;desc.ecost=TIKU_VFS_E_BUS;assert(surface_os_device_read(NULL,"/sys/uptime",out,sizeof out)==TIKU_VFS_EACCES&&reads==before);
    assert(surface_os_device_read(NULL,"/proc/count",out,sizeof out)==4&&reads==before+1);
    proc[0].desc=&desc;assert(surface_os_device_read(NULL,"/proc/count",out,sizeof out)==TIKU_VFS_EACCES&&reads==before+1);
    assert(surface_os_device_read(NULL,"/sys/mem/used",out,sizeof out)==TIKU_VFS_ENOENT);
    owner=0;assert(surface_os_device_read(NULL,"/sys/uptime",out,sizeof out)<0);owner=1;
    assert(surface_os_device_read(NULL,NULL,out,sizeof out)<0&&surface_os_device_read(NULL,"/sys/uptime",out,129)<0);
    puts("Device provider: real VFS, owner/size limits, missing nodes and passive read admission passed");return 0;
}
