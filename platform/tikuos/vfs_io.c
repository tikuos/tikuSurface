/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * vfs_io.c - bounded metadata traversal and local reviewed settings.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "vfs_io.h"
#include "runtime.h"
#include <kernel/vfs/tiku_vfs.h>
#include <shell/tiku_shell_io.h>
#include <string.h>
/** @brief Accept a canonical absolute VFS path within the kernel's path bound. */
static int path_ok(const char *path)
{
    if(!path||path[0]!='/')return 0;
    size_t length=0;while(length<64&&path[length])++length;if(length>=64)return 0;
    const char *part=path+1;
    for(size_t i=1;i<=length;++i){
        if(path[i]&&((unsigned char)path[i]<32||(unsigned char)path[i]==127))return 0;
        if(!path[i]||path[i]=='/'){size_t n=(size_t)(path+i-part);
            if((!n&&length>1)||(n==1&&part[0]=='.')||(n==2&&part[0]=='.'&&part[1]=='.'))return 0;
            part=path+i+1;}
    }
    return 1;
}
/** @brief Device settings exposed by the desktop Inspector's editing policy. */
static int editable(const char *path)
{
    return !strcmp(path,"/sys/device/name")||!strcmp(path,"/sys/cpu/freq_target")||
        !strcmp(path,"/sys/crypto/mode")||!strcmp(path,"/sys/radio/txpower")||!strcmp(path,"/sys/time");
}
/** @brief Copy one kernel node without retaining its address or invoking its handler. */
static void describe(const tiku_vfs_node_t *node,const char *path,surface_vfs_node_t *out)
{
    memset(out,0,sizeof *out);size_t n=strlen(node->name);if(n>63)n=63;memcpy(out->name,node->name,n);
    out->id=tiku_vfs_node_id(node);if(node->type==TIKU_VFS_DIR)out->flags|=SURFACE_NODE_DIRECTORY;
    if(node->read)out->flags|=SURFACE_NODE_READ;
    if(node->write&&!(node->req_cap&~tiku_shell_io_uart.cap))out->flags|=SURFACE_NODE_WRITE;
    if((out->flags&SURFACE_NODE_WRITE)&&path&&editable(path))out->flags|=SURFACE_NODE_EDIT;
    if(node->dyn)out->flags|=SURFACE_NODE_DYNAMIC;
    if(node->desc&&(node->desc->flags&TIKU_VFS_DF_SECRET))out->flags|=SURFACE_NODE_SECRET;
    const char *policy=tiku_vfs_read_policy(node);
    out->policy=!strcmp(policy,"poll")?SURFACE_READ_POLL:!strcmp(policy,"sample")?SURFACE_READ_SAMPLE:
        !strcmp(policy,"consume")?SURFACE_READ_CONSUME:!strcmp(policy,"effect")?SURFACE_READ_EFFECT:
        !strcmp(policy,"none")?SURFACE_READ_NONE:SURFACE_READ_UNKNOWN;
}
int surface_os_vfs_list(void *context,const char *path,unsigned offset,surface_vfs_node_t *rows,size_t capacity,size_t *count,int *more,int *dynamic)
{
    (void)context;
    if(!surface_os_in_owner()||!path_ok(path)||!rows||!count||!more||!dynamic||!capacity||capacity>6||offset>512)return TIKU_VFS_EINVAL;
    *count=0;*more=*dynamic=0;const tiku_vfs_node_t *node=tiku_vfs_resolve(path);
    if(!node)return TIKU_VFS_ENOENT;
    if(node->type!=TIKU_VFS_DIR)return TIKU_VFS_EACCES;
    *dynamic=node->dyn!=NULL;
    for(size_t i=0;i<capacity;++i){const tiku_vfs_node_t *child=tiku_vfs_child_at(node,offset+(unsigned)i);if(!child)break;describe(child,NULL,&rows[(*count)++]);}
    *more=tiku_vfs_child_at(node,offset+(unsigned)*count)!=NULL;return 0;
}
int surface_os_vfs_stat(void *context,const char *path,surface_vfs_node_t *out)
{
    (void)context;
    if(!surface_os_in_owner()||!path_ok(path)||!out)return TIKU_VFS_EINVAL;
    const tiku_vfs_node_t *node=tiku_vfs_resolve(path);if(!node)return TIKU_VFS_ENOENT;
    describe(node,path,out);return 0;
}
int surface_os_vfs_read(void *context,const char *path,uint32_t id,char *out,size_t capacity,size_t *total)
{
    (void)context;
    if(!surface_os_in_owner()||!path_ok(path)||!out||!total||!capacity||capacity>1025)return TIKU_VFS_EINVAL;
    const tiku_vfs_node_t *node=tiku_vfs_resolve(path);
    if(!node||tiku_vfs_node_id(node)!=id)return TIKU_VFS_ENOENT;
    return tiku_vfs_read_total(path,out,capacity,total);
}
/** @brief Parse whole unsigned seconds without accepting signs or overflow. */
static int seconds(const char *text,size_t n,uint32_t *value)
{
    uint32_t result=0;while(n&&(text[n-1]=='\r'||text[n-1]=='\n'))--n;if(!n)return 0;
    for(size_t i=0;i<n;++i){unsigned digit=(unsigned char)text[i]-'0';if(digit>9||result>(UINT32_MAX-digit)/10)return 0;result=result*10+digit;}
    *value=result;return 1;
}
/** @brief Validate the desktop setting syntax and the device's advertised CPU targets. */
static int valid_value(const char *path,const char *value,size_t bytes)
{
    uint32_t number;
    if(!strcmp(path,"/sys/device/name")){
        if(bytes>31)return 0;
        for(size_t i=0;i<bytes;++i){unsigned char c=(unsigned char)value[i];
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return 0;}
        return 1;
    }
    if(!strcmp(path,"/sys/crypto/mode"))return (bytes==4&&!memcmp(value,"auto",4))||(bytes==2&&!memcmp(value,"sw",2));
    if(!strcmp(path,"/sys/radio/txpower")){
        int negative=value[0]=='-';
        return seconds(value+negative,bytes-(size_t)negative,&number)&&number<=(negative?100u:30u);
    }
    if(!seconds(value,bytes,&number))return 0;
    if(!strcmp(path,"/sys/time"))return 1;
    char mode[32],rates[256];size_t total;
    int n=tiku_vfs_read_total("/sys/cpu/freq_change_mode",mode,sizeof mode,&total);
    if(n<0||(size_t)n!=total||total>=sizeof mode)return 0;
    while(n&&(mode[n-1]=='\r'||mode[n-1]=='\n'))mode[--n]=0;
    if(strcmp(mode,"live")&&strcmp(mode,"reboot"))return 0;
    n=tiku_vfs_read_total("/sys/cpu/freq_available",rates,sizeof rates,&total);
    if(n<0||(size_t)n!=total||total>=sizeof rates)return 0;
    size_t start=0;
    for(size_t i=0;i<=total;++i){
        if(i==total||rates[i]==' '||rates[i]=='\n'||rates[i]=='\r'||rates[i]=='\t'){
            uint32_t target;if(i>start&&seconds(rates+start,i-start,&target)&&target==number)return 1;
            start=i+1;
        }
    }
    return 0;
}
int surface_os_vfs_write(void *context,const char *path,uint32_t id,const char *value,size_t bytes,const char *before,size_t before_bytes)
{
    (void)context;
    if(!surface_os_in_owner()||!path_ok(path)||!value||!before||!bytes||bytes>255||before_bytes>255||
       memchr(value,0,bytes)||!editable(path))return TIKU_VFS_EINVAL;
    for(size_t i=0;i<bytes;++i)if((unsigned char)value[i]<32||(unsigned char)value[i]==127)return TIKU_VFS_EINVAL;
    const tiku_vfs_node_t *node=tiku_vfs_resolve(path);
    if(!node||tiku_vfs_node_id(node)!=id)return TIKU_VFS_ENOENT;
    if(!node->write||(node->req_cap&~tiku_shell_io_uart.cap))return TIKU_VFS_EPERM;
    if(!valid_value(path,value,bytes))return TIKU_VFS_EINVAL;
    char current[256];size_t total=0;int n=tiku_vfs_read_total(path,current,sizeof current,&total);
    if(n<0)return n;
    if(total>=sizeof current||(size_t)n!=total)return TIKU_VFS_E2BIG;
    int same=(size_t)n==before_bytes&&!memcmp(current,before,before_bytes);
    if(!strcmp(path,"/sys/time")){uint32_t old_time,new_time;same=seconds(before,before_bytes,&old_time)&&seconds(current,(size_t)n,&new_time)&&new_time>=old_time&&new_time-old_time<=300;}
    if(!same)return TIKU_VFS_EBUSY;
    tiku_vfs_cap_t previous=tiku_vfs_caller_cap_set(tiku_shell_io_uart.cap);
    int result=tiku_vfs_write(path,value,bytes);(void)tiku_vfs_caller_cap_set(previous);return result;
}
