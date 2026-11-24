/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_discard.c - removing unfinished uploads and renaming over a file.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_MUTATION_QUEUE_FIXTURE 1
#include "test_media_mutation.c"
/** @brief Run one paced mutation to its end. */
static int finish(int rc)
{
    if(rc)return rc;
    for(unsigned n=0;advance();++n){assert(n<20000);supply();}
    return tiku_mfs_mutation_result(mutation);
}
static int discard(uint64_t id,const char *name)
{ return finish(tiku_mfs_mutation_discard_begin(&fs,mutation,id,name)); }
static int rename_over(uint64_t id,const char *source,const char *target,uint64_t replaced)
{ return finish(tiku_mfs_mutation_rename_over_begin(&fs,mutation,id,source,target,replaced)); }
/** @brief A completed stream of @p bytes payload bytes named @p name. */
static uint64_t create_bytes(const char *name,uint32_t bytes)
{
    uint64_t id;assert(!tiku_mfs_begin(&fs,name,TIKU_MFS_STREAM,0,NULL,&id));
    for(uint32_t at=0;at<bytes;){uint32_t n=bytes-at>65536?65536:bytes-at;assert(!tiku_mfs_append(&fs,payload,n));at+=n;}
    assert(!tiku_mfs_finalize(&fs));return id;
}
/** @brief Name @p name's visible ID and length, or 0 when absent. */
static uint64_t visible(const char *name,uint64_t *length)
{
    tiku_mfs_object_t o;int rc=tiku_mfs_stat(&fs,name,&o);
    if(rc){assert(rc==TIKU_MFS_NOENT);return 0;}
    if(length)*length=o.length;
    return o.id;
}
/** @brief Discard a pending upload, keeping or not the name's visible file. */
static void discards(void)
{
    fresh();uint64_t doc=create_file("doc"),draft,stream,len;uint8_t hash[32]={0};
    assert(!tiku_mfs_begin(&fs,"doc",TIKU_MFS_ATOMIC,9,hash,&draft));
    assert(discard(draft,"doc")==TIKU_MFS_BUSY);
    assert(!tiku_mfs_release(&fs));
    assert(discard(draft,"other")==TIKU_MFS_STALE);
    assert(discard(doc,"doc")==TIKU_MFS_STALE);
    uint64_t epoch=fs.names_epoch;assert(!discard(draft,"doc"));assert(fs.names_epoch!=epoch);
    assert(visible("doc",&len)==doc&&len==3&&fs.ids.count==1&&fs.names.count==1&&!fs.names.pending);
    assert(discard(draft,"doc")!=0);
    assert(!tiku_mfs_begin(&fs,"doc",TIKU_MFS_ATOMIC,9,hash,&draft));assert(!tiku_mfs_release(&fs));
    reboot();assert(!discard(draft,"doc"));assert(visible("doc",NULL)==doc);
    assert(!tiku_mfs_begin(&fs,"new",TIKU_MFS_ATOMIC,9,hash,&draft));assert(!tiku_mfs_release(&fs));
    assert(!discard(draft,"new"));assert(!visible("new",NULL)&&fs.ids.count==1&&fs.names.count==1);
    assert(!tiku_mfs_begin(&fs,"part",TIKU_MFS_STREAM,0,NULL,&stream));
    assert(!tiku_mfs_append(&fs,payload,5000)&&!tiku_mfs_sync(&fs)&&!tiku_mfs_release(&fs));
    assert(visible("part",NULL)==stream);
    assert(!tiku_mfs_begin(&fs,"part",TIKU_MFS_ATOMIC,9,hash,&draft));assert(!tiku_mfs_release(&fs));
    assert(discard(stream,"part")==TIKU_MFS_STALE);assert(!discard(draft,"part"));
    assert(!discard(stream,"part"));assert(!visible("part",NULL));
    assert(discard(doc,"doc")==TIKU_MFS_STALE&&visible("doc",NULL)==doc);
    reboot();assert(visible("doc",NULL)==doc&&fs.ids.count==1&&fs.names.count==1&&!fs.names.pending);
}
/** @brief Rename a file over another in one transaction, and the refusals. */
static void renames(void)
{
    fresh();uint64_t doc=create_file("doc"),tmp=create_bytes("tmp",70000),other=create_file("other"),len,reader,draft;
    uint8_t hash[32]={0};
    assert(!tiku_mfs_open_read(&fs,"doc",&reader));
    assert(rename_over(tmp,"tmp","doc",0)==TIKU_MFS_BUSY);
    assert(rename_over(tmp,"tmp","doc",other)==TIKU_MFS_STALE);
    assert(rename_over(tmp,"tmp","absent",doc)==TIKU_MFS_STALE);
    assert(rename_over(tmp,"wrong","doc",doc)==TIKU_MFS_STALE);
    assert(rename_over(tmp,"tmp","doc/child",doc)==TIKU_MFS_STALE);
    assert(tiku_mfs_mutation_rename_over_begin(&fs,mutation,tmp,"tmp",NULL,doc)==TIKU_MFS_ARG);
    uint64_t epoch=fs.names_epoch,sequence=fs.sequence;
    assert(!rename_over(tmp,"tmp","doc",doc));
    assert(fs.sequence==sequence+1&&fs.names_epoch!=epoch);
    assert(visible("doc",&len)==tmp&&len==70000&&!visible("tmp",NULL)&&fs.ids.count==2&&fs.names.count==2);
    assert(tiku_mfs_read_at(&fs,reader,0,out,3)==3&&!memcmp(out,"abc",3));assert(!tiku_mfs_close_read(&fs,reader));
    reboot();assert(visible("doc",&len)==tmp&&len==70000&&visible("other",NULL)==other);
    assert(!tiku_mfs_open_read(&fs,"doc",&reader));
    assert(tiku_mfs_read_at(&fs,reader,65530,out,100)==100&&!memcmp(out,payload+65530-65536+65536,6));
    assert(!tiku_mfs_close_read(&fs,reader));
    uint64_t next=create_file("next");
    assert(rename_over(next,"next","doc",tmp)==0&&visible("doc",NULL)==next);
    uint64_t again=create_file("again");
    assert(!tiku_mfs_begin(&fs,"doc",TIKU_MFS_ATOMIC,9,hash,&draft));assert(!tiku_mfs_release(&fs));
    assert(rename_over(again,"again","doc",next)==TIKU_MFS_BUSY);
    assert(!discard(draft,"doc"));assert(!rename_over(again,"again","doc",next));
    assert(rename_over(draft,"doc","x",0)==TIKU_MFS_NOENT);
    assert(!rename_over(again,"doc","fresh",0)&&visible("fresh",NULL)==again);
}
#if TIKU_MEDIA_DIRECTORIES_ENABLE
/** @brief A directory is neither renamed over a file nor replaced by one. */
static void directories(void)
{
    memset(stable,0,BYTES);memset(cache,0,BYTES);offline=fail_at=0;discard_disk();
    tiku_mfs_init(&fs);assert(!tiku_mfs_format_directories(&fs,&block,uuid,used,mark,sizeof used));
    assert(!finish(tiku_mfs_mutation_mkdir_begin(&fs,mutation,"folder")));
    uint64_t folder=visible("folder",NULL),file=create_file("file"),other=create_file("other");
    assert(folder&&rename_over(file,"file","folder",folder)==TIKU_MFS_BUSY);
    assert(rename_over(folder,"folder","other",other)==TIKU_MFS_BUSY);
    assert(visible("folder",NULL)==folder&&visible("file",NULL)==file&&visible("other",NULL)==other);
    assert(!rename_over(file,"file","other",other)&&visible("other",NULL)==file);
}
#endif
/** @brief After a cut at each write, a discard has happened or not. */
static void discard_cuts(void)
{
    fresh();uint64_t doc=create_file("doc"),draft;uint8_t hash[32]={0};
    assert(!tiku_mfs_begin(&fs,"doc",TIKU_MFS_ATOMIC,9,hash,&draft));assert(!tiku_mfs_release(&fs));
    memcpy(snapshot,stable,BYTES);reboot();events=0;assert(!discard(draft,"doc"));unsigned count=events;
    for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=count;++cut){
        memcpy(stable,snapshot,BYTES);reboot();events=0;fail_mode=mode;fail_at=cut;
        assert(discard(draft,"doc")==TIKU_MFS_IO&&fs.failed);reboot();
        assert(visible("doc",NULL)==doc&&fs.ids.count==fs.names.count+fs.names.pending);
        if(fs.names.pending)assert(!discard(draft,"doc"));
        assert(!fs.names.pending&&fs.ids.count==1);
    }
    printf("Discard: recovered across %u write/barrier cuts\n",count*3);
}
/** @brief After a cut at each write, a rename over has happened or not. */
static void rename_cuts(void)
{
    fresh();uint64_t doc=create_file("doc"),tmp=create_bytes("tmp",4097),len;
    memcpy(snapshot,stable,BYTES);reboot();events=0;assert(!rename_over(tmp,"tmp","doc",doc));unsigned count=events;
    for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=count;++cut){
        memcpy(stable,snapshot,BYTES);reboot();events=0;fail_mode=mode;fail_at=cut;
        assert(rename_over(tmp,"tmp","doc",doc)==TIKU_MFS_IO&&fs.failed);reboot();
        uint64_t now=visible("doc",&len);
        if(now==doc)assert(len==3&&visible("tmp",NULL)==tmp&&fs.ids.count==2);
        else assert(now==tmp&&len==4097&&!visible("tmp",NULL)&&fs.ids.count==1);
    }
    printf("Rename over: recovered across %u write/barrier cuts\n",count*3);
}

int main(void)
{
    stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);
    void *memory=malloc(tiku_mfs_mutation_size());assert(stable&&cache&&snapshot&&memory);
    mutation=tiku_mfs_mutation_init(memory,tiku_mfs_mutation_size());assert(mutation);
    for(unsigned i=0;i<sizeof payload;++i)payload[i]=(uint8_t)(i*37+11);
    discards();renames();discard_cuts();rename_cuts();
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    directories();
#endif
    printf("Discard and rename over: pending uploads, unfinished streams, replaced targets passed\n");
    free(memory);free(stable);free(cache);free(snapshot);return 0;
}
