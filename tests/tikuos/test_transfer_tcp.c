/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_transfer_tcp.c - transfer commands composed with the real TCP and media owners.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_TRANSFER_MAIN packet_fixture_suite
#include "test_media_transfer_async.c"
#include "transfer_tcp_fixture.h"
#define TIKU_KITS_NET_TCP_ENABLE 1
#define TIKU_KITS_NET_TCP_DEFERRED_ENABLE 1
#define TIKU_KITS_NET_TCP_MAX_CONNS 4
#define TIKU_KITS_NET_TCP_RX_BUF_SIZE 2048
#define TIKU_KITS_NET_TCP_MSS 536
#include <services/storage/tiku_media_transfer_control.c>
#include <services/storage/tiku_media_transfer_listener.c>
#include <services/storage/tiku_media_transfer_tcp.c>
static tiku_mt_tcp_t *tcp_service;
static uint64_t service_generation;
static uint8_t service_key[32];
static unsigned service_owner=1;
static uint32_t service_sequence;
/** @brief Expose the simulated kernel owner. */
static int service_in_owner(void *p){(void)p;return service_owner;}
/** @brief Retire requests through their active storage owner. */
static int service_cancel(void *p,tiku_media_request_t *r)
{
    (void)p;return transfer_store.running==4?tiku_media_mutation_cancel(&transfer_write,&transfer_store,r):
        tiku_media_async_cancel(&transfer_read,&transfer_store,r);
}
/** @brief Prove a service turn performs no synchronous disk or link output. */
static void service_tick(void)
{
    unsigned nr=reads,nw=writes,no=surface_tcp_fixture_outputs(),tc=transport.calls;
    assert(tiku_mt_tcp_step(tcp_service,100)>=0);
    assert(reads==nr&&writes==nw&&surface_tcp_fixture_outputs()==no&&transport.calls-tc<=1);
    packet_media_step();
}
/** @brief Complete and consume one command outcome while media work progresses separately. */
static void service_command(tiku_mt_control_command_t command)
{
    uint64_t token;assert(!tiku_mt_tcp_submit(tcp_service,command,service_generation,&token));int result;
    for(unsigned i=0;tiku_mt_tcp_take(tcp_service,token,&result);++i){assert(i<10000);service_tick();}
    assert(!result);
}
/** @brief Initialize exclusive caller-owned service storage and provision its endpoint. */
static void service_start(void)
{
    surface_tcp_fixture_reset();packet_reset(NULL);service_sequence=0;
    assert(tiku_mt_tcp_init(tcp_service,tiku_mt_tcp_size(),&transfer_store,77,service_in_owner,service_cancel,NULL)==tcp_service);
    assert(!tiku_mt_tcp_init(tcp_service,tiku_mt_tcp_size(),&transfer_store,77,service_in_owner,service_cancel,NULL));
    service_owner=0;tiku_mt_tcp_snapshot_t snapshot_state={.port=123};
    assert(tiku_mt_tcp_observe(tcp_service,&snapshot_state)==TIKU_MT_CONTROL_OWNER&&snapshot_state.port==123);
    uint32_t saved_now=tcp_service->now;
    assert(tiku_mt_tcp_step(tcp_service,100)==TIKU_MT_CONTROL_OWNER&&tcp_service->now==saved_now);service_owner=1;
    tiku_mt_control_profile_t p={.port=7777};memset(service_key,7,32);memcpy(p.key,service_key,32);memcpy(p.volume,uuid,16);
    assert(!tiku_mt_tcp_configure(tcp_service,&p,&service_generation));service_command(TIKU_MT_CONTROL_ENABLE);
    assert(tiku_mt_tcp_unbind(tcp_service)==TIKU_MFS_BUSY);
}
/** @brief Exchange independently framed requests through real TCP queues and acknowledgements. */
static size_t service_exchange(void *c,unsigned op,unsigned flags,uint64_t arg,const void *data,unsigned size,uint8_t *reply)
{
    uint8_t wire[1060];size_t n=frame(op,flags,++service_sequence,arg,data,size);
    put32(wire,(uint32_t)n);memcpy(wire+4,packet,n);surface_tcp_fixture_input(c,wire,n+4);size_t received=0;
    surface_tcp_fixture_zero_window(c);
    for(unsigned i=0;received<4||received<get32(reply)+4;++i){
        assert(i<10000);service_tick();received+=surface_tcp_fixture_output(c,reply+received,1060-received);
    }
    assert(received>=36&&!get32(reply+16)&&get32(reply+12)==service_sequence);return received;
}
/** @brief Stop established, rejected and half-open owned connections while preserving another listener. */
static void service_stop_connections(void)
{
    service_start();void *c=surface_tcp_fixture_connect(0,7777,1);
    uint8_t reply[1060];assert(service_exchange(c,TIKU_MT_HELLO,0,0,service_key,32,reply)>=36);
    (void)surface_tcp_fixture_connect(1,7777,0);(void)surface_tcp_fixture_connect(2,7777,1);
    (void)surface_tcp_fixture_other(3);assert(surface_tcp_fixture_active(2)==2);
    assert(tiku_mt_tcp_unbind(tcp_service)==TIKU_MFS_BUSY);
    service_command(TIKU_MT_CONTROL_STOP);
    for(unsigned i=0;i<3;++i)assert(!surface_tcp_fixture_active(i));
    assert(surface_tcp_fixture_active(3)==1);
    assert(!tiku_mt_tcp_forget(tcp_service)&&!tiku_mt_tcp_unbind(tcp_service));surface_tcp_fixture_finish();
}
/** @brief Preserve an unrelated occupant after link loss releases the accepted slot before its callback. */
static void service_slot_reuse(void)
{
    service_start();void *c=surface_tcp_fixture_connect(0,7777,1);uint8_t reply[1060];
    (void)service_exchange(c,TIKU_MT_HELLO,0,0,service_key,32,reply);
    surface_tcp_fixture_disconnect(c,1);assert(!surface_tcp_fixture_active(0));
    assert(surface_tcp_fixture_other(0)==c);
    service_command(TIKU_MT_CONTROL_STOP);assert(surface_tcp_fixture_active(0)==1);
    assert(!tiku_mt_tcp_forget(tcp_service)&&!tiku_mt_tcp_unbind(tcp_service));surface_tcp_fixture_finish();
}
/** @brief Reject a new owned connection reusing a disconnected slot before session cleanup. */
static void service_owned_reuse(void)
{
    service_start();void *c=surface_tcp_fixture_connect(0,7777,1);
    surface_tcp_fixture_disconnect(c,1);assert(!surface_tcp_fixture_active(0));
    assert(surface_tcp_fixture_connect(0,7777,1)==c&&surface_tcp_fixture_active(0)==2);
    service_command(TIKU_MT_CONTROL_STOP);assert(!surface_tcp_fixture_active(0));
    assert(!tiku_mt_tcp_forget(tcp_service)&&!tiku_mt_tcp_unbind(tcp_service));surface_tcp_fixture_finish();
}
/** @brief Compose maximum-sized transfer payloads with deferred TCP segmentation and media snapshots. */
static void service_file(void)
{
    service_start();for(unsigned i=0;i<1024;++i)source[i]=(uint8_t)(i*29);
    uint64_t id;assert(!tiku_mfs_begin(&fs,"big",TIKU_MFS_STREAM,0,NULL,&id));
    assert(!tiku_mfs_append(&fs,source,1024)&&!tiku_mfs_finalize(&fs));
    void *c=surface_tcp_fixture_connect(0,7777,1);uint8_t reply[1060];
    (void)service_exchange(c,TIKU_MT_HELLO,0,0,service_key,32,reply);
    (void)service_exchange(c,TIKU_MT_OPEN,0,0,"big",3,reply);
    uint8_t count[4];put32(count,1024);assert(service_exchange(c,TIKU_MT_READ,0,0,count,4,reply)==1060);
    assert(!memcmp(reply+36,source,1024));
    surface_tcp_fixture_disconnect(c,0);assert(surface_tcp_fixture_active(0)==2);
    tiku_mt_tcp_snapshot_t state;
    for(unsigned i=0;;++i){assert(i<5000);service_tick();assert(!tiku_mt_tcp_observe(tcp_service,&state));
        if(state.session.ready&&!state.session.connected)break;}
    assert(!surface_tcp_fixture_active(0));for(unsigned i=0;i<TIKU_MFS_READERS;++i)assert(!transfer_store.readers[i].token);
    service_command(TIKU_MT_CONTROL_STOP);assert(!tiku_mt_tcp_forget(tcp_service)&&!tiku_mt_tcp_unbind(tcp_service));surface_tcp_fixture_finish();
}
/** @brief Retain service buffers while a canceled media query closes its transport. */
static void service_read_cancel(void)
{
    service_start();(void)create_file("file");void *c=surface_tcp_fixture_connect(0,7777,1);uint8_t reply[1060];
    (void)service_exchange(c,TIKU_MT_HELLO,0,0,service_key,32,reply);
    size_t n=frame(TIKU_MT_OPEN,0,++service_sequence,0,"file",4);uint8_t wire[1060];
    put32(wire,(uint32_t)n);memcpy(wire+4,packet,n);surface_tcp_fixture_input(c,wire,n+4);
    for(unsigned i=0;!transfer_read.opened;++i){assert(i<2000);service_tick();}
    transport.close_busy=3;service_command(TIKU_MT_CONTROL_STOP);
    assert(!transport.open&&!transfer_store.running&&!transfer_store.count);
    assert(!tiku_mt_tcp_forget(tcp_service)&&!tiku_mt_tcp_unbind(tcp_service));surface_tcp_fixture_finish();
}
int main(void)
{
    (void)queue_drain;stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);
    void *memory=malloc(tiku_mfs_mutation_size());session=malloc(sizeof *session);packet_owner=malloc(tiku_media_transfer_async_size());
    tcp_service=malloc(tiku_mt_tcp_size());assert(stable&&cache&&snapshot&&memory&&session&&packet_owner&&tcp_service);
    mutation=tiku_mfs_mutation_init(memory,tiku_mfs_mutation_size());
    service_stop_connections();service_slot_reuse();service_owned_reuse();service_file();service_read_cancel();
    printf("TCP transfer service: commands, real framing, passive handshakes, slot reuse and snapshot reads passed (%zu bytes)\n",tiku_mt_tcp_size());
    free(tcp_service);free(packet_owner);free(session);free(memory);free(stable);free(cache);free(snapshot);return 0;
}
