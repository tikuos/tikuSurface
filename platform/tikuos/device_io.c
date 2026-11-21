/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * device_io.c - local telemetry admission through passive VFS policy.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "device_io.h"
#include "runtime.h"
#include <kernel/vfs/tiku_vfs.h>
#include <string.h>
int surface_os_device_read(void *context,const char *path,char *out,size_t capacity)
{
    (void)context;
    if(!surface_os_in_owner()||!path||!out||!capacity||capacity>128)return TIKU_VFS_EINVAL;
    static const char *const allowed[]={"/sys/uptime","/sys/cpu/freq","/sys/mem/used",
        "/sys/mem/free","/sys/mem/stack_free","/proc/count","/sys/mem/sram"};
    unsigned i;for(i=0;i<sizeof allowed/sizeof allowed[0];++i)if(!strcmp(path,allowed[i]))break;
    if(i==sizeof allowed/sizeof allowed[0])return TIKU_VFS_EACCES;
    if(!strcmp(path,"/proc/count")){
        const tiku_vfs_node_t *node=tiku_vfs_resolve(path);
        if(!node)return TIKU_VFS_ENOENT;
        if(!node->desc)return tiku_vfs_read_node(node,out,capacity);
    }
    return tiku_vfs_read_passive(path,out,capacity);
}
