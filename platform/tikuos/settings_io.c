/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * settings_io.c - product-scoped calibration persistence through TFS.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "settings_io.h"
#include "runtime.h"
#include <kernel/vfs/tree/tiku_vfs_tree_data.h>
#include <kernel/fs/tiku_tfs.h>
#include <kernel/fs/tiku_tfs_async.h>
#include <arch/esp32s31/tiku_storage_async.h>
#include <stdint.h>
#include <string.h>
static const char name[] = "tikusurface-cal-v1";

int surface_os_settings_read(unsigned char *out, size_t capacity, size_t *length)
{
    if (!out || !length || !surface_os_in_owner()) return -1;
    tiku_tfs_t *fs = tiku_vfs_tree_data_store();
    if (!fs) return -1;
    int r = tiku_tfs_read(fs, name, out, capacity, length);
    return r == TFS_OK ? 0 : r == TFS_ERR_NOTFOUND ? 1 : -1;
}

int surface_os_settings_write(const unsigned char *data, size_t length)
{
    if (!data || length != 32 || !surface_os_in_owner()) return -1;
    tiku_tfs_t *fs = tiku_vfs_tree_data_store();
    return fs && tiku_tfs_write(fs, name, data, length) == TFS_OK ? 0 : -1;
}

/** @brief Immutable encoded record and both transaction contexts share the runtime's PSRAM. */
typedef struct {
    tiku_tfs_async_t file;
    tiku_s31_storage_write_t storage;
    unsigned char record[32];
    unsigned begun;
} settings_bridge_t;
_Static_assert(sizeof(settings_bridge_t)<=SURFACE_SETTINGS_IO_BYTES,"settings bridge capacity");
/** @brief Translate region admission into the TFS asynchronous backend contract. */
static int write_begin(void *context,tiku_nvm_backend_t *be,size_t off,const void *data,size_t n)
{
    int rc=tiku_s31_storage_write_begin(context,be,off,data,n);
    return rc==-2 ? TFS_ERR_BUSY : rc<0 ? TFS_ERR_IO : rc;
}
/** @brief Report durable completion after the region transaction retires its undo journal. */
static int write_poll(void *context)
{
    int rc=tiku_s31_storage_write_poll(context);
    return rc<0 ? TFS_ERR_IO : rc;
}
static const tiku_tfs_async_ops_t write_ops={write_begin,write_poll};
int surface_os_settings_begin(void *bridge,const unsigned char *data,size_t length)
{
    if(!bridge || ((uintptr_t)bridge&7u) || !data || length!=32 || !surface_os_in_owner())return -1;
    settings_bridge_t *state=bridge;
    if(state->file.phase)return -1;
    state->begun=0;
    tiku_tfs_t *fs=tiku_vfs_tree_data_store();
    if(!fs)return -1;
    memcpy(state->record,data,length);
    state->begun=tiku_tfs_async_begin(fs,&state->file,name,state->record,length,&write_ops,&state->storage)==TFS_OK;
    return state->begun ? 1 : -1;
}
int surface_os_settings_poll(void *bridge)
{
    if(!bridge || ((uintptr_t)bridge&7u) || !surface_os_in_owner())return -1;
    settings_bridge_t *state=bridge;
    if(!state->begun)return -1;
    int rc=tiku_tfs_async_step(&state->file);
    return rc==1 ? 1 : !rc && state->file.result==TFS_OK ? 0 : -1;
}
void surface_os_settings_cancel(void *bridge)
{
    if(!bridge || ((uintptr_t)bridge&7u) || !surface_os_in_owner())return;
    settings_bridge_t *state=bridge;
    (void)tiku_tfs_async_cancel(&state->file);
}
