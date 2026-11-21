/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_directory_upgrade.c - explicit feature migration and mixed-bank recovery.
 * SPDX-License-Identifier: Apache-2.0
 */
#define TIKU_MEDIA_DIRECTORIES_ENABLE 1
#define SURFACE_MUTATION_QUEUE_FIXTURE 1
#include "test_media_mutation.c"
/** @brief Complete migration while enforcing sector ownership and pacing. */
static int finish_upgrade(void)
{for(unsigned i=0;advance();++i){assert(i<30000);supply();}return tiku_mfs_mutation_result(mutation);}
/** @brief Enable persistent directories without formatting or replacing contents. */
static int upgrade(void)
{int rc=tiku_mfs_mutation_upgrade_begin(&fs,mutation);return rc?rc:finish_upgrade();}
/** @brief Check file contents and reject writes while legacy headers remain authoritative. */
static void preserved(void)
{
    tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"kept",&o)&&o.length==3);
    uint64_t reader;assert(!tiku_mfs_open_read(&fs,"kept",&reader));assert(tiku_mfs_read_at(&fs,reader,0,out,3)==3&&!memcmp(out,"abc",3));assert(!tiku_mfs_close_read(&fs,reader));
    if(tiku_mfs_directory_upgrade_pending(&fs)){
        uint64_t id,next_id=fs.next_id,allocations=fs.allocations;unsigned before=writes;assert(!tiku_mfs_has_directories(&fs));
        assert(tiku_mfs_begin(&fs,"blocked",TIKU_MFS_STREAM,0,NULL,&id)==TIKU_MFS_BUSY);
        assert(writes==before&&fs.next_id==next_id&&fs.allocations==allocations&&!fs.failed);
        assert(tiku_mfs_checkpoint(&fs)==TIKU_MFS_BUSY);
        assert(tiku_mfs_collect_begin(&fs)==TIKU_MFS_BUSY);
    }
}
/** @brief Stop after the new checkpoint flush but before retiring the legacy bank. */
static void mixed(void)
{
    fresh();create_file("kept");assert(!tiku_mfs_mutation_upgrade_begin(&fs,mutation));
    for(unsigned i=0;mutation->phase!=MUT_UPGRADE_READ;++i){assert(i<30000);advance();supply();}
    assert(fs.features==3);assert(!tiku_mfs_mutation_cancel(&fs,mutation)&&fs.failed);reboot();
    assert(tiku_mfs_directory_upgrade_pending(&fs)&&fs.features==2);preserved();
}
/** @brief Preserve reader snapshots, require explicit upgrade and resume mixed mounts. */
static void behavior_upgrade(void)
{
    fresh();create_file("kept");uint64_t reader;assert(!tiku_mfs_open_read(&fs,"kept",&reader));
    assert(!tiku_mfs_has_directories(&fs)&&!tiku_mfs_directory_upgrade_pending(&fs));assert(!upgrade());
    assert(tiku_mfs_has_directories(&fs)&&!tiku_mfs_directory_upgrade_pending(&fs));
    for(unsigned i=0;i<2;++i){const uint8_t *header=stable+(size_t)i*fs.bank_sectors*512;assert(valid(&fs,header,"MFC2")&&get32(header+88)==1);}
    assert(tiku_mfs_read_at(&fs,reader,0,out,3)==3&&!memcmp(out,"abc",3));assert(!tiku_mfs_close_read(&fs,reader));
    unsigned before=writes;assert(!upgrade()&&writes==before);reboot();preserved();assert(tiku_mfs_has_directories(&fs));
    assert(!tiku_mfs_mutation_mkdir_begin(&fs,mutation,"new-folder")&&!finish_upgrade());reboot();
    tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"new-folder",&o)&&o.policy==TIKU_MFS_DIRECTORY);
    mixed();assert(!upgrade());reboot();assert(tiku_mfs_has_directories(&fs));preserved();
    mixed();
    /* The private fixture models a legacy owner that cannot see the new bank. */
    fs.features=0;create_file("legacy-later");reboot();assert(!tiku_mfs_stat(&fs,"legacy-later",&o));
    assert(!upgrade());reboot();preserved();assert(!tiku_mfs_stat(&fs,"legacy-later",&o)&&tiku_mfs_has_directories(&fs));
    fresh();uint64_t id;assert(!tiku_mfs_begin(&fs,"upload",TIKU_MFS_STREAM,0,NULL,&id));
    assert(upgrade()==TIKU_MFS_BUSY&&!tiku_mfs_has_directories(&fs));assert(!tiku_mfs_release(&fs));
}
/** @brief Recover and explicitly resume every interrupted migration write/barrier. */
static void interrupted_upgrade(void)
{
    fresh();create_file("kept");memcpy(snapshot,stable,BYTES);events=0;assert(!upgrade());unsigned count=events;
    for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=count;++cut){
        memcpy(stable,snapshot,BYTES);reboot();events=0;fail_mode=mode;fail_at=cut;
        assert(upgrade()==TIKU_MFS_IO);reboot();preserved();assert(!upgrade());reboot();preserved();assert(tiku_mfs_has_directories(&fs));
    }
    memcpy(stable,snapshot,BYTES);reboot();assert(!tiku_mfs_mutation_upgrade_begin(&fs,mutation));unsigned steps=0;
    while(advance()){supply();++steps;}assert(!tiku_mfs_mutation_result(mutation));
    for(unsigned stop=0;stop<=steps+1;++stop){
        memcpy(stable,snapshot,BYTES);reboot();assert(!tiku_mfs_mutation_upgrade_begin(&fs,mutation));
        for(unsigned i=0;i<stop&&!mutation->done;++i){advance();if(i+1<stop)supply();}
        uint64_t lba;void *p;if(tiku_mfs_mutation_io(mutation,&lba,&p)){assert(tiku_mfs_mutation_cancel(&fs,mutation)==TIKU_MFS_BUSY);supply();}
        unsigned done=mutation->done;assert(!tiku_mfs_mutation_cancel(&fs,mutation));if(!done)assert(fs.failed);reboot();preserved();assert(!upgrade());reboot();preserved();
    }
    printf("Directory migration: %u write/barrier cuts, %u cancellations and explicit recovery passed\n",count*3,steps+2);
}
int main(int argc,char **argv)
{
    stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);void *memory=malloc(tiku_mfs_mutation_size());
    assert(stable&&cache&&snapshot&&memory);mutation=tiku_mfs_mutation_init(memory,tiku_mfs_mutation_size());assert(mutation);tiku_mfs_init(&fs);
    if(argc==3&&!strcmp(argv[1],"emit")){mixed();FILE *f=fopen(argv[2],"wb");assert(f&&fwrite(stable,1,BYTES,f)==BYTES&&!fclose(f));}
    else if(argc==3&&!strcmp(argv[1],"resume")){
        FILE *f=fopen(argv[2],"rb");assert(f&&fread(stable,1,BYTES,f)==BYTES&&!fclose(f));reboot();preserved();
        tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"legacy-later",&o));assert(!upgrade());reboot();preserved();
        assert(!tiku_mfs_stat(&fs,"legacy-later",&o)&&tiku_mfs_has_directories(&fs));f=fopen(argv[2],"wb");assert(f&&fwrite(stable,1,BYTES,f)==BYTES&&!fclose(f));
    }else {assert(argc==1);behavior_upgrade();interrupted_upgrade();}
    free(memory);free(stable);free(cache);free(snapshot);return 0;
}
