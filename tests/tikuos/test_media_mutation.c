/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_mutation.c - paced catalog edits, atomic publication and recovery.
 * SPDX-License-Identifier: Apache-2.0
 */
#define TIKU_MEDIA_NAMES_ENABLE 1
#define TIKU_MEDIA_ASYNC_ENABLE 1
#define main mfs_fixture_main
#include <TikuBench/tests/host/kernel/test_mfs.c>
#undef main
#include <assert.h>
static tiku_mfs_mutation_t *mutation;
/** @brief Advance without hidden block access and bound allocation traversal. */
static int advance(void)
{
    unsigned nr=reads,nw=writes,ne=events;uint32_t cursor=fs.cursor;
    int rc=tiku_mfs_mutation_step(&fs,mutation);
    assert(reads==nr&&writes==nw&&events==ne);
    uint32_t distance=fs.cursor>=cursor?fs.cursor-cursor:fs.segments-1-cursor+fs.cursor;
    assert(distance<=64);return rc;
}
/** @brief Retire one requested sector after proving repeated steps preserve its lease. */
static void supply(void)
{
    uint64_t lba;void *sector;int kind=tiku_mfs_mutation_io(mutation,&lba,&sector);
    if(!kind)return;
    uint8_t copy[512];memcpy(copy,sector,512);
    assert(advance()==1);void *again;uint64_t again_lba;
    assert(tiku_mfs_mutation_io(mutation,&again_lba,&again)==kind&&again_lba==lba&&again==sector&&!memcmp(copy,sector,512));
    int rc=kind==TIKU_MFS_MUTATION_READ?disk_read(NULL,lba,1,sector):
        kind==TIKU_MFS_MUTATION_WRITE?disk_write(NULL,lba,1,sector):disk_sync(NULL);
    assert(!tiku_mfs_mutation_supply(mutation,rc?TIKU_MFS_IO:0));
    assert(tiku_mfs_mutation_supply(mutation,0)==TIKU_MFS_ARG);
}
/** @brief Execute a bounded operation with a finite fixture deadline. */
static int change(uint64_t id,const char *source,const char *target)
{
    int rc=tiku_mfs_mutation_begin(&fs,mutation,id,source,target);if(rc)return rc;
    for(unsigned n=0;advance();++n){assert(n<20000);supply();}
    return tiku_mfs_mutation_result(mutation);
}
/** @brief Create a completed file using the existing writer contract. */
static uint64_t create_file(const char *name)
{
    uint64_t id;assert(!tiku_mfs_begin(&fs,name,TIKU_MFS_STREAM,0,NULL,&id));
    assert(!tiku_mfs_append(&fs,"abc",3));assert(!tiku_mfs_finalize(&fs));return id;
}
/** @brief Check file identity, parent/child conflicts and retained snapshot data. */
static void behavior(void)
{
    fresh();uint64_t id=create_file("old/a"),other=create_file("target"),reader;
    assert(!tiku_mfs_open_read(&fs,"old/a",&reader));unsigned before=writes;uint64_t sequence=fs.sequence;
    assert(change(id,"old/a","target")==TIKU_MFS_BUSY);
    assert(change(id,"old/a","target/child")==TIKU_MFS_BUSY);
    assert(change(other,"target","old")==TIKU_MFS_BUSY);
    assert(change(id,"wrong","new")==TIKU_MFS_STALE);
    assert(change(id,"old/a","../bad")==TIKU_MFS_ARG);
    assert(!change(id,"old/a","old/a")&&writes==before&&fs.sequence==sequence);
    uint64_t version=fs.names_epoch;assert(!change(id,"old/a","new/b"));
    assert(fs.sequence==sequence+1&&fs.names_epoch!=version);
    tiku_mfs_object_t o;assert(tiku_mfs_stat(&fs,"old/a",&o)==TIKU_MFS_NOENT);
    assert(!tiku_mfs_stat(&fs,"new/b",&o)&&o.id==id&&o.length==3);
    assert(change(id,"old/a",NULL)==TIKU_MFS_STALE);
    assert(!change(id,"new/b",NULL));assert(tiku_mfs_stat(&fs,"new/b",&o)==TIKU_MFS_NOENT);
    assert(tiku_mfs_read_at(&fs,reader,0,out,3)==3&&!memcmp(out,"abc",3));assert(!tiku_mfs_close_read(&fs,reader));
    reboot();assert(!tiku_mfs_stat(&fs,"target",&o)&&o.id==other);
    uint64_t draft;assert(!tiku_mfs_begin(&fs,"writing",TIKU_MFS_STREAM,0,NULL,&draft));
    assert(change(other,"target","later")==TIKU_MFS_BUSY);assert(!tiku_mfs_release(&fs));
    assert(change(draft,"writing",NULL)==TIKU_MFS_STALE);
    uint8_t hash[32]={0};assert(!tiku_mfs_begin(&fs,"target",TIKU_MFS_ATOMIC,1,hash,&draft));assert(!tiku_mfs_release(&fs));
    assert(change(other,"target","later")==TIKU_MFS_BUSY);assert(change(other,"target",NULL)==TIKU_MFS_BUSY);
}
/** @brief Exercise repeated rotations, successor deletion and checkpoint rollover. */
static void catalogs(void)
{
    fresh();uint64_t ids[97];char names[97][64];uint8_t present[97];
    for(unsigned i=0;i<97;++i){snprintf(names[i],64,"files/%03u",i);ids[i]=create_file(names[i]);present[i]=1;}
    for(unsigned pass=0;pass<3;++pass)for(unsigned j=0;j<97;++j){
        unsigned i=(j*37)%97;
        if(pass<2){char target[64];snprintf(target,64,"files/%c%03u",'a'+(int)pass,96-i);
            assert(!change(ids[i],names[i],target));strcpy(names[i],target);
        }else {assert(!change(ids[i],names[i],NULL));present[i]=0;}
        tiku_mfs_object_t o;unsigned count=0;
        for(unsigned k=0;k<97;++k){int rc=tiku_mfs_stat(&fs,names[k],&o);
            assert(present[k]?(!rc&&o.id==ids[k]&&o.length==3):rc==TIKU_MFS_NOENT);count+=present[k];}
        assert(fs.ids.count==count&&fs.names.count==count);
        if(j%11==0)reboot();
    }
    reboot();assert(!fs.ids.count&&!fs.names.count);
}
/** @brief Traverse a long occupied bitmap without exceeding one allocation slice. */
static void allocation_pacing(void)
{
    block.sectors=UINT64_C(1)<<24;fresh();uint64_t id=create_file("source");
    for(unsigned i=1;i<2000;++i)set(fs.used,i);
    free_count(&fs);fs.cursor=1;fs.tail=0;
    assert(!tiku_mfs_mutation_begin(&fs,mutation,id,"source","dest"));
    unsigned slices=0;uint64_t lba;void *p;
    for(unsigned n=0;n<2000;++n){
        uint32_t before=fs.cursor;assert(advance()==1);slices+=fs.cursor!=before;
        int kind=tiku_mfs_mutation_io(mutation,&lba,&p);
        if(kind==TIKU_MFS_MUTATION_WRITE){assert(lba==(uint64_t)2000*TIKU_MFS_SEGMENT);break;}
        supply();assert(n<1999);
    }
    assert(slices>=32);assert(tiku_mfs_mutation_cancel(&fs,mutation)==TIKU_MFS_BUSY);
    assert(!tiku_mfs_mutation_supply(mutation,TIKU_MFS_IO));assert(!tiku_mfs_mutation_cancel(&fs,mutation)&&fs.failed);
    block.sectors=BYTES/512;
}
/** @brief Require exactly one name or an atomic deletion after each write/barrier cut. */
static void interrupted(void)
{
    for(unsigned remove=0;remove<2;++remove){
        fresh();uint64_t id=create_file("source");create_file("neighbor");memcpy(snapshot,stable,BYTES);
        reboot();events=0;assert(!change(id,"source",remove?NULL:"dest"));unsigned count=events;
        for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=count;++cut){
            memcpy(stable,snapshot,BYTES);reboot();events=0;fail_mode=mode;fail_at=cut;
            assert(change(id,"source",remove?NULL:"dest")==TIKU_MFS_IO&&fs.failed);reboot();
            tiku_mfs_object_t a,b;int old=tiku_mfs_stat(&fs,"source",&a),new=tiku_mfs_stat(&fs,"dest",&b);
            if(remove)assert(new==TIKU_MFS_NOENT&&(!old||old==TIKU_MFS_NOENT));
            else assert((!old&&new==TIKU_MFS_NOENT)||(old==TIKU_MFS_NOENT&&!new));
            if(!old)assert(a.id==id&&a.length==3);
            if(!new)assert(b.id==id&&b.length==3);
            assert(!tiku_mfs_stat(&fs,"neighbor",&a));
        }
        printf("Mutation: %s recovered across %u write/barrier cuts\n",remove?"delete":"rename",count*3);
    }
}
/** @brief Cancel at every phase, refusing to release a live sector lease. */
static void cancellations(void)
{
    fresh();uint64_t id=create_file("source");create_file("neighbor");memcpy(snapshot,stable,BYTES);
    for(unsigned stop=0;stop<260;++stop){
        memcpy(stable,snapshot,BYTES);reboot();assert(!tiku_mfs_mutation_begin(&fs,mutation,id,"source","dest"));
        for(unsigned i=0;i<stop&&!mutation->done;++i){advance();if(i+1<stop)supply();}
        unsigned changed=mutation->changed,done=mutation->done;
        uint64_t lba;void *p;if(tiku_mfs_mutation_io(mutation,&lba,&p)){
            assert(tiku_mfs_mutation_cancel(&fs,mutation)==TIKU_MFS_BUSY);supply();
        }
        assert(!tiku_mfs_mutation_cancel(&fs,mutation));if(changed&&!done)assert(fs.failed);
        reboot();tiku_mfs_object_t a,b;int old=tiku_mfs_stat(&fs,"source",&a),new=tiku_mfs_stat(&fs,"dest",&b);
        assert((!old&&new==TIKU_MFS_NOENT)||(old==TIKU_MFS_NOENT&&!new));
    }
}
#ifndef SURFACE_MUTATION_QUEUE_FIXTURE
#define MUTATION_MAIN main
#else
#define MUTATION_MAIN mutation_core_main
#endif
int MUTATION_MAIN(void)
{
    stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);
    void *memory=malloc(tiku_mfs_mutation_size());assert(stable&&cache&&snapshot&&memory);
    mutation=tiku_mfs_mutation_init(memory,tiku_mfs_mutation_size());assert(mutation);
    behavior();catalogs();allocation_pacing();interrupted();cancellations();
    printf("Mutation: metadata pacing, conflicts, rotations, snapshots and cancellation passed (%zu bytes)\n",tiku_mfs_mutation_size());
    free(memory);free(stable);free(cache);free(snapshot);return 0;
}
