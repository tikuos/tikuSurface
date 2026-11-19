/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_transfer_browser.c - browser pages alongside resumable transfers.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main media_fixture_suite_main
#include "test_media.c"
#undef main
#include <services/storage/tiku_media_transfer.h>

/** @brief Encode the low bytes of an unsigned protocol field. */
static void encode(uint8_t *out,uint64_t value,unsigned n)
{ for(unsigned i=0;i<n;i++)out[i]=(uint8_t)(value>>(i*8)); }

/** @brief Decode an unsigned protocol field without alignment assumptions. */
static uint64_t decode(const uint8_t *in,unsigned n)
{ uint64_t value=0;for(unsigned i=0;i<n;i++)value|=(uint64_t)in[i]<<(i*8);return value; }

/** @brief Execute an independently framed packet and check exact-retry suppression. */
static int exchange(tiku_media_transfer_t *session,unsigned op,unsigned flags,
                    uint64_t argument,const void *data,unsigned n)
{
    uint8_t packet[TIKU_MEDIA_TRANSFER_PACKET]={0},covered[28+TIKU_MEDIA_TRANSFER_CHUNK];
    assert(n<=TIKU_MEDIA_TRANSFER_CHUNK);
    memcpy(packet,"TMT1",4);packet[4]=(uint8_t)op;packet[5]=(uint8_t)flags;
    encode(packet+8,session->last_sequence+1,4);encode(packet+16,argument,8);encode(packet+24,n,4);
    if(n)memcpy(packet+32,data,n);
    memcpy(covered,packet,28);if(n)memcpy(covered+28,data,n);
    encode(packet+28,fixture_crc(covered,28+n),4);
    size_t reply=tiku_media_transfer_handle(session,packet,32+n);
    assert(reply>=32&&reply<=TIKU_MEDIA_TRANSFER_PACKET);
    uint8_t saved[TIKU_MEDIA_TRANSFER_PACKET];memcpy(saved,session->reply,reply);
    unsigned before=reads;uint64_t accepted=session->accepted,committed=session->committed;
    assert(tiku_media_transfer_handle(session,packet,32+n)==reply);
    assert(!memcmp(saved,session->reply,reply)&&reads==before);
    assert(session->accepted==accepted&&session->committed==committed);
    return (int32_t)decode(session->reply+12,4);
}

/** @brief Send one chunk from the deterministic replacement payload. */
static int chunk(tiku_media_transfer_t *session,const uint8_t *data,uint64_t limit)
{
    unsigned n=(unsigned)(limit-session->accepted);if(n>1024)n=1024;
    assert(n);return exchange(session,TIKU_MT_DATA,0,session->accepted,data+session->accepted,n);
}

/** @brief Read a complete remote snapshot and compare bytes with the independent source. */
static void download(tiku_media_transfer_t *session,const uint8_t *expected,unsigned length)
{
    for(unsigned at=0;at<length;){
        uint8_t count[4];unsigned n=length-at;if(n>1024)n=1024;encode(count,n,4);
        assert(!exchange(session,TIKU_MT_READ,0,at,count,sizeof count));
        assert(decode(session->reply+24,4)==n&&!memcmp(session->reply+32,expected+at,n));at+=n;
    }
}

/** @brief Preserve browser cursors and old readers across private upload progress and publication. */
int main(void)
{
    static const uint8_t digest[32]={0xac,0x0b,0x2c,0x98,0xc8,0x77,0x3a,0x3c,
        0x4b,0xeb,0xe9,0x9d,0x28,0x67,0x89,0x62,0xf6,0xa6,0xa7,0x0e,0x1f,0x71,
        0xcd,0xf2,0xff,0xf3,0x3d,0x83,0xb4,0x21,0xac,0xe7};
    const unsigned length=131195;
    uint8_t *data=malloc(length),old[2000];assert(data);
    for(unsigned i=0;i<length;i++)data[i]=(uint8_t)(i*29+i/251);
    for(unsigned i=0;i<sizeof old;i++)old[i]=(uint8_t)(i*7+i/251);
    disk=calloc(1,DISK_BYTES);assert(disk);
    block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();
    char name[64];for(unsigned i=0;i<40;i++){snprintf(name,sizeof name,"f%04u",i);file(name);}
    uint64_t id;assert(!tiku_mfs_begin(&fs,"shared.bin",TIKU_MFS_STREAM,0,NULL,&id));
    assert(!tiku_mfs_append(&fs,old,sizeof old)&&!tiku_mfs_finalize(&fs));
    tiku_media_transfer_t *upload=calloc(1,sizeof *upload),*reader=calloc(1,sizeof *reader);
    assert(upload&&reader);tiku_media_transfer_init(upload,&store,77,NULL);tiku_media_transfer_init(reader,&store,78,NULL);
    assert(!exchange(upload,TIKU_MT_HELLO,0,0,NULL,0)&&!exchange(reader,TIKU_MT_HELLO,0,0,NULL,0));
    assert(!exchange(reader,TIKU_MT_OPEN,0,0,"shared.bin",10));
    uint8_t begin[42];memcpy(begin,digest,32);memcpy(begin+32,"shared.bin",10);
    uint64_t version=tiku_mfs_names_epoch(&fs);
    assert(!exchange(upload,TIKU_MT_BEGIN,1,length,begin,sizeof begin));id=upload->upload;
    assert(tiku_mfs_names_epoch(&fs)==version);
    surface_host_t host;surface_config_t config=surface_host_config(&host);
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    surface_media_t media;tiku_storage_provider_t provider=surface_media_init(&media,bridge);
    tiku_resource_caller_t caller={0};strcpy(caller.principal,"surface-local");
    tiku_paged_storage_t *owner=tiku_paged_storage_create(config.allocator,provider,&caller,16,1024);assert(owner);
    assert(tiku_paged_storage_list(owner,"/",0,0)==TIKU_RESOURCE_PENDING);
    for(unsigned turns=0;tiku_paged_storage_pending(owner);turns++){
        assert(turns<30000);
        if(upload->accepted<64512)assert(!chunk(upload,data,64512));
        surface_media_poll(&media);
    }
    size_t count;uint64_t epoch;int eof;
    const tiku_storage_request_t *page=tiku_paged_storage_result(owner,&count,&epoch,&eof);
    assert(page&&count==16&&!eof&&epoch==version);
    for(unsigned i=0;i<16;i++){snprintf(name,sizeof name,"/f%04u",i);assert(!strcmp(page->entries[i].path,name));}
    uint64_t after=page->entries[15].node_id;
    while(upload->accepted<64512)assert(!chunk(upload,data,64512));
    assert(tiku_paged_storage_list(owner,"/",epoch,after)==TIKU_RESOURCE_PENDING);
    for(unsigned turns=0;!store.running;turns++){assert(turns<30000);surface_media_poll(&media);}
    assert(store.running==2);
    assert(chunk(upload,data,65536)==TIKU_MFS_BUSY&&upload->fault&&upload->committed==0);
    assert(store.running==2&&tiku_paged_storage_pending(owner));
    drain(&media,owner,0);
    page=tiku_paged_storage_result(owner,&count,&epoch,&eof);
    assert(page&&count==16&&!eof&&!strcmp(page->entries[0].path,"/f0016"));
    after=page->entries[15].node_id;
    assert(!exchange(upload,TIKU_MT_RELEASE,0,0,NULL,0));
    uint8_t resume[50];encode(resume,length,8);memcpy(resume+8,begin,sizeof begin);
    assert(!exchange(upload,TIKU_MT_RESUME,0,id,resume,sizeof resume)&&!upload->accepted&&!upload->fault);
    while(upload->accepted<65536)assert(!chunk(upload,data,65536));
    assert(upload->committed==65536&&tiku_mfs_names_epoch(&fs)==version);
    assert(tiku_paged_storage_list(owner,"/",epoch,after)==TIKU_RESOURCE_PENDING);
    unsigned interleaved=0;
    for(unsigned turns=0;tiku_paged_storage_pending(owner);turns++){
        assert(turns<30000);
        if(!store.running&&!store.count&&upload->accepted<length){assert(!chunk(upload,data,length));interleaved++;}
        surface_media_poll(&media);
    }
    page=tiku_paged_storage_result(owner,&count,&epoch,&eof);
    assert(page&&count==9&&eof&&!strcmp(page->entries[8].path,"/shared.bin")&&interleaved);
    assert(epoch==version);
    while(upload->accepted<length)assert(!chunk(upload,data,length));
    assert(upload->committed==131072&&upload->accepted==length);
    download(reader,old,sizeof old);
    assert(tiku_paged_storage_list(owner,"/",epoch,0)==TIKU_RESOURCE_PENDING);
    assert(!exchange(upload,TIKU_MT_FINISH,0,length,NULL,0));
    assert(tiku_mfs_names_epoch(&fs)!=version);
    drain(&media,owner,0);assert(tiku_paged_storage_status(owner)==TIKU_RESOURCE_STALE_CURSOR);
    download(reader,old,sizeof old);
    assert(!exchange(reader,TIKU_MT_CLOSE,0,0,NULL,0));
    assert(!exchange(reader,TIKU_MT_OPEN,0,0,"shared.bin",10));download(reader,data,length);
    assert(!tiku_media_transfer_close(reader)&&!tiku_media_transfer_close(upload));
    assert(tiku_paged_storage_list(owner,"/",0,0)==TIKU_RESOURCE_PENDING);drain(&media,owner,0);
    page=tiku_paged_storage_result(owner,&count,&epoch,&eof);assert(page);
    unsigned seen=0;
    for(;;){
        seen+=(unsigned)count;if(eof)break;
        after=page->entries[count-1].node_id;
        assert(tiku_paged_storage_list(owner,"/",epoch,after)==TIKU_RESOURCE_PENDING);drain(&media,owner,0);
        page=tiku_paged_storage_result(owner,&count,&epoch,&eof);assert(page);
    }
    assert(seen==41&&count==9&&!strcmp(page->entries[8].path,"/shared.bin"));
    assert(tiku_paged_storage_read(owner,"/shared.bin",epoch,page->entries[8].node_id,0)==TIKU_RESOURCE_PENDING);
    drain(&media,owner,0);page=tiku_paged_storage_result(owner,&count,&epoch,&eof);
    assert(page&&count==1024&&!memcmp(page->bytes,data,1024));
    tiku_paged_storage_close(owner);assert(tiku_paged_storage_destroy(&owner)&&!host.memory.live);
    assert(!store.running&&!store.count&&!store.writer);
    for(unsigned i=0;i<TIKU_MFS_READERS;i++)assert(!store.readers[i].token);
    free(bridge);free(reader);free(upload);free(data);free(disk);
    puts("Transfer/browser: private progress, busy retry, resume, publication and snapshot bytes pass");
    return 0;
}
