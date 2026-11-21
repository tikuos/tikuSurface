/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_replace.c - atomic bounded file creation and content replacement.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_MUTATION_QUEUE_FIXTURE 1
#include "test_media_mutation.c"
/** @brief Advance one content transaction with bounded hashing and no hidden I/O. */
static int replace_file(uint64_t expected,const char *name,const void *data,uint32_t bytes)
{
    int rc=tiku_mfs_mutation_replace_begin(&fs,mutation,expected,name,data,bytes);if(rc)return rc;
    for(unsigned n=0;;++n){
        uint32_t at=mutation->payload_at;int pending=advance();assert(mutation->payload_at-at<=64);
        if(!pending)break;
        assert(n<20000);supply();
    }
    return tiku_mfs_mutation_result(mutation);
}
/** @brief Verify published bytes through the existing snapshot reader. */
static uint64_t verify_file(const char *name,const void *data,uint32_t bytes)
{
    tiku_mfs_object_t object;uint64_t token;
    assert(!tiku_mfs_stat(&fs,name,&object)&&object.length==bytes&&object.complete&&object.visible);
    assert(!tiku_mfs_open_read(&fs,name,&token));
    if(bytes)assert(tiku_mfs_read_at(&fs,token,0,out,bytes)==(int)bytes&&!memcmp(out,data,bytes));
    assert(tiku_mfs_read_at(&fs,token,bytes,out,1)==0);assert(!tiku_mfs_close_read(&fs,token));return object.id;
}
/** @brief Check complete creation, compare-and-replace and immutable readers. */
static void contents(void)
{
    fresh();assert(tiku_mfs_mutation_size()<=8192);
    assert(replace_file(0,"bad",payload,65537)==TIKU_MFS_ARG);
    assert(replace_file(0,"bad",NULL,1)==TIKU_MFS_ARG);
    assert(replace_file(0,"../bad",payload,1)==TIKU_MFS_ARG);
    assert(!replace_file(0,"empty",NULL,0));uint64_t empty=verify_file("empty",NULL,0);
    assert(replace_file(0,"empty","x",1)==TIKU_MFS_BUSY);
    assert(replace_file(empty+1,"empty","x",1)==TIKU_MFS_STALE);
    assert(replace_file(empty,"missing","x",1)==TIKU_MFS_STALE);
    assert(replace_file(0,"empty/child","x",1)==TIKU_MFS_BUSY);
    assert(!replace_file(empty,"empty","abc",3));uint64_t id=verify_file("empty","abc",3),reader;
    assert(id!=empty);assert(!tiku_mfs_open_read(&fs,"empty",&reader));
    tiku_mfs_object_t object;assert(!tiku_mfs_stat(&fs,"empty",&object));
    static const uint8_t abc[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    assert(!memcmp(object.digest,abc,32)&&!memcmp(object.expected_digest,abc,32));
    static const unsigned sizes[]={1,63,64,65,511,512,513,4095,4096,4097,65535,65536,0};
    for(unsigned i=0;i<sizeof sizes/sizeof *sizes;++i){
        uint64_t epoch=fs.names_epoch,sequence=fs.sequence;
        assert(!replace_file(id,"empty",payload,sizes[i]));uint64_t next=verify_file("empty",payload,sizes[i]);
        assert(next!=id&&fs.ids.count==1&&fs.names.count==1&&fs.sequence==sequence+1&&fs.names_epoch!=epoch);
        assert(tiku_mfs_read_at(&fs,reader,0,out,3)==3&&!memcmp(out,"abc",3));id=next;
    }
    assert(!tiku_mfs_close_read(&fs,reader));reboot();assert(verify_file("empty",NULL,0)==id);
    assert(!replace_file(0,"folder/file",payload,4097));
    assert(replace_file(0,"folder",payload,1)==TIKU_MFS_BUSY);
    uint64_t draft;uint8_t hash[32]={0};assert(!tiku_mfs_begin(&fs,"empty",TIKU_MFS_ATOMIC,1,hash,&draft));
    assert(replace_file(id,"empty",payload,1)==TIKU_MFS_BUSY);assert(!tiku_mfs_release(&fs));
    assert(replace_file(id,"empty",payload,1)==TIKU_MFS_BUSY);
}
/** @brief Recover creation/replacement after each data, metadata and barrier cut. */
static void content_cuts(void)
{
    for(unsigned create=0;create<2;++create){
        fresh();uint64_t id=create?0:create_file("document");create_file("neighbor");memcpy(snapshot,stable,BYTES);
        reboot();events=0;assert(!replace_file(id,"document",payload,4097));unsigned count=events;
        for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=count;++cut){
            memcpy(stable,snapshot,BYTES);reboot();events=0;fail_mode=mode;fail_at=cut;
            assert(replace_file(id,"document",payload,4097)==TIKU_MFS_IO&&fs.failed);reboot();
            tiku_mfs_object_t o;int rc=tiku_mfs_stat(&fs,"document",&o);
            if(rc)assert(create&&rc==TIKU_MFS_NOENT);
            else if(o.id==id)assert(verify_file("document","abc",3)==id);
            else assert(verify_file("document",payload,4097)==o.id);
            (void)verify_file("neighbor","abc",3);
        }
        printf("Contents: %s recovered across %u write/barrier cuts\n",create?"create":"replace",count*3);
    }
}
/** @brief Cancel each phase while retaining outstanding sector ownership. */
static void content_cancel(void)
{
    fresh();uint64_t id=create_file("document");memcpy(snapshot,stable,BYTES);unsigned completed=0;
    for(unsigned stop=0;stop<400;++stop){
        memcpy(stable,snapshot,BYTES);reboot();assert(!tiku_mfs_mutation_replace_begin(&fs,mutation,id,"document",payload,4097));
        for(unsigned i=0;i<stop&&!mutation->done;++i){advance();if(i+1<stop)supply();}
        uint64_t lba;void *sector;if(tiku_mfs_mutation_io(mutation,&lba,&sector)){
            assert(tiku_mfs_mutation_cancel(&fs,mutation)==TIKU_MFS_BUSY);supply();
        }
        unsigned changed=mutation->changed,done=mutation->done;completed+=done;
        assert(!tiku_mfs_mutation_cancel(&fs,mutation));if(changed&&!done)assert(fs.failed);
        reboot();tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"document",&o));
        (void)verify_file("document",o.id==id?(const void *)"abc":payload,o.id==id?3:4097);
    }
    assert(completed);
}
/** @brief Exercise namespace rotations and journal rollover with replaced contents. */
static void content_catalog(void)
{
    fresh();uint64_t ids[67];char name[64];
    for(unsigned i=0;i<67;++i){snprintf(name,sizeof name,"files/%03u",i);assert(!replace_file(0,name,payload,i+1));ids[i]=verify_file(name,payload,i+1);}
    for(unsigned j=0;j<67;++j){unsigned i=j*19%67;snprintf(name,sizeof name,"files/%03u",i);assert(!replace_file(ids[i],name,payload,513+i));ids[i]=verify_file(name,payload,513+i);if(j%9==0)reboot();}
    assert(fs.ids.count==67&&fs.names.count==67);reboot();
    for(unsigned i=0;i<67;++i){snprintf(name,sizeof name,"files/%03u",i);assert(verify_file(name,payload,513+i)==ids[i]);}
}
/** @brief Refuse full-media creation and replacement before changing the visible file. */
static void content_full(void)
{
    fresh();uint64_t old=create_file("document"),filler;int rc;unsigned count=0;
    assert(!tiku_mfs_begin(&fs,"filler",TIKU_MFS_STREAM,0,NULL,&filler));
    do{rc=tiku_mfs_append(&fs,payload,65536);if(!rc)assert(!tiku_mfs_sync(&fs));assert(++count<1000);}while(!rc);
    assert(rc==TIKU_MFS_SPACE);
    do{rc=tiku_mfs_append(&fs,payload,1);if(!rc)assert(!tiku_mfs_sync(&fs));assert(++count<2000);}while(!rc);
    assert(rc==TIKU_MFS_SPACE&&!tiku_mfs_finalize(&fs));reboot();
    unsigned before=writes;uint64_t sequence=fs.sequence,epoch=fs.names_epoch;
    assert(replace_file(old,"document",payload,65536)==TIKU_MFS_SPACE);
    assert(replace_file(0,"new",NULL,0)==TIKU_MFS_SPACE);
    assert(writes==before&&fs.sequence==sequence&&fs.names_epoch==epoch&&!fs.failed);
    assert(verify_file("document","abc",3)==old);tiku_mfs_object_t object;
    assert(tiku_mfs_stat(&fs,"new",&object)==TIKU_MFS_NOENT);
    assert(!tiku_mfs_delete(&fs,filler)&&!tiku_mfs_collect_begin(&fs));
    do{rc=tiku_mfs_collect_step(&fs,8);}while(rc>0);assert(!rc);
    assert(!replace_file(old,"document",payload,65536));(void)verify_file("document",payload,65536);
}

int main(void)
{
    stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);
    void *memory=malloc(tiku_mfs_mutation_size());assert(stable&&cache&&snapshot&&memory);
    mutation=tiku_mfs_mutation_init(memory,tiku_mfs_mutation_size());assert(mutation);
    for(unsigned i=0;i<sizeof payload;++i)payload[i]=(uint8_t)(i*37+11);
    contents();content_cuts();content_cancel();content_catalog();content_full();
    printf("Contents: sector/hash pacing, atomic replacement and snapshots passed (%zu bytes)\n",tiku_mfs_mutation_size());
    free(memory);free(stable);free(cache);free(snapshot);return 0;
}
