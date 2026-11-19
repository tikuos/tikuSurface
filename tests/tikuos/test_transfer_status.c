/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_transfer_status.c - typed observations over the existing transfer-server fixture.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main transfer_server_suite
#include <TikuBench/tests/host/kernel/test_media_transfer_server.c>
#undef main
#include <assert.h>

/** @brief Check empty, connected, active, faulted and invalidated snapshots without mutations. */
int main(void)
{
    assert(!transfer_server_suite());
    tiku_media_transfer_status_t status,empty={0};
    assert(tiku_media_transfer_server_status(NULL)==TIKU_MFS_ARG);
    memset(&status,0xa5,sizeof status);
    assert(!tiku_media_transfer_server_status(&status) && !memcmp(&status,&empty,sizeof status));
    uint8_t key[32]={7};assert(!reset(key));
    assert(!tiku_media_transfer_server_start(9000,key));
    assert(!tiku_media_transfer_server_status(&status) && status.active && status.port==9000 && !status.connected);
    event(&peer,TIKU_KITS_NET_TCP_EVT_CONNECTED);
    memory->session.upload=9;memory->session.reader=11;
    memory->session.accepted=0x100000020ULL;memory->session.committed=0x100000000ULL;
    memory->session.expected=0x200000000ULL;memory->session.read_offset=0x300000000ULL;
    memory->session.read_length=0x400000000ULL;
    transfer_memory_t copy=*memory;
    unsigned before_reads=reads,before_writes=writes;
    for(unsigned i=0;i<10;i++) {
        assert(!tiku_media_transfer_server_status(&status));
        assert(status.active && status.connected && status.uploading && status.reading && !status.fault);
        assert(status.accepted==0x100000020ULL && status.committed==0x100000000ULL);
        assert(status.expected==0x200000000ULL && status.read_offset==0x300000000ULL && status.read_length==0x400000000ULL);
        assert(!memcmp(&copy,memory,sizeof copy) && reads==before_reads && writes==before_writes);
    }
    lost=1;assert(!tiku_media_transfer_server_status(&status) && status.fault);lost=0;
    memory->session.fault=1;assert(!tiku_media_transfer_server_status(&status) && status.fault);
    valid=0;
    assert(tiku_media_transfer_server_status(&status)==TIKU_MFS_STALE && !memcmp(&status,&empty,sizeof status));
    valid=1;assert(!tiku_media_transfer_server_stop());
    assert(!tiku_media_transfer_server_status(&status) && !status.active);
    puts("Transfer status: listener, peer, 64-bit progress, faults and read-only snapshots pass");
    return 0;
}
