/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_transfer_async.c - packet pacing, retry identity and queued transfer lifetime.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_STREAM_QUEUE_FIXTURE 1
#define TIKU_MEDIA_DIRECTORIES_ENABLE 1
#include "test_media_mutation_queue.c"
#include <services/storage/tiku_media_async.c>
#include <services/storage/tiku_media_transfer_async.c>
static tiku_media_store_t transfer_store;
static tiku_media_mutation_t transfer_write;
static tiku_media_async_t transfer_read;
static queue_transport_t transport;
static tiku_media_transfer_t *session;
static tiku_media_transfer_async_t *packet_owner;
static uint8_t packet[TIKU_MEDIA_TRANSFER_PACKET],source[131195];
static const tiku_media_async_ops_t read_ops={queue_acquire,queue_read,queue_poll,queue_close};
/** @brief Initialize a mounted fixture and an untouched staging reservation. */
static void packet_reset(const uint8_t *key)
{
    fresh();transfer_store=(tiku_media_store_t){.fs=&fs};memcpy(transfer_store.uuid,uuid,16);
    transport=(queue_transport_t){0};tiku_media_mutation_init(&transfer_write,mutation,&queue_ops,&transport);
    tiku_media_async_init(&transfer_read,&read_ops,&transport);memset(session,0xa5,sizeof *session);
    assert(tiku_media_transfer_async_init(packet_owner,tiku_media_transfer_async_size(),session,&transfer_store,77,key)==packet_owner);
    for(unsigned i=0;i<sizeof session->stage;++i)assert(session->stage[i]==0xa5);
}
/** @brief Independently frame one packet using the fixture's CRC routine. */
static size_t frame(unsigned op,unsigned flags,uint32_t sequence,uint64_t argument,const void *bytes,unsigned n)
{
    assert(n<=1024);memset(packet,0,32);memcpy(packet,"TMT1",4);packet[4]=(uint8_t)op;packet[5]=(uint8_t)flags;
    put32(packet+8,sequence);put64(packet+16,argument);put32(packet+24,n);if(n)memcpy(packet+32,bytes,n);
    uint8_t covered[1052];memcpy(covered,packet,28);if(n)memcpy(covered+28,bytes,n);put32(packet+28,crc(covered,28+n));return 32+n;
}
/** @brief Advance the media owner without invoking unsupported synchronous operations. */
static void packet_media_step(void)
{
    unsigned before=transport.calls;int rc;
    if(transfer_store.running==4)rc=tiku_media_mutation_step(&transfer_write,&transfer_store);
    else {rc=tiku_media_async_step(&transfer_read,&transfer_store);
        if(rc==2)rc=tiku_media_mutation_step(&transfer_write,&transfer_store);}
    assert(rc>=0&&rc!=2&&transport.calls-before<=1);
}
/** @brief Check protocol progression has no block access and respects copy/checksum work limits. */
static int packet_step(void)
{
    unsigned before_calls=transport.calls,before_reads=reads,before_writes=writes,phase=packet_owner->phase;
    size_t at=packet_owner->at;uint32_t copied=packet_owner->copy_at;
    int rc=tiku_media_transfer_async_step(packet_owner);
    assert(reads==before_reads&&writes==before_writes&&transport.calls==before_calls);
    if((phase==MT_CRC&&packet_owner->phase!=MT_DUPLICATE)||phase==MT_REPLY_CRC||phase==MT_DUPLICATE)
        assert(packet_owner->at>=at&&packet_owner->at-at<=64);
    if(phase==MT_CACHE)assert(packet_owner->at>=at&&packet_owner->at-at<=512);
    if(phase==MT_DATA_COPY||phase==MT_READ_COPY)assert(packet_owner->copy_at>=copied&&packet_owner->copy_at-copied<=512);
    return rc;
}
/** @brief Complete one admitted packet through separately scheduled protocol and media turns. */
static int packet_drain(void)
{
    for(unsigned n=0;packet_step();++n){assert(n<50000);packet_media_step();}
    const uint8_t *reply;int n=tiku_media_transfer_async_result(packet_owner,&reply);
    assert(n>=0&&n<=(int)TIKU_MEDIA_TRANSFER_PACKET);
    if(n){assert(n>=32&&get32(reply+24)==(unsigned)n-32);
        uint8_t covered[1052];memcpy(covered,reply,28);memcpy(covered+28,reply+32,(size_t)n-32);
        assert(crc(covered,(size_t)n-4)==get32(reply+28));}
    return n;
}
/** @brief Exchange one packet and prove its exact retry performs no storage work. */
static int packet_exchange(unsigned op,unsigned flags,uint64_t argument,const void *bytes,unsigned n)
{
    size_t size=frame(op,flags,session->last_sequence+1,argument,bytes,n);
    assert(!tiku_media_transfer_async_submit(packet_owner,packet,size));
    assert(tiku_media_transfer_async_submit(packet_owner,packet,size)==TIKU_MFS_BUSY);
    int length=packet_drain();assert(length>=32);uint8_t saved[1056];memcpy(saved,session->reply,(size_t)length);
    unsigned nr=reads,nw=writes;uint64_t accepted=session->accepted,sequence=fs.sequence;
    assert(!tiku_media_transfer_async_submit(packet_owner,packet,size)&&packet_drain()==length);
    assert(reads==nr&&writes==nw&&fs.sequence==sequence&&session->accepted==accepted&&!memcmp(saved,session->reply,(size_t)length));
    return (int32_t)get32(session->reply+12);
}
/** @brief Exercise malformed framing, authentication, sequence and conflicting retries. */
static void packet_validation(void)
{
    uint8_t key[32];memset(key,7,sizeof key);packet_reset(key);
    assert(packet_exchange(TIKU_MT_HELLO,0,0,NULL,0)==TIKU_MFS_ARG&&!session->authenticated);
    key[0]^=1;assert(packet_exchange(TIKU_MT_HELLO,0,0,key,32)==TIKU_MFS_ARG&&!session->authenticated);
    key[0]^=1;assert(!packet_exchange(TIKU_MT_HELLO,0,0,key,32)&&session->authenticated);
    uint32_t seq=session->last_sequence;size_t n=frame(TIKU_MT_LIST,0,seq+1,0,NULL,0);packet[28]^=1;
    assert(!tiku_media_transfer_async_submit(packet_owner,packet,n)&&!packet_drain()&&session->last_sequence==seq);
    n=frame(TIKU_MT_LIST,0,seq+2,0,NULL,0);assert(!tiku_media_transfer_async_submit(packet_owner,packet,n)&&!packet_drain());
    assert(!packet_exchange(TIKU_MT_LIST,0,0,NULL,0));seq=session->last_sequence;
    n=frame(TIKU_MT_NEXT,0,seq,0,NULL,0);assert(!tiku_media_transfer_async_submit(packet_owner,packet,n)&&!packet_drain());
    assert(packet_exchange(TIKU_MT_NEXT,0,0,NULL,0)==TIKU_MFS_NOENT);
    n=frame(TIKU_MT_LIST,0,session->last_sequence+1,0,NULL,0);reboot();
    assert(!tiku_media_transfer_async_submit(packet_owner,packet,n)&&!packet_drain()&&!session->authenticated);
}
/** @brief Stage source bytes through the next requested accepted boundary. */
static void packet_upload(unsigned limit)
{
    while(session->accepted<limit){unsigned n=limit-(unsigned)session->accepted;if(n>1024)n=1024;
        assert(!packet_exchange(TIKU_MT_DATA,0,session->accepted,source+session->accepted,n));}
}
/** @brief Upload and replace across durable prefixes while preserving snapshots and list epochs. */
static void packet_files(void)
{
    static const uint8_t digest[32]={0xac,0x0b,0x2c,0x98,0xc8,0x77,0x3a,0x3c,0x4b,0xeb,0xe9,0x9d,0x28,0x67,0x89,0x62,
        0xf6,0xa6,0xa7,0x0e,0x1f,0x71,0xcd,0xf2,0xff,0xf3,0x3d,0x83,0xb4,0x21,0xac,0xe7};
    packet_reset(NULL);(void)create_file("file");assert(!packet_exchange(TIKU_MT_HELLO,0,0,NULL,0));
    assert(!packet_exchange(TIKU_MT_OPEN,0,0,"file",4));
    uint8_t begin[36];memcpy(begin,digest,32);memcpy(begin+32,"file",4);
    assert(packet_exchange(TIKU_MT_BEGIN,0,sizeof source,begin,sizeof begin)==TIKU_MFS_BUSY);
    assert(!packet_exchange(TIKU_MT_LIST,0,0,NULL,0));uint64_t epoch=fs.names_epoch;
    assert(!packet_exchange(TIKU_MT_BEGIN,1,sizeof source,begin,sizeof begin));uint64_t id=session->upload;
    packet_upload(66536);assert(session->committed==65536&&session->staged==1000&&fs.names_epoch==epoch);
    assert(!packet_exchange(TIKU_MT_NEXT,0,0,NULL,0));
    uint8_t count[4];put32(count,3);assert(!packet_exchange(TIKU_MT_READ,0,0,count,4)&&!memcmp(session->reply+32,"abc",3));
    assert(!packet_exchange(TIKU_MT_RELEASE,0,0,NULL,0)&&!transfer_store.writer);
    uint8_t resume[44];put64(resume,sizeof source);memcpy(resume+8,begin,sizeof begin);resume[8]^=1;
    assert(packet_exchange(TIKU_MT_RESUME,0,id,resume,sizeof resume)==TIKU_MFS_STALE&&!transfer_store.writer);
    resume[8]^=1;assert(!packet_exchange(TIKU_MT_RESUME,0,id,resume,sizeof resume)&&session->accepted==65536);
    packet_upload(sizeof source);assert(!packet_exchange(TIKU_MT_FINISH,0,sizeof source,NULL,0)&&!transfer_store.writer);
    assert(packet_exchange(TIKU_MT_NEXT,0,0,NULL,0)==TIKU_MFS_STALE);
    assert(!packet_exchange(TIKU_MT_READ,0,0,count,4)&&!memcmp(session->reply+32,"abc",3));
    assert(!packet_exchange(TIKU_MT_CLOSE,0,0,NULL,0));assert(!packet_exchange(TIKU_MT_OPEN,0,0,"file",4));
    for(unsigned at=0;at<sizeof source;){
        unsigned take=sizeof source-at;if(take>1024)take=1024;put32(count,take);
        assert(!packet_exchange(TIKU_MT_READ,0,at,count,4));
        assert(get32(session->reply+24)==take&&!memcmp(session->reply+32,source+at,take));at+=take;
    }
    assert(!packet_exchange(TIKU_MT_CLOSE,0,0,NULL,0));
    assert(!packet_exchange(TIKU_MT_LIST,0,0,"fi",2));assert(!packet_exchange(TIKU_MT_NEXT,0,0,NULL,0));
    assert(get64(session->reply+32)==id&&!memcmp(session->reply+81,"file",4));
    assert(packet_exchange(TIKU_MT_NEXT,0,0,NULL,0)==TIKU_MFS_NOENT);
}
/** @brief Refuse namespace changes between lookup and checked upload/open admission. */
static void packet_identity(void)
{
    for(unsigned opening=0;opening<2;++opening){
        packet_reset(NULL);uint64_t id=create_file("file");assert(!packet_exchange(TIKU_MT_HELLO,0,0,NULL,0));
        uint8_t begin[36]={0};memcpy(begin+32,"file",4);
        size_t n=opening?frame(TIKU_MT_OPEN,0,session->last_sequence+1,0,"file",4):
            frame(TIKU_MT_BEGIN,1,session->last_sequence+1,9,begin,sizeof begin);
        assert(!tiku_media_transfer_async_submit(packet_owner,packet,n));
        unsigned target=opening?MT_OPEN_STAT:MT_BEGIN_STAT;
        for(unsigned i=0;packet_owner->phase!=target;++i){assert(i<1000);assert(packet_step());packet_media_step();}
        assert(!transfer_store.count&&!transfer_store.running);(void)create_file("neighbor");
        assert(packet_drain()>=32&&(int32_t)get32(session->reply+12)==TIKU_MFS_STALE);
        tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"file",&o)&&o.id==id&&!transfer_store.writer&&!session->reader);
    }
}
/** @brief Retry admission without consuming another client's full request queue. */
static void packet_queue_pressure(void)
{
    packet_reset(NULL);(void)create_file("file");assert(!packet_exchange(TIKU_MT_HELLO,0,0,NULL,0));
    tiku_media_request_t foreign[4];tiku_mfs_object_t info[4];
    for(unsigned i=0;i<4;++i){foreign[i]=(tiku_media_request_t){.op=TIKU_MEDIA_ENUMERATE,.client=100+i,
        .permissions=TIKU_MEDIA_READ,.name="",.info=&info[i]};assert(!tiku_media_store_submit(&transfer_store,&foreign[i]));}
    size_t n=frame(TIKU_MT_OPEN,0,session->last_sequence+1,0,"file",4);
    assert(!tiku_media_transfer_async_submit(packet_owner,packet,n));
    for(unsigned i=0;packet_owner->phase!=MT_SUBMIT;++i){assert(i<100);assert(packet_step());}
    for(unsigned i=0;i<5;++i){assert(packet_step()&&transfer_store.count==4&&packet_owner->phase==MT_SUBMIT);
        for(unsigned j=0;j<4;++j)assert(!foreign[j].done&&transfer_store.queue[j]==&foreign[j]);}
    assert(packet_drain()>=32&&!get32(session->reply+12)&&session->reader);
    for(unsigned i=0;i<4;++i)assert(foreign[i].done&&!foreign[i].result&&!strcmp(info[i].name,"file"));
    assert(!packet_exchange(TIKU_MT_HELLO,0,0,NULL,0)&&!session->reader);
}
/** @brief Preserve request and sector ownership until cancellation retires the media job. */
static void packet_cancellation(void)
{
    packet_reset(NULL);assert(!packet_exchange(TIKU_MT_HELLO,0,0,NULL,0));
    uint8_t begin[36]={0};memcpy(begin+32,"file",4);size_t n=frame(TIKU_MT_BEGIN,0,session->last_sequence+1,9,begin,sizeof begin);
    assert(!tiku_media_transfer_async_submit(packet_owner,packet,n));
    for(unsigned i=0;!transfer_read.opened;++i){assert(i<100);assert(packet_step());packet_media_step();}
    tiku_media_request_t *r=tiku_media_transfer_async_request(packet_owner);assert(r);
    assert(tiku_media_transfer_async_cancel(packet_owner)==TIKU_MFS_BUSY);
    transport.close_busy=2;
    assert(tiku_media_async_cancel(&transfer_read,&transfer_store,r)==TIKU_MFS_BUSY);
    assert(tiku_media_transfer_async_cancel(packet_owner)==TIKU_MFS_BUSY);
    assert(tiku_media_async_cancel(&transfer_read,&transfer_store,r)==TIKU_MFS_BUSY);
    assert(!tiku_media_async_cancel(&transfer_read,&transfer_store,r));
    assert(!tiku_media_transfer_async_cancel(packet_owner)&&!session->authenticated&&!session->last_size&&!transfer_store.count);
    assert(!packet_exchange(TIKU_MT_HELLO,0,0,NULL,0));
    assert(!packet_exchange(TIKU_MT_BEGIN,0,9,begin,sizeof begin));
    assert(!packet_exchange(TIKU_MT_DATA,0,0,"123456789",9));
    n=frame(TIKU_MT_FINISH,0,session->last_sequence+1,9,NULL,0);assert(!tiku_media_transfer_async_submit(packet_owner,packet,n));
    for(unsigned i=0;!(transfer_write.opened&&transport.kind==2);++i){assert(i<2000);assert(packet_step());packet_media_step();}
    r=tiku_media_transfer_async_request(packet_owner);assert(r&&tiku_media_transfer_async_cancel(packet_owner)==TIKU_MFS_BUSY);
    transport.close_busy=1;assert(tiku_media_mutation_cancel(&transfer_write,&transfer_store,r)==TIKU_MFS_BUSY);
    assert(!tiku_media_mutation_cancel(&transfer_write,&transfer_store,r));assert(!tiku_media_transfer_async_cancel(packet_owner));
    assert(transfer_store.failed&&transfer_store.writer==77&&!transfer_store.count);
    tiku_media_request_t release={.op=TIKU_MEDIA_RELEASE,.client=77,.permissions=TIKU_MEDIA_WRITE};
    assert(!tiku_media_store_submit(&transfer_store,&release));assert(tiku_media_mutation_step(&transfer_write,&transfer_store)==1);
    assert(release.done&&!release.result&&!transfer_store.writer&&!fs.writing&&fs.failed);
}

#ifndef SURFACE_TRANSFER_MAIN
#define SURFACE_TRANSFER_MAIN main
#endif
int SURFACE_TRANSFER_MAIN(void)
{
    (void)queue_drain;stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);
    void *memory=malloc(tiku_mfs_mutation_size());session=malloc(sizeof *session);packet_owner=malloc(tiku_media_transfer_async_size());
    assert(stable&&cache&&snapshot&&memory&&session&&packet_owner);mutation=tiku_mfs_mutation_init(memory,tiku_mfs_mutation_size());
    for(unsigned i=0;i<sizeof source;++i)source[i]=(uint8_t)(i*29+i/251);
    packet_validation();packet_files();packet_identity();packet_queue_pressure();packet_cancellation();
    printf("Transfer packets: paced framing, retries, resume, snapshots, epochs and cancellation passed (%zu bytes control)\n",tiku_media_transfer_async_size());
    free(packet_owner);free(session);free(memory);free(stable);free(cache);free(snapshot);return 0;
}
