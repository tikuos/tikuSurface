/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_directories.c - typed directories and sector-paced publication.
 * SPDX-License-Identifier: Apache-2.0
 */
#define TIKU_MEDIA_DIRECTORIES_ENABLE 1
#define SURFACE_MUTATION_QUEUE_FIXTURE 1
#include "test_media_mutation.c"
/** @brief Provision a private directory-enabled extent without adopting another volume. */
static void fresh_directories(void)
{
    memset(stable,0,BYTES);memset(cache,0,BYTES);offline=fail_at=0;discard_disk();
    tiku_mfs_init(&fs);assert(!tiku_mfs_format_directories(&fs,&block,uuid,used,mark,sizeof used));
    assert(tiku_mfs_has_directories(&fs));
}
/** @brief Complete one admitted operation under the metadata pacing guard. */
static int complete(void)
{for(unsigned i=0;advance();++i){assert(i<30000);supply();}return tiku_mfs_mutation_result(mutation);}
/** @brief Create an empty directory and return its transaction result. */
static int directory(const char *name)
{int rc=tiku_mfs_mutation_mkdir_begin(&fs,mutation,name);return rc?rc:complete();}
/** @brief Create or replace a small file using the bounded content operation. */
static int content(uint64_t id,const char *name)
{int rc=tiku_mfs_mutation_replace_begin(&fs,mutation,id,name,"data",4);return rc?rc:complete();}
/** @brief Check persistent identities, file/directory conflicts and nonempty refusal. */
static void directory_behavior(void)
{
    fresh();assert(!tiku_mfs_has_directories(&fs));assert(directory("legacy")==TIKU_MFS_UNSUPPORTED);
    fresh_directories();assert(directory("../bad")==TIKU_MFS_ARG);assert(!directory("folder"));
    tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"folder",&o)&&o.policy==TIKU_MFS_DIRECTORY&&!o.length&&o.complete&&o.visible);
    uint64_t folder=o.id,token;
    assert(change(folder,"folder","folder/inside")==TIKU_MFS_BUSY);
    assert(tiku_mfs_rename(&fs,folder,"folder","folder/inside")==TIKU_MFS_BUSY);assert(tiku_mfs_open_read(&fs,"folder",&token)==TIKU_MFS_ARG);
    assert(directory("folder")==TIKU_MFS_BUSY&&content(folder,"folder")==TIKU_MFS_BUSY);
    uint64_t writer;uint8_t hash[32]={0};assert(tiku_mfs_begin(&fs,"folder",TIKU_MFS_ATOMIC,0,hash,&writer)==TIKU_MFS_BUSY);
    uint64_t file=create_file("folder/file");assert(!content(file,"folder/file"));assert(!directory("folder/empty"));
    assert(tiku_mfs_delete(&fs,folder)==TIKU_MFS_BUSY&&tiku_mfs_delete_version(&fs,folder,"folder")==TIKU_MFS_BUSY);
    assert(tiku_mfs_rename(&fs,folder,"folder","moved")==TIKU_MFS_BUSY);
    assert(change(folder,"folder",NULL)==TIKU_MFS_BUSY&&change(folder,"folder","moved")==TIKU_MFS_BUSY);
    assert(!change(folder,"folder","folder"));
    assert(!tiku_mfs_stat(&fs,"folder/empty",&o));uint64_t empty=o.id;
    assert(!change(empty,"folder/empty","empty"));assert(!change(empty,"empty",NULL));
    assert(!tiku_mfs_stat(&fs,"folder/file",&o));assert(!tiku_mfs_open_read(&fs,"folder/file",&token));
    assert(!change(o.id,"folder/file",NULL));assert(!change(folder,"folder","moved"));
    assert(tiku_mfs_read_at(&fs,token,0,out,4)==4&&!memcmp(out,"data",4));assert(!tiku_mfs_close_read(&fs,token));
    reboot();assert(tiku_mfs_has_directories(&fs)&&!tiku_mfs_stat(&fs,"moved",&o)&&o.id==folder&&o.policy==TIKU_MFS_DIRECTORY);
    assert(!tiku_mfs_checkpoint(&fs));reboot();assert(tiku_mfs_has_directories(&fs));
    assert(!tiku_mfs_collect_begin(&fs));int rc;do{rc=tiku_mfs_collect_step(&fs,3);}while(rc>0);assert(!rc);reboot();
    assert(!tiku_mfs_stat(&fs,"moved",&o)&&o.policy==TIKU_MFS_DIRECTORY);
    assert(!change(folder,"moved",NULL));assert(tiku_mfs_stat(&fs,"moved",&o)==TIKU_MFS_NOENT);
    uint64_t parent=create_file("parent");assert(directory("parent/child")==TIKU_MFS_BUSY);assert(!tiku_mfs_delete(&fs,parent));
    assert(!directory("dir"));assert(!tiku_mfs_stat(&fs,"dir",&o));
    assert(!tiku_mfs_begin(&fs,"dir/pending",TIKU_MFS_ATOMIC,1,hash,&writer));assert(!tiku_mfs_release(&fs));
    assert(change(o.id,"dir",NULL)==TIKU_MFS_BUSY);assert(!tiku_mfs_delete(&fs,writer));assert(!change(o.id,"dir",NULL));
}
/** @brief Recover directory create, rename and delete at each ordered write cut. */
static void directory_cuts(void)
{
    for(unsigned operation=0;operation<3;++operation){
        fresh_directories();uint64_t id=0;if(operation){assert(!directory("before"));tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"before",&o));id=o.id;}
        create_file("neighbor");memcpy(snapshot,stable,BYTES);reboot();events=0;
        assert(!(operation?change(id,"before",operation==1?"after":NULL):directory("after")));unsigned count=events;
        for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=count;++cut){
            memcpy(stable,snapshot,BYTES);reboot();events=0;fail_mode=mode;fail_at=cut;
            assert((operation?change(id,"before",operation==1?"after":NULL):directory("after"))==TIKU_MFS_IO);reboot();
            tiku_mfs_object_t a,b;int before=tiku_mfs_stat(&fs,"before",&a),after=tiku_mfs_stat(&fs,"after",&b);
            if(operation==1)assert((!before&&after==TIKU_MFS_NOENT)||(before==TIKU_MFS_NOENT&&!after));
            else if(operation==2)assert(after==TIKU_MFS_NOENT&&(!before||before==TIKU_MFS_NOENT));
            else assert(before==TIKU_MFS_NOENT&&(!after||after==TIKU_MFS_NOENT));
            if(!before)assert(a.policy==TIKU_MFS_DIRECTORY&&a.id==id);
            if(!after)assert(b.policy==TIKU_MFS_DIRECTORY);
            assert(tiku_mfs_has_directories(&fs)&&!tiku_mfs_stat(&fs,"neighbor",&a)&&a.length==3);
        }
        printf("Directories: operation %u recovered at %u write/barrier cuts\n",operation,count*3);
    }
}
/** @brief Cancel directory publication while retaining any outstanding sector. */
static void directory_cancellations(void)
{
    fresh_directories();create_file("neighbor");memcpy(snapshot,stable,BYTES);
    for(unsigned stop=0;stop<180;++stop){
        memcpy(stable,snapshot,BYTES);reboot();assert(!tiku_mfs_mutation_mkdir_begin(&fs,mutation,"cancelled"));
        for(unsigned i=0;i<stop&&!mutation->done;++i){advance();if(i+1<stop)supply();}
        unsigned changed=mutation->changed,done=mutation->done;
        uint64_t lba;void *p;if(tiku_mfs_mutation_io(mutation,&lba,&p)){
            assert(tiku_mfs_mutation_cancel(&fs,mutation)==TIKU_MFS_BUSY);supply();
        }
        assert(!tiku_mfs_mutation_cancel(&fs,mutation));if(changed&&!done)assert(fs.failed);
        reboot();tiku_mfs_object_t o;int rc=tiku_mfs_stat(&fs,"cancelled",&o);
        assert(rc==TIKU_MFS_NOENT||(!rc&&o.policy==TIKU_MFS_DIRECTORY));
        assert(!tiku_mfs_stat(&fs,"neighbor",&o)&&o.length==3);
    }
}
/** @brief Unknown mandatory features cannot be identified or mounted. */
static void features(void)
{
    fresh_directories();uint8_t found[16];assert(!tiku_mfs_identify(&block,found)&&!memcmp(found,uuid,16));
    for(unsigned i=0;i<2;++i){uint8_t *p=stable+i*fs.bank_sectors*512;put32(p+88,2);seal(p);}
    memcpy(cache,stable,BYTES);assert(tiku_mfs_identify(&block,found)==TIKU_MFS_NOFS);
    assert(tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used)==TIKU_MFS_NOFS);
}
int main(int argc,char **argv)
{
    stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);void *memory=malloc(tiku_mfs_mutation_size());
    assert(stable&&cache&&snapshot&&memory);mutation=tiku_mfs_mutation_init(memory,tiku_mfs_mutation_size());assert(mutation);
    directory_behavior();directory_cuts();directory_cancellations();features();
    if(argc==2){fresh_directories();assert(!directory("persisted"));FILE *f=fopen(argv[1],"wb");assert(f);assert(fwrite(stable,1,BYTES,f)==BYTES&&!fclose(f));}
    printf("Directories: persistent metadata, namespace, readers, legacy refusal and recovery passed; mount=%zu mutation=%zu\n",sizeof fs,tiku_mfs_mutation_size());
    free(memory);free(stable);free(cache);free(snapshot);return 0;
}
