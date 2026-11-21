/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_transfer_binding.c - credential-free service states and command-result translation.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "services.h"
#include <services/storage/tiku_media_transfer_service.h>
#include <services/storage/tiku_media_transfer_server.h>
#include <interfaces/wireless/tiku_wireless.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
static tiku_mt_service_snapshot_t state;
static int owner=1,api_result,terminal_result;
static unsigned calls;
static uint64_t submitted_generation;
static tiku_mt_control_command_t submitted_command;
int surface_os_in_owner(void){return owner;}
int tiku_wireless_observe(tiku_wireless_observation_t *out){(void)out;assert(0);return -1;}
int tiku_media_transfer_server_status(tiku_media_transfer_status_t *out){(void)out;assert(0);return -1;}
int tiku_mt_service_observe(tiku_mt_service_snapshot_t *out){++calls;if(api_result)return api_result;*out=state;return 0;}
int tiku_mt_service_submit(tiku_mt_control_command_t command,uint64_t generation,uint64_t *token)
{ ++calls;submitted_generation=generation;submitted_command=command;if(api_result)return api_result;*token=123;return 0; }
int tiku_mt_service_take(uint64_t token,int *out)
{ ++calls;assert(token==123);if(api_result)return api_result;*out=terminal_result;return 0; }
int main(void)
{
    surface_transfer_provider_t p=surface_os_transfers();surface_transfer_snapshot_t out={.generation=999};
    owner=0;uint64_t token=999;int result=999;
    assert(p.observe(p.context,&out)==SURFACE_TRANSFER_UNAVAILABLE&&out.generation==999);
    assert(p.submit(p.context,SURFACE_TRANSFER_ENABLE,7,&token)==SURFACE_TRANSFER_UNAVAILABLE&&token==999);
    assert(p.take(p.context,123,&result)==SURFACE_TRANSFER_UNAVAILABLE&&result==999&&!calls);owner=1;
    state.available=1;state.transfer.control.generation=0x100000007ULL;
    for(unsigned i=0;i<6;++i){state.transfer.control.state=(tiku_mt_control_state_t)i;assert(!p.observe(p.context,&out)&&out.state==i&&out.generation==state.transfer.control.generation);}
    state.transfer.control.state=TIKU_MT_CONTROL_READY;state.transfer.control.cleanup_required=1;
    state.transfer.session=(tiku_mt_listener_snapshot_t){.ready=1,.connected=1,.upload=8,.reading=1,.accepted=0x200000009ULL,
        .committed=0x200000000ULL,.expected=0x300000000ULL,.read_offset=0x400000000ULL,.read_length=0x500000000ULL};
    assert(!p.observe(p.context,&out)&&out.accepted==state.transfer.session.accepted&&out.committed==state.transfer.session.committed);
    assert(out.expected==state.transfer.session.expected&&out.prepared==state.transfer.session.read_offset&&out.read_length==state.transfer.session.read_length);
    assert(out.uploading&&out.reading&&out.connected&&out.ready&&out.cleanup_required);
    state.transfer.session.ready=0;state.transfer.session.recovering=1;assert(!p.observe(p.context,&out)&&out.recovering&&!out.ready);
    state.transfer.session.fault=1;assert(!p.observe(p.context,&out)&&out.error==SURFACE_TRANSFER_FAILED);
    assert(!p.submit(p.context,SURFACE_TRANSFER_STOP,0x100000007ULL,&token)&&token==123&&submitted_command==TIKU_MT_CONTROL_STOP&&submitted_generation==0x100000007ULL);
    assert(!p.submit(p.context,SURFACE_TRANSFER_ENABLE,7,&token)&&submitted_command==TIKU_MT_CONTROL_ENABLE);
    static const int native[]={TIKU_MT_CONTROL_PENDING,TIKU_MFS_BUSY,TIKU_MFS_STALE,TIKU_MFS_ARG,TIKU_MT_CONTROL_CANCELLED,TIKU_MFS_NOFS,TIKU_MFS_IO};
    static const int mapped[]={SURFACE_TRANSFER_PENDING,SURFACE_TRANSFER_BUSY,SURFACE_TRANSFER_STALE,SURFACE_TRANSFER_INVALID,SURFACE_TRANSFER_CANCELLED,SURFACE_TRANSFER_UNAVAILABLE,SURFACE_TRANSFER_FAILED};
    for(unsigned i=0;i<sizeof native/sizeof native[0];++i){
        api_result=native[i];result=999;assert(p.take(p.context,123,&result)==mapped[i]&&result==999);
        api_result=0;terminal_result=native[i];assert(!p.take(p.context,123,&result)&&result==mapped[i]);
    }
    unsigned before=calls;assert(p.submit(p.context,(surface_transfer_command_t)99,7,&token)==SURFACE_TRANSFER_INVALID&&calls==before);
    assert(p.take(p.context,123,NULL)==SURFACE_TRANSFER_INVALID&&calls==before);
    puts("Transfer binding: owner checks, typed commands, 64-bit progress and retained outcomes passed");return 0;
}
