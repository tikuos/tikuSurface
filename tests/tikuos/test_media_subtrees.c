/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_subtrees.c - atomic recursive namespace publication and recovery.
 * SPDX-License-Identifier: Apache-2.0
 */
#define TIKU_MEDIA_DIRECTORIES_ENABLE 1
#define SURFACE_MUTATION_QUEUE_FIXTURE 1
#include "test_media_mutation.c"
/** @brief Complete a sector-paced operation without synchronous block access. */
static int complete_tree(void)
{for(unsigned n=0;advance();++n){assert(n<1000000);supply();}return tiku_mfs_mutation_result(mutation);}
/** @brief Start a directory creation and return its durable object identity. */
static uint64_t dir(const char *name)
{assert(!tiku_mfs_mutation_mkdir_begin(&fs,mutation,name));assert(!complete_tree());return tiku_mfs_mutation_id(mutation);}
/** @brief Change one whole subtree, preserving the single-object API contract. */
static int tree(uint64_t id,const char *source,const char *target)
{int rc=tiku_mfs_mutation_tree_begin(&fs,mutation,id,source,target);return rc?rc:complete_tree();}
/** @brief Provision a private feature volume or a legacy implicit-folder volume. */
static uint64_t tree_fixture(unsigned typed)
{
    fresh();uint64_t id=0;
    if(typed){memset(stable,0,BYTES);memset(cache,0,BYTES);discard_disk();tiku_mfs_init(&fs);
        assert(!tiku_mfs_format_directories(&fs,&block,uuid,used,mark,sizeof used));id=dir("before");dir("before/empty");dir("before/nested");}
    create_file("before/a");create_file("before/nested/b");create_file("before0");create_file("neighbor");return id;
}
/** @brief Require complete source or target trees, never an individually published child. */
static void whole_tree(unsigned typed,unsigned remove)
{
    tiku_mfs_object_t o;int old=tiku_mfs_stat(&fs,"before/a",&o),now=tiku_mfs_stat(&fs,"after/a",&o);
    assert(old==0||old==TIKU_MFS_NOENT);assert(now==0||now==TIKU_MFS_NOENT);
    assert(remove?now==TIKU_MFS_NOENT:(!old)!=(now==0));
    assert(tiku_mfs_stat(&fs,"before/nested/b",&o)==old);
    assert(tiku_mfs_stat(&fs,"after/nested/b",&o)==now);
    if(typed){assert(tiku_mfs_stat(&fs,"before",&o)==old);if(!old)assert(o.policy==TIKU_MFS_DIRECTORY);
        assert(tiku_mfs_stat(&fs,"before/empty",&o)==old);assert(tiku_mfs_stat(&fs,"before/nested",&o)==old);
        assert(tiku_mfs_stat(&fs,"after",&o)==now);if(!now)assert(o.policy==TIKU_MFS_DIRECTORY);
        assert(tiku_mfs_stat(&fs,"after/empty",&o)==now);assert(tiku_mfs_stat(&fs,"after/nested",&o)==now);}
    assert(!tiku_mfs_stat(&fs,"neighbor",&o)&&o.length==3);assert(!tiku_mfs_stat(&fs,"before0",&o)&&o.length==3);
}
/** @brief Exercise namespace conflicts, original reader snapshots and complete-tree edits. */
static void behavior_tree(void)
{
    for(unsigned typed=0;typed<2;++typed){uint64_t id=tree_fixture(typed),reader;
        assert(!tiku_mfs_open_read(&fs,"before/nested/b",&reader));uint64_t sequence=fs.sequence;
        assert(!tree(id,"before","before")&&fs.sequence==sequence);
        assert(tree(id,"before","before/inside")==TIKU_MFS_BUSY);
        assert(tree(id,"before","neighbor/inside")==TIKU_MFS_BUSY);
        assert(tree(id,"before","neighbor")==TIKU_MFS_BUSY);
        create_file("occupied/file");assert(tree(id,"before","occupied")==TIKU_MFS_BUSY);
        assert(tree(0,"missing","target")==TIKU_MFS_NOENT);
        char longname[64];memset(longname,'x',60);longname[60]=0;unsigned writes_before=writes;
        assert(tree(id,"before",longname)==TIKU_MFS_LIMIT&&writes==writes_before&&!fs.failed);
        if(typed){assert(tree(0,"before","after")==TIKU_MFS_STALE);assert(change(id,"before",NULL)==TIKU_MFS_BUSY);}
        sequence=fs.sequence;assert(!tree(id,"before","after")&&fs.sequence==sequence+1);whole_tree(typed,0);
        assert(tiku_mfs_read_at(&fs,reader,0,out,3)==3&&!memcmp(out,"abc",3));assert(!tiku_mfs_close_read(&fs,reader));
        reboot();whole_tree(typed,0);sequence=fs.sequence;assert(!tree(id,"after",NULL)&&fs.sequence==sequence+1);
        tiku_mfs_object_t o;assert(tiku_mfs_stat(&fs,"after/nested/b",&o)==TIKU_MFS_NOENT);reboot();
        assert(!tiku_mfs_collect_begin(&fs));int rc;do{rc=tiku_mfs_collect_step(&fs,3);}while(rc>0);assert(!rc);reboot();
        assert(!tiku_mfs_stat(&fs,"neighbor",&o));
        id=tree_fixture(typed);uint64_t pending;uint8_t digest[32]={0};
        assert(!tiku_mfs_begin(&fs,"before/pending",TIKU_MFS_ATOMIC,1,digest,&pending));assert(!tiku_mfs_release(&fs));
        writes_before=writes;assert(tree(id,"before",NULL)==TIKU_MFS_BUSY&&writes==writes_before&&!fs.failed);
        assert(tree(id,"before","after")==TIKU_MFS_BUSY&&writes==writes_before&&!fs.failed);
    }
    uint64_t id=tree_fixture(1),file=create_file("ordinary");assert(tree(file,"ordinary",NULL)==TIKU_MFS_ARG);
    for(unsigned i=0;i<73;++i){char path[64];snprintf(path,sizeof path,"before/many/%03u",(i*37)%73);create_file(path);}
    uint64_t seq=fs.sequence;assert(!tree(id,"before","many")&&fs.sequence==seq+1);reboot();
    for(unsigned i=0;i<73;++i){char path[64];tiku_mfs_object_t o;snprintf(path,sizeof path,"many/many/%03u",i);assert(!tiku_mfs_stat(&fs,path,&o)&&o.length==3);}
    assert(!tree(id,"many",NULL));reboot();assert(fs.names.count==3);
}
/** @brief Cut every write/barrier and cancel at each state of representative subtrees. */
static void interrupted_tree(void)
{
    for(unsigned typed=0;typed<2;++typed)for(unsigned remove=0;remove<2;++remove){
        uint64_t id=tree_fixture(typed);memcpy(snapshot,stable,BYTES);reboot();events=0;
        assert(!tree(id,"before",remove?NULL:"after"));unsigned count=events;
        for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=count;++cut){
            memcpy(stable,snapshot,BYTES);reboot();events=0;fail_mode=mode;fail_at=cut;
            assert(tree(id,"before",remove?NULL:"after")==TIKU_MFS_IO);reboot();whole_tree(typed,remove);
        }
        memcpy(stable,snapshot,BYTES);reboot();assert(!tiku_mfs_mutation_tree_begin(&fs,mutation,id,"before",remove?NULL:"after"));
        unsigned steps=0;while(advance()){supply();++steps;}assert(!tiku_mfs_mutation_result(mutation));
        for(unsigned stop=0;stop<=steps+1;++stop){
            memcpy(stable,snapshot,BYTES);reboot();assert(!tiku_mfs_mutation_tree_begin(&fs,mutation,id,"before",remove?NULL:"after"));
            for(unsigned i=0;i<stop&&!mutation->done;++i){advance();if(i+1<stop)supply();}
            uint64_t lba;void *p;if(tiku_mfs_mutation_io(mutation,&lba,&p)){assert(tiku_mfs_mutation_cancel(&fs,mutation)==TIKU_MFS_BUSY);supply();}
            assert(!tiku_mfs_mutation_cancel(&fs,mutation));reboot();whole_tree(typed,remove);
        }
        printf("Subtree typed=%u remove=%u: %u cuts, %u cancellation positions\n",typed,remove,count*3,steps+2);
    }
}
#ifndef SURFACE_TREE_COPY_FIXTURE
#define SUBTREE_MAIN main
#else
#define SUBTREE_MAIN subtree_fixture_main
#endif
int SUBTREE_MAIN(void)
{
    stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);void *memory=malloc(tiku_mfs_mutation_size());
    assert(stable&&cache&&snapshot&&memory);mutation=tiku_mfs_mutation_init(memory,tiku_mfs_mutation_size());assert(mutation);
    behavior_tree();interrupted_tree();printf("Subtree transactions passed; owner=%zu bytes\n",tiku_mfs_mutation_size());
    free(memory);free(stable);free(cache);free(snapshot);return 0;
}
