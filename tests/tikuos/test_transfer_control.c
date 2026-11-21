/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_transfer_control.c - command retention and media-backed cleanup bounds.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <TikuBench/tests/host/kernel/test_media_transfer.c>
#include <services/storage/tiku_media_transfer_control.c>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/** @brief Backend fixture counts callbacks and bounds simulated session clearing. */
typedef struct {
    tiku_mt_control_t *control;
    unsigned owner, calls, phases[6], busy_prepare, reenter, use_store;
    int fail_phase, fail_result;
    unsigned prepared, attached;
    uint8_t data[1537];
    size_t cursor;
} backend_t;
/** @brief Model a serialized service-owner predicate without hardware access. */
static int owner(void *context) { return ((backend_t *)context)->owner; }
/** @brief Perform one counted backend action, including real store handle release. */
static int backend_step(void *context,tiku_mt_control_phase_t phase,const tiku_mt_control_profile_t *profile)
{
    backend_t *b=context; ++b->calls; ++b->phases[phase];
    assert(profile->port==7331 && profile->key[0]==7 && profile->volume[0]==1);
    if (b->reenter) {
        uint64_t token=999;
        assert(tiku_mt_control_submit(b->control,TIKU_MT_CONTROL_STOP,1,&token)==TIKU_MFS_BUSY && token==999);
        assert(tiku_mt_control_step(b->control)==TIKU_MFS_BUSY);
    }
    if (b->fail_phase==(int)phase) { b->fail_phase=-1; return b->fail_result; }
    if (phase==TIKU_MT_CONTROL_PREPARE) {
        if (!b->prepared) { b->cursor=0; memset(b->data,0xa5,sizeof b->data); }
        b->prepared=1;
        if (b->busy_prepare) { --b->busy_prepare; return TIKU_MT_CONTROL_PENDING; }
    } else if (phase==TIKU_MT_CONTROL_ATTACH) b->attached=1;
    else if (phase==TIKU_MT_CONTROL_DETACH) b->attached=0;
    else if (phase==TIKU_MT_CONTROL_RELEASE && b->use_store) return tiku_mt_control_release_one(&store,42);
    else if (phase==TIKU_MT_CONTROL_WIPE) {
        size_t before=b->cursor;
        int rc=tiku_mt_control_wipe(b->data,sizeof b->data,&b->cursor);
        assert(b->cursor-before<=512);
        for(size_t i=0;i<b->cursor;i++)assert(!b->data[i]);
        for(size_t i=b->cursor;i<sizeof b->data;i++)assert(b->data[i]==0xa5);
        if(!rc)b->prepared=0;
        return rc;
    }
    return 0;
}
/** @brief Initialize a private controller and externally provisioned profile. */
static tiku_mt_control_t *create(backend_t *b,uint64_t *generation)
{
    *b=(backend_t){.owner=1,.fail_phase=-1};memset(b->data,0xa5,sizeof b->data);
    void *memory=malloc(tiku_mt_control_size());assert(memory);
    b->control=tiku_mt_control_init(memory,tiku_mt_control_size(),(tiku_mt_control_ops_t){owner,backend_step},b);
    assert(b->control);
    tiku_mt_control_profile_t p={.port=7331,.key={7},.volume={1}};
    assert(!tiku_mt_control_configure(b->control,&p,generation));assert(*generation==1 && !b->calls);
    return b->control;
}
/** @brief Advance a finite fixture while checking one callback per owner turn. */
static void drain(backend_t *b)
{
    for(unsigned i=0;i<64;i++) {
        unsigned before=b->calls;
        int rc=tiku_mt_control_step(b->control);assert(b->calls-before<=1);
        assert(rc==0 || rc==TIKU_MT_CONTROL_PENDING);
        if(!rc)return;
    }
    assert(0);
}
/** @brief Consume exactly one retained terminal result. */
static void take(tiku_mt_control_t *c,uint64_t token,int expected)
{
    int result=123;assert(!tiku_mt_control_take(c,token,&result) && result==expected);
    assert(tiku_mt_control_take(c,token,&result)==TIKU_MFS_STALE);
}
/** @brief Refusal, observations, admission and pending cancellation perform no hidden work. */
static void commands(void)
{
    backend_t b;uint64_t generation,enable,stop,other=999;
    tiku_mt_control_t *c=create(&b,&generation);
    assert(tiku_mt_control_submit(c,TIKU_MT_CONTROL_ENABLE,generation+1,&other)==TIKU_MFS_STALE && other==999);
    b.owner=0;
    assert(tiku_mt_control_submit(c,TIKU_MT_CONTROL_ENABLE,generation,&other)==TIKU_MT_CONTROL_OWNER);
    tiku_mt_control_snapshot_t snapshot={.generation=999};
    assert(tiku_mt_control_observe(c,&snapshot)==TIKU_MT_CONTROL_OWNER && snapshot.generation==999);
    b.owner=1;
    assert(!tiku_mt_control_submit(c,TIKU_MT_CONTROL_ENABLE,generation,&enable));
    tiku_mt_control_profile_t replacement={.port=9000,.key={8},.volume={2}};
    assert(tiku_mt_control_configure(c,&replacement,&other)==TIKU_MFS_BUSY && other==999);
    int result=123;assert(tiku_mt_control_take(c,enable,&result)==TIKU_MT_CONTROL_PENDING && result==123);
    assert(!tiku_mt_control_observe(c,&snapshot) && snapshot.enabling==enable && !b.calls);
    assert(!tiku_mt_control_submit(c,TIKU_MT_CONTROL_STOP,generation,&stop));
    assert(!tiku_mt_control_submit(c,TIKU_MT_CONTROL_STOP,generation,&other) && other==stop && !b.calls);
    b.reenter=1;drain(&b);
    assert(!b.phases[TIKU_MT_CONTROL_PREPARE] && !b.phases[TIKU_MT_CONTROL_ATTACH]);
    assert(b.phases[TIKU_MT_CONTROL_WIPE]==4);
    assert(!tiku_mt_control_observe(c,&snapshot) && snapshot.state==TIKU_MT_CONTROL_STOPPED && !snapshot.cleanup_required);
    assert(tiku_mt_control_submit(c,TIKU_MT_CONTROL_ENABLE,generation,&other)==TIKU_MFS_BUSY);
    assert(tiku_mt_control_forget(c)==TIKU_MFS_BUSY);
    take(c,enable,TIKU_MT_CONTROL_CANCELLED);take(c,stop,0);
    assert(!tiku_mt_control_forget(c));
    assert(!memcmp(&c->profile,&(tiku_mt_control_profile_t){0},sizeof c->profile));
    assert(tiku_mt_control_submit(c,TIKU_MT_CONTROL_ENABLE,generation,&other)==TIKU_MFS_NOFS);
    free(c);
}
/** @brief Stop drains a pending backend preparation before publishing either completion. */
static void pending_enable(void)
{
    backend_t b;uint64_t generation,enable,stop;
    tiku_mt_control_t *c=create(&b,&generation);b.busy_prepare=4;
    assert(!tiku_mt_control_submit(c,TIKU_MT_CONTROL_ENABLE,generation,&enable));
    assert(tiku_mt_control_step(c)==TIKU_MT_CONTROL_PENDING && b.prepared && !b.attached);
    assert(!tiku_mt_control_submit(c,TIKU_MT_CONTROL_STOP,generation,&stop));
    drain(&b);assert(b.phases[TIKU_MT_CONTROL_PREPARE]==1 && !b.phases[TIKU_MT_CONTROL_ATTACH]);
    assert(!b.prepared && !b.attached);
    take(c,enable,TIKU_MT_CONTROL_CANCELLED);take(c,stop,0);free(c);
}
/** @brief Backend failures retain ownership until an explicit successful Stop retry. */
static void failures(void)
{
    backend_t b;uint64_t generation,enable,stop;
    tiku_mt_control_t *c=create(&b,&generation);
    assert(!tiku_mt_control_submit(c,TIKU_MT_CONTROL_ENABLE,generation,&enable));drain(&b);take(c,enable,0);
    b.fail_phase=TIKU_MT_CONTROL_DETACH;b.fail_result=TIKU_MFS_IO;
    assert(!tiku_mt_control_submit(c,TIKU_MT_CONTROL_STOP,generation,&stop));drain(&b);take(c,stop,TIKU_MFS_IO);
    tiku_mt_control_snapshot_t snapshot;
    assert(!tiku_mt_control_observe(c,&snapshot) && snapshot.state==TIKU_MT_CONTROL_FAILED && snapshot.cleanup_required);
    assert(b.attached && !b.cursor);
    assert(tiku_mt_control_forget(c)==TIKU_MFS_BUSY);
    assert(tiku_mt_control_submit(c,TIKU_MT_CONTROL_ENABLE,generation,&enable)==TIKU_MFS_BUSY);
    assert(!tiku_mt_control_submit(c,TIKU_MT_CONTROL_STOP,generation,&stop));drain(&b);take(c,stop,0);
    assert(!b.attached && !b.prepared);free(c);
    c=create(&b,&generation);b.fail_phase=TIKU_MT_CONTROL_ATTACH;b.fail_result=TIKU_MFS_SPACE;
    assert(!tiku_mt_control_submit(c,TIKU_MT_CONTROL_ENABLE,generation,&enable));drain(&b);
    take(c,enable,TIKU_MFS_SPACE);assert(!b.prepared && !b.attached);
    assert(!tiku_mt_control_observe(c,&snapshot) && snapshot.state==TIKU_MT_CONTROL_STOPPED && !snapshot.cleanup_required);
    free(c);
}
/** @brief Provisioning and token exhaustion refuse reuse while reserving a final Stop. */
static void generations(void)
{
    backend_t b;uint64_t generation,enable,stop,unchanged=123;
    tiku_mt_control_t *c=create(&b,&generation);
    tiku_mt_control_profile_t p={.port=7331,.key={7},.volume={1}},old=c->profile;
    assert(tiku_mt_control_configure(c,&(tiku_mt_control_profile_t){.port=7331},&unchanged)==TIKU_MFS_ARG && unchanged==123);
    assert(!memcmp(&old,&c->profile,sizeof old));
    tiku_mt_control_profile_t bad=p;memset(bad.key,0,sizeof bad.key);
    assert(tiku_mt_control_configure(c,&bad,&unchanged)==TIKU_MFS_ARG);
    bad=p;memset(bad.volume,0,sizeof bad.volume);
    assert(tiku_mt_control_configure(c,&bad,&unchanged)==TIKU_MFS_ARG);
    bad=p;bad.port=0;assert(tiku_mt_control_configure(c,&bad,&unchanged)==TIKU_MFS_ARG);
    assert(!tiku_mt_control_configure(c,&p,&generation) && generation==2);
    assert(tiku_mt_control_submit(c,TIKU_MT_CONTROL_ENABLE,1,&unchanged)==TIKU_MFS_STALE);
    c->sequence=UINT64_MAX-1;
    assert(tiku_mt_control_submit(c,TIKU_MT_CONTROL_ENABLE,generation,&unchanged)==TIKU_MFS_LIMIT && unchanged==123);
    c->sequence=UINT64_MAX-2;
    assert(!tiku_mt_control_submit(c,TIKU_MT_CONTROL_ENABLE,generation,&enable) && enable==UINT64_MAX-1);
    assert(!tiku_mt_control_submit(c,TIKU_MT_CONTROL_STOP,generation,&stop) && stop==UINT64_MAX);
    drain(&b);take(c,enable,TIKU_MT_CONTROL_CANCELLED);take(c,stop,0);
    assert(tiku_mt_control_submit(c,TIKU_MT_CONTROL_ENABLE,generation,&unchanged)==TIKU_MFS_LIMIT && unchanged==123);
    c->generation=UINT64_MAX;
    assert(tiku_mt_control_configure(c,&p,&unchanged)==TIKU_MFS_LIMIT && unchanged==123);
    assert(!tiku_mt_control_forget(c) && !c->profile.port && !c->profile.key[0]);
    assert(tiku_mt_control_configure(c,&p,&unchanged)==TIKU_MFS_LIMIT);
    free(c);
}
/** @brief Submit an independent store request through its normal serialized owner. */
static void request(tiku_media_request_t *r)
{ assert(!tiku_media_store_submit(&store,r));assert(tiku_media_store_step(&store)==1 && r->done && !r->result); }
/** @brief Preserve other clients, queued work and durable upload prefixes during cleanup. */
static void media_cleanup(void)
{
    assert(!reset(NULL));uint8_t payload[64];memset(payload,0x7b,sizeof payload);
    uint64_t id;assert(!tiku_mfs_begin(&fs,"readme",TIKU_MFS_STREAM,0,NULL,&id));
    assert(!tiku_mfs_append(&fs,payload,sizeof payload) && !tiku_mfs_finalize(&fs));
    uint64_t original_id=id;
    tiku_media_request_t r={.client=42,.permissions=7,.op=TIKU_MEDIA_OPEN,.name="readme"};request(&r);
    r.op=TIKU_MEDIA_OPEN;request(&r);r.client=43;request(&r);uint64_t other=r.id;
    r=(tiku_media_request_t){.client=42,.permissions=7,.op=TIKU_MEDIA_BEGIN,.name="readme",.policy=TIKU_MFS_ATOMIC,.offset=128};request(&r);id=r.id;
    memset(payload,0x88,sizeof payload);
    r.op=TIKU_MEDIA_APPEND;r.buffer=payload;r.length=sizeof payload;request(&r);
    r.op=TIKU_MEDIA_SYNC;request(&r);
    r.op=TIKU_MEDIA_APPEND;request(&r);
    tiku_mfs_object_t info;
    tiku_media_request_t queued_request={.client=43,.permissions=7,.op=TIKU_MEDIA_STAT,.name="readme",.info=&info};
    assert(!tiku_media_store_submit(&store,&queued_request));
    unsigned before_reads=reads,before_writes=writes;
    assert(tiku_mt_control_release_one(&store,42)==TIKU_MFS_BUSY);
    assert(store.count==1 && store.queue[0]==&queued_request && !queued_request.done && store.writer==42);
    assert(reads==before_reads && writes==before_writes);
    assert(tiku_media_store_step(&store)==1 && !queued_request.result);
    store.running=1;assert(tiku_mt_control_release_one(&store,42)==TIKU_MFS_BUSY);store.running=0;
    before_reads=reads;before_writes=writes;
    backend_t b;uint64_t generation,enable,stop;tiku_mt_control_t *c=create(&b,&generation);
    b.use_store=1;
    assert(!tiku_mt_control_submit(c,TIKU_MT_CONTROL_ENABLE,generation,&enable));drain(&b);take(c,enable,0);
    assert(!tiku_mt_control_submit(c,TIKU_MT_CONTROL_STOP,generation,&stop));drain(&b);take(c,stop,0);
    assert(b.phases[TIKU_MT_CONTROL_RELEASE]==4 && !store.writer);
    assert(reads==before_reads && writes==before_writes);
    unsigned remaining=0;
    for(unsigned i=0;i<TIKU_MFS_READERS;i++)if(store.readers[i].token){++remaining;assert(store.readers[i].token==other && store.readers[i].client==43);}
    assert(remaining==1);
    assert(!tiku_mfs_stat(&fs,"readme",&info) && info.id==original_id && info.complete);
    tiku_mfs_cursor_t cursor={0};
    assert(!tiku_mfs_list_pending(&fs,&cursor,&info) && info.id==id && info.length==sizeof payload && !info.complete);
    uint8_t readback[64];assert(tiku_mfs_read_at(&fs,other,0,readback,sizeof readback)==64);
    for(unsigned i=0;i<sizeof readback;i++)assert(readback[i]==0x7b);
    assert(!tiku_mfs_resume(&fs,id));assert(fs.draft.length==sizeof payload);
#if TIKU_MEDIA_ASYNC_ENABLE
    store.writer=42;store.failed=fs.failed=1;before_reads=reads;before_writes=writes;
    assert(tiku_mt_control_release_one(&store,42)==TIKU_MT_CONTROL_PENDING);
    assert(!store.writer&&!fs.writing&&store.failed&&fs.failed&&reads==before_reads&&writes==before_writes);
#else
    assert(!tiku_mfs_release(&fs));
#endif
    free(c);
}
/** @brief Reject invalid storage and wipe cursors without changing their targets. */
static void bounds(void)
{
    backend_t b={.owner=1};tiku_mt_control_ops_t ops={owner,backend_step};
    _Alignas(tiku_mt_control_t) unsigned char memory[sizeof(tiku_mt_control_t)+1];
    memset(memory,0xa5,sizeof memory);
    assert(!tiku_mt_control_init(memory,sizeof(tiku_mt_control_t)-1,ops,&b));
    assert(!tiku_mt_control_init(memory+1,sizeof(tiku_mt_control_t),ops,&b));
    assert(!tiku_mt_control_init(memory,sizeof memory,(tiku_mt_control_ops_t){0},&b));
    b.owner=0;assert(!tiku_mt_control_init(memory,sizeof memory,ops,&b));
    for(unsigned i=0;i<sizeof memory;i++)assert(memory[i]==0xa5);
    size_t cursor=2;uint8_t byte=0xa5;
    assert(tiku_mt_control_wipe(&byte,1,&cursor)==TIKU_MFS_ARG && cursor==2 && byte==0xa5);
    cursor=0;assert(tiku_mt_control_wipe(NULL,1,&cursor)==TIKU_MFS_ARG && !cursor);
    assert(!tiku_mt_control_wipe(NULL,0,&cursor));
    assert(tiku_mt_control_release_one(NULL,42)==TIKU_MFS_ARG);
}
/** @brief Reuse retained configuration and completion slots through repeated service sessions. */
static void repeated_sessions(void)
{
    backend_t b;uint64_t generation,enable,stop,previous=0;
    tiku_mt_control_t *c=create(&b,&generation);
    for(unsigned cycle=0;cycle<100;cycle++) {
        b.busy_prepare=2;
        assert(!tiku_mt_control_submit(c,TIKU_MT_CONTROL_ENABLE,generation,&enable) && enable>previous);
        drain(&b);take(c,enable,0);
        tiku_mt_control_snapshot_t snapshot;
        assert(!tiku_mt_control_observe(c,&snapshot) && snapshot.state==TIKU_MT_CONTROL_READY);
        assert(!tiku_mt_control_submit(c,TIKU_MT_CONTROL_STOP,generation,&stop) && stop>enable);
        drain(&b);take(c,stop,0);previous=stop;
        assert(!tiku_mt_control_observe(c,&snapshot) && snapshot.state==TIKU_MT_CONTROL_STOPPED &&
            snapshot.completion_slots_free==2 && !snapshot.cleanup_required && snapshot.generation==generation);
        assert(b.cursor==sizeof b.data && !b.prepared && !b.attached);
    }
    assert(!tiku_mt_control_forget(c));free(c);
}
/** @brief Run selected ownership and completion guards without board access. */
int main(int argc,char **argv)
{
    struct {const char *name;void (*run)(void);} cases[]={
        {"commands",commands},{"pending",pending_enable},{"failures",failures},
        {"generations",generations},{"media",media_cleanup},{"bounds",bounds},{"repeated",repeated_sessions}};
    unsigned ran=0;
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++)if(argc==1 || (argc==2 && !strcmp(argv[1],cases[i].name))){cases[i].run();++ran;}
    assert(ran);printf("transfer control: %u groups pass; context=%zu bytes\n",ran,tiku_mt_control_size());return 0;
}
