/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * services.c - cached radio and transfer observations for device information.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "services.h"
#include "runtime.h"
#include <interfaces/wireless/tiku_wireless.h>
#include <services/storage/tiku_media_transfer_server.h>
#include <stdio.h>
#if SURFACE_TRANSFERS
#include <services/storage/tiku_media_transfer_service.h>
/** @brief Normalize service outcomes without exposing firmware enums to the desktop. */
static int transfer_result(int rc)
{
    if(!rc)return SURFACE_TRANSFER_OK;
    if(rc==TIKU_MT_CONTROL_PENDING)return SURFACE_TRANSFER_PENDING;
    if(rc==TIKU_MFS_BUSY)return SURFACE_TRANSFER_BUSY;
    if(rc==TIKU_MFS_STALE)return SURFACE_TRANSFER_STALE;
    if(rc==TIKU_MFS_ARG)return SURFACE_TRANSFER_INVALID;
    if(rc==TIKU_MT_CONTROL_CANCELLED)return SURFACE_TRANSFER_CANCELLED;
    if(rc==TIKU_MFS_NOFS||rc==TIKU_MFS_UNSUPPORTED||rc==TIKU_MT_CONTROL_OWNER)return SURFACE_TRANSFER_UNAVAILABLE;
    return SURFACE_TRANSFER_FAILED;
}
/** @brief Copy capability, command state and byte progress without reading media. */
static int transfer_observe(void *context,surface_transfer_snapshot_t *out)
{
    (void)context;if(!out)return SURFACE_TRANSFER_INVALID;
    if(!surface_os_in_owner())return SURFACE_TRANSFER_UNAVAILABLE;
    tiku_mt_service_snapshot_t state;int rc=tiku_mt_service_observe(&state);if(rc)return transfer_result(rc);
    static const unsigned states[]={SURFACE_TRANSFER_UNCONFIGURED,SURFACE_TRANSFER_STOPPED,SURFACE_TRANSFER_STARTING,
        SURFACE_TRANSFER_READY,SURFACE_TRANSFER_STOPPING,SURFACE_TRANSFER_ERROR};
    if((unsigned)state.transfer.control.state>=sizeof states/sizeof states[0])return SURFACE_TRANSFER_FAILED;
    const tiku_mt_listener_snapshot_t *s=&state.transfer.session;
    *out=(surface_transfer_snapshot_t){.available=state.available,.state=states[state.transfer.control.state],
        .generation=state.transfer.control.generation,.accepted=s->accepted,.committed=s->committed,.expected=s->expected,
        .prepared=s->read_offset,.read_length=s->read_length,.ready=s->ready,.connected=s->connected,
        .uploading=s->upload!=0,.reading=s->reading,.recovering=s->recovering,
        .cleanup_required=state.transfer.control.cleanup_required,
        .error=transfer_result(state.transfer.control.error?state.transfer.control.error:s->error?s->error:s->fault?TIKU_MFS_IO:0)};
    return SURFACE_TRANSFER_OK;
}
/** @brief Admit a typed generation-checked command without progressing the service. */
static int transfer_submit(void *context,surface_transfer_command_t command,uint64_t generation,uint64_t *token)
{
    (void)context;if(!surface_os_in_owner())return SURFACE_TRANSFER_UNAVAILABLE;
    if(command!=SURFACE_TRANSFER_ENABLE&&command!=SURFACE_TRANSFER_STOP)return SURFACE_TRANSFER_INVALID;
    return transfer_result(tiku_mt_service_submit(command==SURFACE_TRANSFER_ENABLE?TIKU_MT_CONTROL_ENABLE:TIKU_MT_CONTROL_STOP,generation,token));
}
/** @brief Consume and normalize one terminal outcome while preserving pending tokens. */
static int transfer_take(void *context,uint64_t token,int *result)
{
    (void)context;if(!surface_os_in_owner())return SURFACE_TRANSFER_UNAVAILABLE;
    if(!result)return SURFACE_TRANSFER_INVALID;
    int outcome;int rc=tiku_mt_service_take(token,&outcome);if(!rc)*result=transfer_result(outcome);return transfer_result(rc);
}
surface_transfer_provider_t surface_os_transfers(void)
{ return (surface_transfer_provider_t){NULL,transfer_observe,transfer_submit,transfer_take}; }
#endif

void surface_os_services(char *out, size_t size)
{
    if (!out || !size) return;
    if (!surface_os_in_owner()) { out[0]=0; return; }
    tiku_wireless_observation_t radio={0};
    const char *wifi="unavailable";
    if (!tiku_wireless_observe(&radio) && radio.up) {
        static const char *const names[]={"idle","connecting","joined","failed"};
        if (radio.disconnect_pending) wifi="disconnecting";
        else if (radio.scan_in_progress) wifi="scanning";
        else if (radio.link_state<sizeof names/sizeof names[0]) wifi=names[radio.link_state];
    }
    tiku_media_transfer_status_t transfer;
    int rc=tiku_media_transfer_server_status(&transfer);
    if (rc || transfer.fault) snprintf(out,size,"Wi-Fi %s; transfer unavailable",wifi);
    else if (transfer.uploading) snprintf(out,size,"Wi-Fi %s; upload committed %llu/%llu",wifi,
        (unsigned long long)transfer.committed,(unsigned long long)transfer.expected);
    else if (transfer.reading) snprintf(out,size,"Wi-Fi %s; read prepared %llu/%llu",wifi,
        (unsigned long long)transfer.read_offset,(unsigned long long)transfer.read_length);
    else snprintf(out,size,"Wi-Fi %s; transfer %s",wifi,!transfer.active ? "stopped" :
        !transfer.port ? "console ready" : transfer.connected ? "connected" : "listening");
}
