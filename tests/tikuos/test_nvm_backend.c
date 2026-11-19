/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_nvm_backend.c - interrupted S31 undo and mirror transactions over private flash.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <arch/esp32s31/tiku_storage_arch.h>
static _Alignas(8) uint8_t flash[TIKU_S31_STORAGE_SHADOW],shadow[sizeof flash],baseline[sizeof flash];
static _Alignas(8) uint8_t live[12288];
static unsigned unlocked,allows_sync=1,kernel=1,io_calls,accepted,cut_call=~0u,cut_bytes,cut_seen;
static unsigned pending_kind,pending_length,polls,fail_read;
static uint32_t pending_address;
static uint8_t pending_bytes[64];
#define TIKU_S31_STORAGE_ASYNC_ENABLE 1
#define TIKU_S31_STORAGE_VIEW shadow
#define TIKU_S31_STORAGE_UNDO (*(undo_t *)(live+256))
#define LIVE live
#define LIVE_BYTES sizeof live
#include <arch/esp32s31/tiku_storage_arch.c>

/** @brief Require explicit durable write windows around asynchronous undo updates. */
uint16_t tiku_mpu_unlock_nvm(void) {assert(!unlocked);unlocked=1;return 0x55;}
/** @brief Relock without invoking a synchronous flush from the polled transaction. */
void tiku_mpu_arch_lock_nvm(uint16_t saved) {assert(unlocked && saved==0x55);unlocked=0;}
/** @brief Expose the fixture's foreground ownership state. */
int tiku_s31_storage_context(void) {return kernel;}
/** @brief Require reserved ranges and emulate NOR erase/program/read behavior. */
static void access_flash(unsigned kind,uint32_t address,void *buffer,size_t n)
{
    assert(address>=TIKU_S31_STORAGE_START && address<TIKU_S31_STORAGE_END);
    size_t off=address-TIKU_S31_STORAGE_START;
    assert(n<=sizeof flash-off);
    if(kind==0)memcpy(buffer,flash+off,n);
    else if(kind==1)memset(flash+off,255,n);
    else {
        const uint8_t *src=buffer;
        for(size_t i=0;i<n;i++){assert((flash[off+i]&src[i])==src[i]);flash[off+i]=src[i];}
    }
}
/** @brief Model the boot/synchronous interface; asynchronous polls may use bounded reads only. */
int tiku_s31_storage_io(unsigned kind,uint32_t address,void *buffer,size_t n)
{
    ++io_calls;
    assert(!pending_kind && (allows_sync || (kind==0 && n<=64)));
    if(address==0x8000) {
        assert(kind==0 && n==4096);memset(buffer,255,n);
        uint32_t *p=buffer;p[0]=0x50aa;p[1]=0x10000;p[2]=0x200000;return 0;
    }
    if(fail_read && !kind)return -1;
    access_flash(kind,address,buffer,kind==1?4096:n);return 0;
}
/** @brief Borrow a copied controller payload until a later poll durably completes or tears it. */
int tiku_s31_storage_slice(tiku_s31_flash_t *state,unsigned kind,uint32_t address,void *buffer,size_t n,int resume)
{
    (void)state; ++io_calls;assert(!unlocked && !allows_sync);
    if(!resume) {
        assert(!pending_kind && (kind==1 ? n==0 : n<=64 && n && !(n&3)));
        pending_kind=kind;pending_address=address;pending_length=(unsigned)n;polls=2;
        if(n){assert(!blank(buffer,n));memcpy(pending_bytes,buffer,n);}
        ++accepted;return 1;
    }
    assert(kind==pending_kind && address==pending_address && n==pending_length);
    if(--polls)return 1;
    unsigned length=kind==1?4096:pending_length;
    int fail=accepted-1==cut_call;
    if(fail){assert(cut_bytes<=length);length=cut_bytes;cut_seen=1;}
    access_flash(kind,address,pending_bytes,length);pending_kind=0;
    return fail ? -1 : 0;
}
/** @brief Drop volatile state and run production boot recovery from physical flash bytes. */
static void boot(void)
{
    selected=-1;generation=ready=attempted=failed=writing=flushing=provisioning=0;
    restore_status=TIKU_NVM_RESTORE_CRC_FAIL;active_write=NULL;
    pending_kind=unlocked=fail_read=0;allows_sync=1;kernel=1;
    memset(shadow,0,sizeof shadow);memset(live,0,sizeof live);
    assert(tiku_s31_storage_init()==0);tiku_s31_storage_seal();
    allows_sync=0;accepted=io_calls=cut_seen=0;cut_call=~0u;
}
/** @brief Build established media with a durable user scalar and patterned region sectors. */
static void prepare(void)
{
    memset(flash,255,sizeof flash);boot();allows_sync=1;
    live[8192]=0x3c;assert(!tiku_mem_arch_nvm_flush_status());
    uint8_t data[8192];for(unsigned i=0;i<sizeof data;i++)data[i]=(uint8_t)(i^(i>>8));
    tiku_nvm_backend_t be=*tiku_nvm_backend_get();
    assert(!be.write(&be,0,data,sizeof data));allows_sync=0;
    memcpy(baseline,flash,sizeof flash);boot();
}
/** @brief Advance one transaction phase and preserve protection and backend ownership. */
static int step(tiku_s31_storage_write_t *op)
{
    unsigned before=io_calls;int rc=tiku_s31_storage_write_poll(op);
    assert(io_calls-before<=1 && !unlocked);
    if(rc==1)assert(writing && flushing && active_write==op);
    else assert(!writing && !flushing && !active_write && !pending_kind);
    return rc;
}
/** @brief Drain an operation without assuming its number of memory or flash phases. */
static int finish(tiku_s31_storage_write_t *op)
{
    int rc;unsigned steps=0;
    do {assert(++steps<10000);rc=step(op);}while(rc==1);
    return rc;
}
/** @brief Require untouched bytes around a write and the independent durable user scalar. */
static void check(size_t off,const void *data,size_t n,int old_ok)
{
    assert(live[8192]==0x3c && !TIKU_S31_STORAGE_UNDO.pending);
    assert(!memcmp(shadow,baseline,off));
    assert(!memcmp(shadow+off+n,baseline+off+n,TIKU_S31_REGION_BYTES-off-n));
    assert(!memcmp(shadow+off,data,n) || (old_ok && !memcmp(shadow+off,baseline+off,n)));
}
/** @brief Exercise every flash command boundary with empty, partial and complete persistence. */
static unsigned interruptions(void)
{
    prepare();tiku_nvm_backend_t be=*tiku_nvm_backend_get();
    tiku_s31_storage_write_t op;uint32_t value=0x61727374;
    assert(tiku_s31_storage_write_begin(&op,&be,128,&value,4)==1);
    unsigned count=0,lengths[512];
    while(op.phase) {
        unsigned prev=accepted;int rc=step(&op);assert(rc>=0);
        if(accepted!=prev){assert(count<512);lengths[count++]=pending_kind==1 ? 4096 : pending_length;}
    }
    assert(count==accepted);check(128,&value,4,0);boot();check(128,&value,4,0);
    unsigned cases=0;
    for(unsigned i=0;i<count;i++)for(unsigned cut=0;cut<3;cut++) {
        memcpy(flash,baseline,sizeof flash);boot();be=*tiku_nvm_backend_get();
        assert(tiku_s31_storage_write_begin(&op,&be,128,&value,4)==1);
        cut_call=i;cut_bytes=cut==0 ? 0 : cut==1 ? lengths[i]/2 : lengths[i];
        assert(finish(&op)==-1 && failed && cut_seen && !tiku_nvm_backend_get());
        boot();check(128,&value,4,1);++cases;
    }
    return cases;
}
/** @brief Check mapped publication, subregions, cross-sector writes and competing requests. */
static void ownership(void)
{
    prepare();tiku_nvm_backend_t be=*tiku_nvm_backend_get();tiku_s31_storage_write_t op,other;
    uint8_t bytes[12];memset(bytes,0x69,sizeof bytes);
    tiku_nvm_backend_t view=be;view.base+=2048;view.size-=2048;
    assert(tiku_s31_storage_write_begin(&op,&view,2044,bytes,sizeof bytes)==1);
    assert(tiku_s31_storage_write_begin(&other,&view,0,bytes,sizeof bytes)==-2);
    assert(tiku_mem_arch_nvm_flush_status()==-1 && !failed);
    assert(be.write(&be,0,bytes,sizeof bytes)==-1 && !failed);
    kernel=0;assert(tiku_s31_storage_write_poll(&op)==-1);kernel=1;
    unsigned turns=0;
    while(op.phase) {
        if(op.phase!=WRITE_PUBLISH && op.left==sizeof bytes)assert(!memcmp(shadow,baseline,4096));
        assert(step(&op)>=0);assert(++turns<10000);
    }
    check(4092,bytes,sizeof bytes,0);boot();check(4092,bytes,sizeof bytes,0);
    be=*tiku_nvm_backend_get();
    assert(tiku_s31_storage_write_begin(&op,&be,4092,bytes,sizeof bytes)==1);
    unsigned before=accepted;assert(!finish(&op) && before==accepted);
    assert(tiku_s31_storage_write_begin(&op,&be,0,shadow,4)==-1);
    assert(tiku_s31_storage_write_begin(&op,&be,be.size,bytes,1)==-1);
    assert(tiku_s31_storage_write_begin(&op,&be,0,bytes,0)==-1);
    assert(tiku_s31_storage_write_begin(&op,NULL,0,bytes,4)==-1);
    assert(tiku_s31_storage_write_begin(&op,&be,128,bytes,4)==1);
    while(!op.verify)assert(step(&op)==1);
    fail_read=1;assert(step(&op)==-1 && failed);boot();
    be=*tiku_nvm_backend_get();
    ((uint32_t *)mirror((unsigned)selected))[1]^=1;
    assert(tiku_s31_storage_write_begin(&op,&be,0,bytes,4)==1);
    assert(finish(&op)==-1 && !tiku_nvm_backend_get());
}
/** @brief Test the production journal state machine and production reset recovery. */
int main(void)
{
    ownership();unsigned cases=interruptions();
    printf("S31 NVM: %u interrupted flash commands, undo replay, shadow publication and ownership pass\n",cases);
    return 0;
}
