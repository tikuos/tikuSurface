/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * vfs_contract.h - copied local namespace metadata and reviewed value writes.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_VFS_CONTRACT_H
#define SURFACE_VFS_CONTRACT_H
#include <stddef.h>
#include <stdint.h>
enum { SURFACE_NODE_DIRECTORY=1,SURFACE_NODE_READ=2,SURFACE_NODE_WRITE=4,
    SURFACE_NODE_EDIT=8,SURFACE_NODE_DYNAMIC=16,SURFACE_NODE_SECRET=32 };
enum { SURFACE_READ_NONE,SURFACE_READ_POLL,SURFACE_READ_SAMPLE,
    SURFACE_READ_CONSUME,SURFACE_READ_EFFECT,SURFACE_READ_UNKNOWN };
/** @brief Copied metadata; id identifies a node during this boot, not a pointer. */
typedef struct {char name[64];uint32_t id;unsigned flags,policy;} surface_vfs_node_t;
/** @brief Foreground operations complete before return; a negative result is an error. */
typedef struct {
    void *context;
    int (*list)(void *,const char *,unsigned,surface_vfs_node_t *,size_t,size_t *,int *,int *);
    int (*stat)(void *,const char *,surface_vfs_node_t *);
    int (*read)(void *,const char *,uint32_t,char *,size_t,size_t *);
    int (*write)(void *,const char *,uint32_t,const char *,size_t,const char *,size_t);
} surface_vfs_provider_t;
#endif
