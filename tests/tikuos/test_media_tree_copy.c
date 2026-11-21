/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_tree_copy.c - subtree copy publication, content and recovery.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_TREE_COPY_FIXTURE 1
#include "test_media_subtrees.c"
static uint8_t transfer[TIKU_MFS_CHUNK],data[TIKU_MFS_CHUNK];
/** @brief Copy one subtree through borrowed transport storage. */
static int copy_tree(uint64_t id,const char *source,const char *target)
{
    int rc=tiku_mfs_mutation_tree_copy_begin(&fs,mutation,id,source,target,transfer,sizeof transfer);
    if(rc)return rc;
    uint64_t prepared=0,total=0;
    for(unsigned n=0;advance();++n){uint64_t a,b;assert(n<1000000);
        assert(!tiku_mfs_mutation_progress(mutation,&a,&b));assert(a>=prepared&&a<=b&&b>=total);
        prepared=a;total=b;supply();}
    return tiku_mfs_mutation_result(mutation);
}
/** @brief Require preserved sources and either a complete destination or no destination. */
static void copied_tree(unsigned typed)
{
    tiku_mfs_object_t a,b;assert(!tiku_mfs_stat(&fs,"before/a",&a));
    int copied=tiku_mfs_stat(&fs,"after/a",&b);assert(!copied||copied==TIKU_MFS_NOENT);
    const char *names[]={"","/a","/empty","/nested","/nested/b"};
    for(unsigned i=0;i<5;++i){if(!typed&&(i==0||i==2||i==3))continue;
        char source[64],target[64];snprintf(source,sizeof source,"before%s",names[i]);snprintf(target,sizeof target,"after%s",names[i]);
        assert(!tiku_mfs_stat(&fs,source,&a));assert(tiku_mfs_stat(&fs,target,&b)==copied);
        if(!copied){assert(a.id!=b.id&&a.length==b.length&&(a.policy==TIKU_MFS_DIRECTORY)==(b.policy==TIKU_MFS_DIRECTORY));
            assert(!memcmp(a.digest,b.digest,32));
            if(a.length){uint64_t reader;assert(!tiku_mfs_open_read(&fs,target,&reader));
                assert(tiku_mfs_read_at(&fs,reader,0,out,3)==3&&!memcmp(out,"abc",3));assert(!tiku_mfs_close_read(&fs,reader));}}
    }
    assert(!tiku_mfs_stat(&fs,"neighbor",&a));assert(!tiku_mfs_stat(&fs,"before0",&a));
}
/** @brief Verify typed, implicit, empty and binary copies with new identities and one publication. */
static void copy_behavior(void)
{
    for(unsigned typed=0;typed<2;++typed){uint64_t id=tree_fixture(typed),reader;
        assert(!tiku_mfs_open_read(&fs,"before/nested/b",&reader));uint64_t seq=fs.sequence;
        assert(copy_tree(id,"before","before")==TIKU_MFS_BUSY);
        assert(copy_tree(id,"before","before/inside")==TIKU_MFS_BUSY);
        assert(copy_tree(id,"before","neighbor")==TIKU_MFS_BUSY);
        create_file("occupied/file");unsigned nw=writes;uint64_t next=fs.next_id,alloc=fs.allocations;
        assert(copy_tree(id,"before","occupied")==TIKU_MFS_BUSY&&writes==nw&&fs.next_id==next&&fs.allocations==alloc);
        seq=fs.sequence;assert(!copy_tree(id,"before","after")&&fs.sequence==seq+1);copied_tree(typed);
        assert(tiku_mfs_mutation_id(mutation)==(typed?mutation->copy_id:0));
        assert(tiku_mfs_read_at(&fs,reader,0,out,3)==3&&!memcmp(out,"abc",3));assert(!tiku_mfs_close_read(&fs,reader));
        reboot();copied_tree(typed);assert(!tiku_mfs_collect_begin(&fs));int rc;do{rc=tiku_mfs_collect_step(&fs,3);}while(rc>0);assert(!rc);
        reboot();copied_tree(typed);
    }
    tree_fixture(1);uint64_t empty=dir("empty"),seq=fs.sequence;assert(!copy_tree(empty,"empty","empty-copy")&&fs.sequence==seq+1);
    tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"empty-copy",&o)&&o.policy==TIKU_MFS_DIRECTORY&&o.id!=empty);
    for(unsigned i=0;i<sizeof data;++i)data[i]=(uint8_t)(i*37+i/13);
    uint64_t root=dir("binary");assert(!tiku_mfs_mutation_replace_begin(&fs,mutation,0,"binary/full",data,sizeof data));assert(!complete_tree());
    assert(!tiku_mfs_mutation_replace_begin(&fs,mutation,0,"binary/zero",NULL,0));assert(!complete_tree());
    assert(!copy_tree(root,"binary","binary-copy"));uint64_t reader;assert(!tiku_mfs_open_read(&fs,"binary-copy/full",&reader));
    assert(tiku_mfs_read_at(&fs,reader,0,transfer,sizeof transfer)==sizeof transfer&&!memcmp(transfer,data,sizeof data));assert(!tiku_mfs_close_read(&fs,reader));
    assert(!tiku_mfs_stat(&fs,"binary-copy/zero",&o)&&!o.length);
}
/** @brief Refuse oversized or pending descendants before allocating destination metadata. */
static void copy_refusal(void)
{
    uint64_t root=tree_fixture(1),id;assert(!tiku_mfs_begin(&fs,"before/large",TIKU_MFS_STREAM,0,NULL,&id));
    assert(!tiku_mfs_append(&fs,data,sizeof data));assert(!tiku_mfs_append(&fs,"x",1));assert(!tiku_mfs_finalize(&fs));
    unsigned nw=writes;uint64_t next=fs.next_id,alloc=fs.allocations;
    assert(copy_tree(root,"before","after")==TIKU_MFS_LIMIT&&writes==nw&&fs.next_id==next&&fs.allocations==alloc&&!fs.failed);
    root=tree_fixture(1);uint8_t digest[32]={0};assert(!tiku_mfs_begin(&fs,"before/pending",TIKU_MFS_ATOMIC,1,digest,&id));assert(!tiku_mfs_release(&fs));
    nw=writes;next=fs.next_id;alloc=fs.allocations;
    assert(copy_tree(root,"before","after")==TIKU_MFS_BUSY&&writes==nw&&fs.next_id==next&&fs.allocations==alloc&&!fs.failed);
}
/** @brief Reject corrupted digests or payload slices and recover from exhausted media. */
static void copy_integrity(void)
{
    for(unsigned mode=0;mode<2;++mode){uint64_t root=tree_fixture(1);tiku_mfs_object_t o;
        assert(!tiku_mfs_stat(&fs,"before/nested/b",&o));
        if(!mode){o.digest[0]^=1;assert(!commit(&fs,&o,0));}
        else {uint8_t page_bytes[512];assert(!disk_read(NULL,o.root.lba,1,page_bytes));
            tiku_mfs_ref_t frame=ref_get(page_bytes,0);assert(frame.lba);
            stable[(frame.lba+1)*512]^=1;memcpy(cache,stable,BYTES);}
        assert(copy_tree(root,"before","after")==TIKU_MFS_CORRUPT);
        if(mode)assert(!memcmp(transfer,"abc",3));
        reboot();
        assert(tiku_mfs_stat(&fs,"after",&o)==TIKU_MFS_NOENT);copied_tree(1);
    }
    uint64_t root=tree_fixture(1),filler;int rc;unsigned count=0;
    assert(!tiku_mfs_begin(&fs,"filler",TIKU_MFS_STREAM,0,NULL,&filler));
    do{rc=tiku_mfs_append(&fs,data,sizeof data);if(!rc)assert(!tiku_mfs_sync(&fs));assert(++count<1000);}while(!rc);
    assert(rc==TIKU_MFS_SPACE);
    do{rc=tiku_mfs_append(&fs,data,1);if(!rc)assert(!tiku_mfs_sync(&fs));assert(++count<2000);}while(!rc);
    assert(rc==TIKU_MFS_SPACE&&!tiku_mfs_finalize(&fs));reboot();
    assert(copy_tree(root,"before","after")==TIKU_MFS_SPACE);reboot();copied_tree(1);
    tiku_mfs_object_t o;assert(tiku_mfs_stat(&fs,"after",&o)==TIKU_MFS_NOENT);
}
/** @brief Copy a rotated catalog and reject mapped-path and identity violations. */
static void copy_catalogs(void)
{
    uint64_t root=tree_fixture(1);unsigned nw=writes;uint64_t next=fs.next_id,alloc=fs.allocations;
    char longname[64];memset(longname,'x',60);longname[60]=0;
    assert(copy_tree(root,"before",longname)==TIKU_MFS_LIMIT);
    assert(copy_tree(0,"missing","after")==TIKU_MFS_NOENT);
    assert(copy_tree(0,"before","after")==TIKU_MFS_STALE);
    tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"before/a",&o));
    assert(copy_tree(o.id,"before/a","after")==TIKU_MFS_ARG);
    assert(tiku_mfs_mutation_tree_copy_begin(&fs,mutation,root,"before","after",transfer,sizeof transfer-1)==TIKU_MFS_ARG);
    assert(writes==nw&&fs.next_id==next&&fs.allocations==alloc&&!fs.failed);
    for(unsigned i=0;i<73;++i){char name[64];snprintf(name,sizeof name,"before/many/%03u",i*37%73);create_file(name);}
    uint64_t seq=fs.sequence;assert(!copy_tree(root,"before","many")&&fs.sequence==seq+1);reboot();
    for(unsigned i=0;i<73;++i){char name[64];tiku_mfs_object_t original;
        snprintf(name,sizeof name,"before/many/%03u",i);assert(!tiku_mfs_stat(&fs,name,&original));
        snprintf(name,sizeof name,"many/many/%03u",i);assert(!tiku_mfs_stat(&fs,name,&o)&&o.length==3&&o.id!=original.id);}
}
/** @brief Cut writes and barriers, then cancel at each state with live leases retained. */
static void copy_interrupted(void)
{
    for(unsigned typed=0;typed<2;++typed){uint64_t id=tree_fixture(typed);memcpy(snapshot,stable,BYTES);reboot();events=0;
        assert(!copy_tree(id,"before","after"));unsigned count=events;
        for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=count;++cut){
            memcpy(stable,snapshot,BYTES);reboot();events=0;fail_mode=mode;fail_at=cut;
            assert(copy_tree(id,"before","after")==TIKU_MFS_IO);reboot();copied_tree(typed);
        }
        memcpy(stable,snapshot,BYTES);reboot();assert(!tiku_mfs_mutation_tree_copy_begin(&fs,mutation,id,"before","after",transfer,sizeof transfer));
        unsigned steps=0;while(advance()){supply();++steps;}assert(!tiku_mfs_mutation_result(mutation));
        for(unsigned stop=0;stop<=steps+1;++stop){
            memcpy(stable,snapshot,BYTES);reboot();assert(!tiku_mfs_mutation_tree_copy_begin(&fs,mutation,id,"before","after",transfer,sizeof transfer));
            for(unsigned i=0;i<stop&&!mutation->done;++i){advance();if(i+1<stop)supply();}
            uint64_t lba;void *p;if(tiku_mfs_mutation_io(mutation,&lba,&p)){assert(tiku_mfs_mutation_cancel(&fs,mutation)==TIKU_MFS_BUSY);supply();}
            assert(!tiku_mfs_mutation_cancel(&fs,mutation));reboot();copied_tree(typed);
        }
        printf("Tree copy typed=%u: %u cuts, %u cancellations\n",typed,count*3,steps+2);
    }
}
int main(void)
{
    stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);void *memory=malloc(tiku_mfs_mutation_size());
    assert(stable&&cache&&snapshot&&memory);mutation=tiku_mfs_mutation_init(memory,tiku_mfs_mutation_size());assert(mutation);
    copy_behavior();copy_refusal();copy_integrity();copy_catalogs();copy_interrupted();printf("Tree copy passed; owner=%zu bytes\n",tiku_mfs_mutation_size());
    free(memory);free(stable);free(cache);free(snapshot);return 0;
}
