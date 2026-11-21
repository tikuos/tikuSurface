/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_rename.c - atomic names, retained readers and interrupted publication.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main mfs_fixture_main
#include <TikuBench/tests/host/kernel/test_mfs.c>
#undef main
#include <assert.h>
/** @brief Create a completed three-byte file through the real filesystem. */
static uint64_t file(const char *name)
{
    uint64_t id;assert(!tiku_mfs_begin(&fs,name,TIKU_MFS_STREAM,0,NULL,&id));
    assert(!tiku_mfs_append(&fs,"abc",3));assert(!tiku_mfs_finalize(&fs));return id;
}
/** @brief Check name ownership, conflicts, readers, listing and stale callers. */
static void names(void)
{
    fresh();uint64_t id=file("old/a.txt"),other=file("target.txt"),reader;
    tiku_mfs_object_t o;assert(!tiku_mfs_open_read(&fs,"old/a.txt",&reader));
    tiku_mfs_cursor_t cursor={0};assert(!tiku_mfs_list(&fs,&cursor,&o));
    uint64_t sequence=fs.sequence;
#if TIKU_MEDIA_NAMES_ENABLE
    uint64_t epoch=tiku_mfs_names_epoch(&fs);
#endif
    unsigned before=writes;
    assert(tiku_mfs_rename(&fs,id,"old/a.txt","target.txt")==TIKU_MFS_BUSY);
    assert(tiku_mfs_rename(&fs,id,"old/a.txt","target.txt/child")==TIKU_MFS_BUSY);
    assert(tiku_mfs_rename(&fs,other,"target.txt","old")==TIKU_MFS_BUSY);
    assert(tiku_mfs_rename(&fs,id,"wrong","new.txt")==TIKU_MFS_STALE);
    assert(tiku_mfs_rename(&fs,id,"old/a.txt","../new.txt")==TIKU_MFS_ARG);
    assert(!tiku_mfs_rename(&fs,id,"old/a.txt","old/a.txt") && writes==before && fs.sequence==sequence);
    assert(!tiku_mfs_rename(&fs,id,"old/a.txt","new/b.txt"));
    assert(fs.sequence==sequence+1 && tiku_mfs_list(&fs,&cursor,&o)==TIKU_MFS_STALE);
#if TIKU_MEDIA_NAMES_ENABLE
    assert(tiku_mfs_names_epoch(&fs)!=epoch);
#endif
    assert(tiku_mfs_stat(&fs,"old/a.txt",&o)==TIKU_MFS_NOENT);
    assert(!tiku_mfs_stat(&fs,"new/b.txt",&o) && o.id==id && o.length==3 && o.complete);
    assert(tiku_mfs_read_at(&fs,reader,0,out,3)==3 && !memcmp(out,"abc",3));
    assert(!tiku_mfs_close_read(&fs,reader));
    assert(tiku_mfs_rename(&fs,id,"old/a.txt","other.txt")==TIKU_MFS_STALE);
    reboot();assert(!tiku_mfs_stat(&fs,"new/b.txt",&o) && o.id==id);
    assert(!tiku_mfs_open_read(&fs,"new/b.txt",&reader));
    assert(tiku_mfs_read_at(&fs,reader,0,out,3)==3 && !memcmp(out,"abc",3));
    assert(!tiku_mfs_close_read(&fs,reader));
    uint64_t draft;assert(!tiku_mfs_begin(&fs,"writing",TIKU_MFS_STREAM,0,NULL,&draft));
    assert(tiku_mfs_rename(&fs,id,"new/b.txt","later")==TIKU_MFS_BUSY);
    assert(!tiku_mfs_release(&fs));
    assert(tiku_mfs_rename(&fs,draft,"writing","later")==TIKU_MFS_STALE);
    uint8_t digest[32]={0};
    assert(!tiku_mfs_begin(&fs,"new/b.txt",TIKU_MFS_ATOMIC,1,digest,&draft));
    assert(!tiku_mfs_release(&fs));
    assert(tiku_mfs_rename(&fs,id,"new/b.txt","later")==TIKU_MFS_BUSY);
    assert(tiku_mfs_rename(&fs,other,"target.txt","new/b.txt")==TIKU_MFS_BUSY);
}
/** @brief Assert every injected publication cut recovers exactly one complete name. */
static void cuts(void)
{
    fresh();uint64_t id=file("source");(void)file("neighbor");
    assert(!tiku_mfs_checkpoint(&fs));memcpy(snapshot,stable,BYTES);
    reboot();events=0;assert(!tiku_mfs_rename(&fs,id,"source","dest"));unsigned count=events;
    for(unsigned mode=0;mode<3;++mode) for(unsigned cut=1;cut<=count;++cut) {
        memcpy(stable,snapshot,BYTES);reboot();events=0;fail_mode=mode;fail_at=cut;
        assert(tiku_mfs_rename(&fs,id,"source","dest")==TIKU_MFS_IO);reboot();
        tiku_mfs_object_t old,new,neighbor;
        int a=tiku_mfs_stat(&fs,"source",&old),b=tiku_mfs_stat(&fs,"dest",&new);
        assert((!a && b==TIKU_MFS_NOENT)||(a==TIKU_MFS_NOENT && !b));
        assert((!a?old.id:new.id)==id && (!a?old.length:new.length)==3);
        assert(!tiku_mfs_stat(&fs,"neighbor",&neighbor));
        uint64_t reader;assert(!tiku_mfs_open_read(&fs,!a?"source":"dest",&reader));
        assert(tiku_mfs_read_at(&fs,reader,0,out,3)==3 && !memcmp(out,"abc",3));
        assert(!tiku_mfs_close_read(&fs,reader));
    }
    printf("Rename: %u publication cuts across three fault modes\n",count*3);
}
/** @brief Exercise trusted permissions and borrowed-name queue completion. */
static void queued(void)
{
    fresh();uint64_t id=file("before");tiku_media_store_t store;
    assert(!tiku_media_store_init(&store,&fs,&block,uuid,used,mark,sizeof used,NULL,NULL,NULL));
    assert(!tiku_mfs_mount(&fs,&block,uuid,used,mark,sizeof used));
    char destination[]="after";
    tiku_media_request_t r={.op=TIKU_MEDIA_RENAME,.client=17,.permissions=TIKU_MEDIA_READ,
        .id=id,.name="before",.buffer=destination};
    assert(tiku_media_store_submit(&store,&r)==TIKU_MFS_ARG && !store.count);
    r.permissions=TIKU_MEDIA_WRITE;assert(!tiku_media_store_submit(&store,&r) && !r.done);
    assert(tiku_media_store_step(&store)==1 && r.done && !r.result);
    tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"after",&o) && o.id==id);
    assert(!tiku_media_store_submit(&store,&r));assert(tiku_media_store_step(&store)==1 && r.result==TIKU_MFS_STALE);
}
int main(void)
{
    stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);assert(stable&&cache&&snapshot);
    names();cuts();queued();free(stable);free(cache);free(snapshot);
    puts("Rename: atomic publication, conflicts, readers, permissions and stale commands passed");return 0;
}
