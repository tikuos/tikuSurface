/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_transfer_listener.c - framed streams, service cleanup and shared media ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_TRANSFER_MAIN packet_fixture_suite
#include "test_media_transfer_async.c"
#include <services/storage/tiku_media_transfer_control.c>
#include <services/storage/tiku_media_transfer_listener.c>
/** @brief Stream transport retains reservations through delayed disconnect cleanup. */
typedef struct {
    unsigned owner,attached,calls,read_limit,send_limit,send_busy,unlisten_busy,retiring,cancel_calls;
    int listen_error,retire_error;
    uint8_t input[1060],output[1060];size_t in_size,in_at,out_size;
    unsigned connection;
} listener_transport_t;
static listener_transport_t net;
static tiku_mt_listener_t *listener;
static tiku_mt_control_t *controller;
static tiku_mt_control_profile_t profile;
static uint64_t generation;
static uint32_t now_ms,sequence;
/** @brief Expose a simulated serialized owner. */
static int ls_owner(void *p){return ((listener_transport_t *)p)->owner;}
/** @brief Reserve the endpoint or refuse without changing state. */
static int ls_listen(void *p,uint16_t port)
{
    listener_transport_t *n=p;++n->calls;assert(port==7777&&!n->attached);
    if(n->listen_error)return n->listen_error;
    n->attached=1;return 0;
}
/** @brief Retry endpoint removal while retaining its reservation. */
static int ls_unlisten(void *p,uint16_t port)
{
    listener_transport_t *n=p;++n->calls;assert(port==7777&&n->attached);
    if(n->unlisten_busy){--n->unlisten_busy;return TIKU_MFS_BUSY;}
    n->attached=0;return 0;
}
/** @brief Deliver a bounded fragment without advancing storage or link output. */
static int ls_read(void *p,void *connection,void *data,size_t cap)
{
    listener_transport_t *n=p;++n->calls;assert(connection==&n->connection&&cap&&cap<=512);
    size_t take=n->in_size-n->in_at;if(take>cap)take=cap;if(take>n->read_limit)take=n->read_limit;
    memcpy(data,n->input+n->in_at,take);n->in_at+=take;return (int)take;
}
/** @brief Copy accepted bytes before returning, with partial acceptance and backpressure. */
static int ls_send(void *p,void *connection,const void *data,size_t size)
{
    listener_transport_t *n=p;++n->calls;assert(connection==&n->connection&&size&&size<=512);
    if(n->send_busy){--n->send_busy;return TIKU_MFS_BUSY;}
    if(size>n->send_limit)size=n->send_limit;
    assert(n->out_size+size<=sizeof n->output);
    memcpy(n->output+n->out_size,data,size);n->out_size+=size;return (int)size;
}
/** @brief Retain a connection across bounded retirement turns. */
static int ls_retire(void *p,void *connection)
{
    listener_transport_t *n=p;++n->calls;assert(connection==&n->connection);
    if(n->retire_error)return n->retire_error;
    if(n->retiring){--n->retiring;return 1;}return 0;
}
/** @brief Cancel through the matching owner without progressing unrelated requests. */
static int ls_cancel(void *p,tiku_media_request_t *r)
{
    listener_transport_t *n=p;++n->calls;++n->cancel_calls;assert(r->client==77);
    return transfer_store.running==4?tiku_media_mutation_cancel(&transfer_write,&transfer_store,r):
        tiku_media_async_cancel(&transfer_read,&transfer_store,r);
}
static const tiku_mt_listener_ops_t ls_ops={ls_owner,ls_listen,ls_unlisten,ls_read,ls_send,ls_retire,ls_cancel};
/** @brief Initialize the service on an isolated mounted disk and provision a nonzero key. */
static void ls_reset(void)
{
    packet_reset(NULL);net=(listener_transport_t){.owner=1,.read_limit=511,.send_limit=127};
    memset(listener,0xa5,tiku_mt_listener_size());
    assert(tiku_mt_listener_init(listener,tiku_mt_listener_size(),&transfer_store,77,ls_ops,&net)==listener);
    for(size_t i=0;i<sizeof listener->work;++i)assert(((uint8_t *)&listener->work)[i]==0xa5);
    tiku_mt_control_ops_t ops={tiku_mt_listener_in_owner,tiku_mt_listener_phase};
    assert(tiku_mt_control_init(controller,tiku_mt_control_size(),ops,listener)==controller);
    profile=(tiku_mt_control_profile_t){.port=7777};memset(profile.key,7,32);memcpy(profile.volume,uuid,16);
    assert(!tiku_mt_control_configure(controller,&profile,&generation));now_ms=10;sequence=0;
}
/** @brief Advance controller and listener separately and check their callback and I/O bounds. */
static void ls_tick(void)
{
    unsigned nr=reads,nw=writes,nc=net.calls,tc=transport.calls;
    size_t preparing=listener->preparing,wiping=listener->wiping;
    assert(tiku_mt_control_step(controller)>=0);assert(net.calls-nc<=1&&transport.calls-tc<=1);
    assert(listener->preparing<=preparing+512&&listener->wiping<=wiping+512);
    unsigned stream=listener->stream;size_t copied=listener->copied;
    nc=net.calls;tc=transport.calls;assert(tiku_mt_listener_step(listener,now_ms)>=0);
    if(stream==LS_REPLY)assert(listener->copied<=copied+512);
    assert(net.calls-nc<=1&&transport.calls-tc<=1&&reads==nr&&writes==nw);
    packet_media_step();
}
/** @brief Complete an admitted controller command and consume its retained outcome. */
static int ls_command(tiku_mt_control_command_t command)
{
    uint64_t token;assert(!tiku_mt_control_submit(controller,command,generation,&token));
    int result=999;for(unsigned i=0;tiku_mt_control_take(controller,token,&result);++i){assert(i<50000);ls_tick();}
    return result;
}
/** @brief Start the listener and hand it one exclusively reserved connection. */
static void ls_start(void)
{
    assert(!ls_command(TIKU_MT_CONTROL_ENABLE)&&net.attached);
    assert(!tiku_mt_listener_connected(listener,&net.connection,now_ms));
    unsigned rejected;assert(tiku_mt_listener_connected(listener,&rejected,now_ms)==TIKU_MFS_BUSY);
}
/** @brief Supply a complete framed request while preserving reply backpressure. */
static void ls_submit(unsigned op,unsigned flags,uint64_t arg,const void *data,unsigned size)
{
    assert(net.in_at==net.in_size);size_t n=frame(op,flags,++sequence,arg,data,size);
    put32(net.input,(uint32_t)n);memcpy(net.input+4,packet,n);net.in_size=n+4;net.in_at=net.out_size=0;
}
/** @brief Check the independently decoded response and its checksum. */
static int ls_exchange(unsigned op,unsigned flags,uint64_t arg,const void *data,unsigned size)
{
    ls_submit(op,flags,arg,data,size);
    for(unsigned i=0;net.in_at<net.in_size||listener->stream!=LS_PREFIX;++i){assert(i<50000);ls_tick();}
    unsigned n=get32(net.output);assert(n>=32&&n+4==net.out_size&&get32(net.output+12)==sequence);
    uint8_t covered[1052];memcpy(covered,net.output+4,28);memcpy(covered+28,net.output+36,n-32);
    assert(crc(covered,n-4)==get32(net.output+32));return (int32_t)get32(net.output+16);
}
/** @brief Read an old snapshot while publishing a checked replacement over fragmented streams. */
static void ls_files(void)
{
    static const uint8_t digest[32]={0x15,0xe2,0xb0,0xd3,0xc3,0x38,0x91,0xeb,0xb0,0xf1,0xef,0x60,0x9e,0xc4,0x19,0x42,
        0x0c,0x20,0xe3,0x20,0xce,0x94,0xc6,0x5f,0xbc,0x8c,0x33,0x12,0x44,0x8e,0xb2,0x25};
    ls_reset();(void)create_file("file");ls_start();net.read_limit=1;net.send_busy=5;
    assert(!ls_exchange(TIKU_MT_HELLO,0,0,profile.key,32));net.read_limit=511;
    assert(!ls_exchange(TIKU_MT_OPEN,0,0,"file",4));
    tiku_mt_listener_snapshot_t status;assert(!tiku_mt_listener_observe(listener,&status));
    assert(status.reading&&status.read_length==3&&!status.fault);
    uint8_t begin[36];memcpy(begin,digest,32);memcpy(begin+32,"file",4);
    assert(!ls_exchange(TIKU_MT_BEGIN,1,9,begin,sizeof begin));
    assert(!ls_exchange(TIKU_MT_DATA,0,0,"123456789",9));
    assert(!tiku_mt_listener_observe(listener,&status));
    assert(status.expected==9&&status.accepted==9&&!status.committed);
    assert(!ls_exchange(TIKU_MT_FINISH,0,9,NULL,0));
    uint8_t count[4];put32(count,3);assert(!ls_exchange(TIKU_MT_READ,0,0,count,4)&&!memcmp(net.output+36,"abc",3));
    assert(!ls_exchange(TIKU_MT_CLOSE,0,0,NULL,0));assert(!ls_exchange(TIKU_MT_OPEN,0,0,"file",4));
    put32(count,9);assert(!ls_exchange(TIKU_MT_READ,0,0,count,4)&&!memcmp(net.output+36,"123456789",9));
    net.retiring=3;net.unlisten_busy=2;assert(!ls_command(TIKU_MT_CONTROL_STOP));
    assert(!net.attached&&!listener->connection&&!transfer_store.writer&&!net.retiring);
    for(size_t i=0;i<sizeof listener->work;++i)assert(!((uint8_t *)&listener->work)[i]);
    for(unsigned i=0;i<TIKU_MFS_READERS;++i)assert(!transfer_store.readers[i].token);
}
/** @brief Stop during retained read transport and preserve another client's queued request. */
static void ls_cancellation(void)
{
    ls_reset();(void)create_file("file");ls_start();assert(!ls_exchange(TIKU_MT_HELLO,0,0,profile.key,32));
    ls_submit(TIKU_MT_OPEN,0,0,"file",4);
    for(unsigned i=0;!transfer_read.opened;++i){assert(i<2000);ls_tick();}
    tiku_media_request_t *borrowed=tiku_media_transfer_async_request(listener->packet);assert(borrowed&&!borrowed->done);
    tiku_mfs_object_t info;tiku_media_request_t foreign={.op=TIKU_MEDIA_ENUMERATE,.client=88,.permissions=TIKU_MEDIA_READ,.name="",.info=&info};
    assert(!tiku_media_store_submit(&transfer_store,&foreign));transport.close_busy=3;net.retiring=2;
    uint64_t stop;assert(!tiku_mt_control_submit(controller,TIKU_MT_CONTROL_STOP,generation,&stop));
    for(unsigned i=0;i<4;++i){unsigned calls=net.calls;assert(tiku_mt_control_step(controller)==1);assert(net.calls-calls<=1);
        assert(!tiku_mt_listener_step(listener,now_ms)&&!foreign.done);}
    assert(!foreign.done&&!borrowed->done&&net.cancel_calls==3);
    int result;for(unsigned i=0;tiku_mt_control_take(controller,stop,&result);++i){assert(i<2000);ls_tick();}
    assert(!result&&foreign.done&&!foreign.result&&!strcmp(info.name,"file")&&!transport.open);
}
/** @brief Relisten after disconnect, reject malformed lengths, and preempt recovery with Stop. */
static void ls_recovery(void)
{
    ls_reset();ls_start();assert(!ls_exchange(TIKU_MT_HELLO,0,0,profile.key,32));
    net.retiring=4;assert(!tiku_mt_listener_closed(listener,&net.connection));
    for(unsigned i=0;!listener->ready;++i){assert(i<2000);ls_tick();}
    assert(!listener->connection&&net.attached&&!listener->work.session.authenticated);
    assert(!tiku_mt_listener_connected(listener,&net.connection,now_ms));
    assert(!tiku_mt_listener_closed(listener,&net.connection));
    assert(!ls_command(TIKU_MT_CONTROL_STOP));ls_start();
    put32(net.input,1057);net.in_at=0;net.in_size=4;
    for(unsigned i=0;!listener->recovery;++i){assert(i<10);ls_tick();}
    assert(!ls_command(TIKU_MT_CONTROL_STOP)&&!listener->recovery&&!net.attached);
    for(unsigned i=0;i<10;++i)assert(!tiku_mt_listener_step(listener,now_ms));
    ls_start();now_ms+=120000;ls_tick();assert(listener->recovery&&!listener->ready);
    assert(!ls_command(TIKU_MT_CONTROL_STOP));
}
/** @brief Reject owner/volume misuse and retain failed cleanup resources for a later Stop. */
static void ls_errors(void)
{
    ls_reset();net.owner=0;tiku_mt_listener_snapshot_t state={.error=999};
    assert(tiku_mt_listener_observe(listener,&state)==TIKU_MT_CONTROL_OWNER&&state.error==999);
    assert(tiku_mt_listener_step(listener,now_ms)==TIKU_MT_CONTROL_OWNER);net.owner=1;
    transfer_store.uuid[0]^=1;uint64_t wrong_volume;
    assert(!tiku_mt_control_submit(controller,TIKU_MT_CONTROL_ENABLE,generation,&wrong_volume));
    assert(tiku_mt_control_step(controller)==1&&!listener->preparing&&!listener->packet&&!net.calls);
    int rejected;for(unsigned i=0;tiku_mt_control_take(controller,wrong_volume,&rejected);++i){assert(i<2000);ls_tick();}
    assert(rejected==TIKU_MFS_STALE&&!net.attached);
    transfer_store.uuid[0]^=1;net.listen_error=TIKU_MFS_BUSY;
    uint64_t enable;assert(!tiku_mt_control_submit(controller,TIKU_MT_CONTROL_ENABLE,generation,&enable));
    for(unsigned i=0;i<300;++i)ls_tick();
    assert(!ls_command(TIKU_MT_CONTROL_STOP));
    int result;assert(!tiku_mt_control_take(controller,enable,&result)&&result==TIKU_MT_CONTROL_CANCELLED);
    net.listen_error=0;ls_start();net.retire_error=TIKU_MFS_IO;
    assert(ls_command(TIKU_MT_CONTROL_STOP)==TIKU_MFS_IO&&listener->connection&&!listener->ready);
    net.retire_error=0;assert(!ls_command(TIKU_MT_CONTROL_STOP)&&!listener->connection);
}
/** @brief Exercise maximum packet payloads and reject a changed mounted identity. */
static void ls_payload(void)
{
    ls_reset();for(unsigned i=0;i<1024;++i)source[i]=(uint8_t)i;
    uint64_t id;assert(!tiku_mfs_begin(&fs,"big",TIKU_MFS_STREAM,0,NULL,&id));
    assert(!tiku_mfs_append(&fs,source,1024)&&!tiku_mfs_finalize(&fs));
    ls_start();net.read_limit=net.send_limit=512;
    assert(!ls_exchange(TIKU_MT_HELLO,0,0,profile.key,32));assert(!ls_exchange(TIKU_MT_OPEN,0,0,"big",3));
    uint8_t count[4];put32(count,1024);assert(!ls_exchange(TIKU_MT_READ,0,0,count,4));
    assert(!memcmp(net.output+36,source,1024));
    uint8_t begin[37]={0};memcpy(begin+32,"draft",5);
    assert(!ls_exchange(TIKU_MT_BEGIN,1,1024,begin,sizeof begin));
    assert(!ls_exchange(TIKU_MT_DATA,0,0,source,1024));
    tiku_mt_listener_snapshot_t status;assert(!tiku_mt_listener_observe(listener,&status));
    assert(status.accepted==1024&&status.committed==0&&status.upload);
    assert(!ls_command(TIKU_MT_CONTROL_STOP)&&!transfer_store.writer);
    ls_start();transfer_store.uuid[0]^=1;
    assert(tiku_mt_listener_step(listener,now_ms)==1&&!listener->ready);
    assert(!ls_command(TIKU_MT_CONTROL_STOP));
}
/** @brief Stop a pending finalization without erasing a borrowed mutation buffer. */
static void ls_write_cancel(void)
{
    ls_reset();ls_start();assert(!ls_exchange(TIKU_MT_HELLO,0,0,profile.key,32));
    uint8_t begin[36]={0};memcpy(begin+32,"file",4);
    assert(!ls_exchange(TIKU_MT_BEGIN,1,9,begin,sizeof begin));
    assert(!ls_exchange(TIKU_MT_DATA,0,0,"123456789",9));
    ls_submit(TIKU_MT_FINISH,0,9,NULL,0);
    for(unsigned i=0;!(transfer_write.opened&&transport.kind==2);++i){assert(i<5000);ls_tick();}
    transport.close_busy=2;void *buffer=transport.buffer;uint8_t saved[512];memcpy(saved,buffer,512);
    uint64_t token;assert(!tiku_mt_control_submit(controller,TIKU_MT_CONTROL_STOP,generation,&token));
    for(unsigned i=0;i<3;++i){assert(tiku_mt_control_step(controller)==1);assert(transport.buffer==buffer&&!memcmp(buffer,saved,512));}
    int result;for(unsigned i=0;tiku_mt_control_take(controller,token,&result);++i){assert(i<5000);ls_tick();}
    assert(!result&&!transfer_store.writer&&transfer_store.failed&&!transport.buffer&&!transport.open);
    for(size_t i=0;i<sizeof listener->work;++i)assert(!((uint8_t *)&listener->work)[i]);
}
int main(void)
{
    (void)queue_drain;stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);
    void *memory=malloc(tiku_mfs_mutation_size());session=malloc(sizeof *session);packet_owner=malloc(tiku_media_transfer_async_size());
    listener=malloc(tiku_mt_listener_size());controller=malloc(tiku_mt_control_size());
    assert(stable&&cache&&snapshot&&memory&&session&&packet_owner&&listener&&controller);
    mutation=tiku_mfs_mutation_init(memory,tiku_mfs_mutation_size());
    ls_files();ls_cancellation();ls_recovery();ls_errors();ls_payload();ls_write_cancel();
    printf("Transfer listener: framing, backpressure, atomic replacement, cleanup and recovery passed (%zu bytes)\n",tiku_mt_listener_size());
    free(controller);free(listener);free(packet_owner);free(session);free(memory);free(stable);free(cache);free(snapshot);return 0;
}
