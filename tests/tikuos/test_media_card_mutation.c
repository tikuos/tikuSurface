/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_card_mutation.c - card binding authorization and polled file jobs.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../platform/tikuos/media_io.h"
#define SURFACE_MUTATION_QUEUE_FIXTURE 1
#define service fixture_service
#include "test_media_mutation.c"
#undef service
#define PLATFORM_ESP32S31 1
#define TIKU_S31_SD_ASYNC_ENABLE 1
#include <services/storage/tiku_media_mutation.c>
#include <services/storage/tiku_media_async.c>
#include <services/storage/tiku_media_card.c>
static unsigned enabled,acquired,fail_enable,bad_arena,close_busy;
/** @brief Model the bitmap arena's retained backing. */
tiku_mem_err_t tiku_arena_stats(const tiku_arena_t *arena,tiku_mem_stats_t *stats)
{ assert(arena==&bitmap_arena);memset(stats,0,sizeof *stats);stats->used_bytes=bad_arena?0:2*bitmap_capacity;return TIKU_MEM_OK; }
/** @brief Acquire an initially read-only card session. */
int tiku_s31_mfs_async_acquire(tiku_s31_mfs_async_t *a,tiku_s31_mfs_block_t *b)
{
    assert(b==&backend&&!b->active);*a=(tiku_s31_mfs_async_t){.backend=b,.phase=1};b->active=2;++acquired;return 1;
}
/** @brief Enable writes after the simulated identity/extent check finishes. */
int tiku_s31_mfs_async_write_enable(tiku_s31_mfs_async_t *a)
{ assert(a->phase==3&&a->backend->active==2);++enabled;if(fail_enable)return TIKU_MFS_IO;a->backend->active=3;return 0; }
/** @brief Borrow a destination for one authorized read. */
int tiku_s31_mfs_async_read(tiku_s31_mfs_async_t *a,uint64_t lba,uint32_t n,void *p)
{ assert(a->phase==3&&n==1&&a->backend->active==3);a->lba=lba;a->output=p;a->phase=4;return 1; }
/** @brief Borrow a source for one authorized write. */
int tiku_s31_mfs_async_write(tiku_s31_mfs_async_t *a,uint64_t lba,uint32_t n,const void *p)
{ assert(a->phase==3&&n==1&&a->backend->active==3);a->lba=lba;a->output=(void *)p;a->phase=5;return 1; }
/** @brief Queue an ordered barrier under the write lease. */
int tiku_s31_mfs_async_sync(tiku_s31_mfs_async_t *a)
{ assert(a->phase==3&&a->backend->active==3);a->phase=6;return 1; }
/** @brief Retire one simulated transport phase. */
int tiku_s31_mfs_async_poll(tiku_s31_mfs_async_t *a)
{
    assert(a->phase&&a->phase!=3);int rc=a->phase==4?disk_read(NULL,a->lba,1,a->output):
        a->phase==5?disk_write(NULL,a->lba,1,a->output):a->phase==6?disk_sync(NULL):0;
    a->output=NULL;a->phase=3;
    if(rc){a->backend->active=0;a->backend=NULL;a->phase=0;return TIKU_MFS_IO;}return 0;
}
/** @brief Retain the entire owner until a refused close succeeds. */
int tiku_s31_mfs_async_close(tiku_s31_mfs_async_t *a)
{
    assert(a->backend&&a->backend->active);
    if(close_busy){--close_busy;return TIKU_MFS_BUSY;}
    a->backend->active=0;*a=(tiku_s31_mfs_async_t){0};return 0;
}
/** @brief Complete a file request through the real card and queue bindings. */
static void card_drain(void *state,tiku_media_request_t *r)
{
    for(unsigned i=0;!r->done;++i){assert(i<20000);assert(tiku_media_card_mutation_step(state,&service)>=0);}
    assert(tiku_media_card_mutation_idle(state)&&!backend.active);
}
int main(void)
{
    assert(tiku_media_card_read_size()<=SURFACE_MEDIA_READ_BYTES);
    assert(tiku_media_card_mutation_size()<=SURFACE_MEDIA_MUTATION_BYTES);
    printf("Card owner capacities: read=%zu/%u mutation=%zu/%u\n",tiku_media_card_read_size(),SURFACE_MEDIA_READ_BYTES,tiku_media_card_mutation_size(),SURFACE_MEDIA_MUTATION_BYTES);
    stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);
    void *state=malloc(tiku_media_card_mutation_size());assert(stable&&cache&&snapshot&&state);
    tiku_media_card_mutation_init(state);fresh();uint64_t id=create_file("before");
    service=(tiku_media_store_t){.fs=&fs,.backend=&backend};bitmap_capacity=sizeof used;
    tiku_media_request_t r={.op=TIKU_MEDIA_RENAME,.client=1,.permissions=TIKU_MEDIA_WRITE,.id=id,.name="before",.buffer="after"};
    assert(!tiku_media_store_submit(&service,&r));card_drain(state,&r);assert(!r.result&&enabled==1&&acquired==1);
    r.op=TIKU_MEDIA_DELETE_VERSION;r.name="after";assert(!tiku_media_store_submit(&service,&r));
    bad_arena=1;card_drain(state,&r);assert(r.result==TIKU_MFS_IO&&acquired==1&&!fs.failed);bad_arena=0;
    fail_enable=1;close_busy=2;assert(!tiku_media_store_submit(&service,&r));card_drain(state,&r);assert(r.result==TIKU_MFS_IO&&service.failed);
    reboot();service.failed=0;fail_enable=0;assert(!tiku_media_store_submit(&service,&r));card_drain(state,&r);assert(!r.result);
    reboot();tiku_mfs_object_t o;assert(tiku_mfs_stat(&fs,"after",&o)==TIKU_MFS_NOENT);
    free(state);free(stable);free(cache);free(snapshot);puts("Card mutations: bitmap admission, write authorization, queue integration and delayed release passed");return 0;
}
