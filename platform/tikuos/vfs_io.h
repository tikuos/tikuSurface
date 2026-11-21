/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * vfs_io.h - local VFS inspection and checked device-setting writes.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_VFS_IO_H
#define SURFACE_VFS_IO_H
#include "vfs_contract.h"
/** @brief List up to six static/mounted children without invoking a node handler. */
int surface_os_vfs_list(void *,const char *,unsigned,surface_vfs_node_t *,size_t,size_t *,int *,int *);
/** @brief Copy metadata without reading a node's value. */
int surface_os_vfs_stat(void *,const char *,surface_vfs_node_t *);
/** @brief Read the selected node once after checking its identity. */
int surface_os_vfs_read(void *,const char *,uint32_t,char *,size_t,size_t *);
/** @brief Write an audited setting when identity, permission and reviewed value match. */
int surface_os_vfs_write(void *,const char *,uint32_t,const char *,size_t,const char *,size_t);
#endif
