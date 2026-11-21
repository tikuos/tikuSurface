/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_settings_nvm.c - product settings through TFS, S31 undo, mirrors and boot recovery.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main nvm_backend_suite
#include "test_nvm_backend.c"
#undef main
#include "settings_io.h"
#include <kernel/fs/tiku_tfs_async.h>
static tiku_tfs_t files;
static uint8_t original[32],replacement[32];
static _Alignas(8) unsigned char bridge[SURFACE_SETTINGS_IO_BYTES];

#if SURFACE_PREFERENCES_NVM
static const unsigned char calibration_guard[32]={1,9,3,7};
/** @brief Seed a desktop record using the fixture's synchronous setup backend. */
static int preferences_seed(const unsigned char *data,size_t n)
{return tiku_tfs_write(&files,"tikusurface-desk-v1",data,n)==TFS_OK?0:-1;}
/** @brief Check the independent calibration record during preference recovery. */
static int preferences_read(unsigned char *out,size_t capacity,size_t *length)
{
    unsigned char guard[32];size_t n;
    assert(tiku_tfs_read(&files,"tikusurface-cal-v1",guard,sizeof guard,&n)==TFS_OK&&n==32&&!memcmp(guard,calibration_guard,32));
    return surface_os_preferences_read(out,capacity,length);
}
#define surface_os_settings_read preferences_read
#define surface_os_settings_write preferences_seed
#define surface_os_settings_begin surface_os_preferences_begin
#endif

/** @brief Translate the data-store view to the production region backend during setup. */
static int store_write(tiku_nvm_backend_t *be,size_t off,const void *data,size_t n)
{
    (void)be;tiku_nvm_backend_t region=*tiku_nvm_backend_get();
    return region.write(&region,32768+off,data,n);
}
static tiku_nvm_backend_t store={.base=shadow+32768,.size=TIKU_S31_REGION_BYTES-32768,.write=store_write};
/** @brief Expose the mounted fixture store to the product provider. */
tiku_tfs_t *tiku_vfs_tree_data_store(void){return &files;}
/** @brief Expose the fixture's kernel-owner admission. */
int surface_os_in_owner(void){return kernel;}

/** @brief Remount the on-flash file store after production boot recovery. */
static void mount_files(void)
{
    boot();memset(&files,0,sizeof files);memset(bridge,0,sizeof bridge);
    assert(tiku_tfs_mount(&files,&store)==TFS_OK);
}
/** @brief Create an established store containing unrelated data and optional calibration. */
static void prepare_files(int prior)
{
    prepare();allows_sync=1;memset(&files,0,sizeof files);
    assert(tiku_tfs_init(&files,&store)==TFS_OK);
    assert(tiku_tfs_write(&files,"user-note","unrelated",10)==TFS_OK);
#if SURFACE_PREFERENCES_NVM
    assert(tiku_tfs_write(&files,"tikusurface-cal-v1",calibration_guard,32)==TFS_OK);
#endif
    if(prior)assert(surface_os_settings_write(original,32)==0);
    allows_sync=0;memcpy(baseline,flash,sizeof flash);mount_files();
}
/** @brief Verify the complete product record and the unrelated file and durable scalar. */
static void records(int prior,int newer)
{
    unsigned char got[32];size_t length;
    int rc=surface_os_settings_read(got,sizeof got,&length);
    if(newer)assert(!rc && length==32 && !memcmp(got,replacement,32));
    else if(prior)assert(!rc && length==32 && !memcmp(got,original,32));
    else assert(rc==1);
    assert(tiku_tfs_read(&files,"user-note",got,sizeof got,&length)==TFS_OK);
    assert(length==10 && !memcmp(got,"unrelated",10) && live[8192]==0x3c);
}
/** @brief Drive one product callback while limiting it to one physical flash callback. */
static int provider_step(void)
{
    unsigned before=io_calls;int rc=surface_os_settings_poll(bridge);
    assert(io_calls-before<=1 && !unlocked);return rc;
}
/** @brief Drain the product's retained bridge. */
static int provider_finish(void)
{
    unsigned turns=0;int rc;
    do{assert(++turns<20000);rc=provider_step();}while(rc==1);
    return rc;
}
/** @brief Tear each accepted flash command in a real settings transaction and reboot. */
static unsigned interrupted_settings(int prior)
{
    prepare_files(prior);unsigned sizes[2048],count=0;
    assert(surface_os_settings_begin(bridge,replacement,32)==1);
    while(1){
        unsigned before=accepted;int rc=provider_step();assert(rc>=0);
        if(accepted!=before){assert(count<2048);sizes[count++]=pending_kind==1?4096:pending_length;}
        if(!rc)break;
    }
    assert(count==accepted);records(prior,1);mount_files();records(prior,1);
    unsigned cases=0;
    for(unsigned i=0;i<count;i++){
        memcpy(flash,baseline,sizeof flash);mount_files();records(prior,0);
        assert(surface_os_settings_begin(bridge,replacement,32)==1);
        cut_call=i;cut_bytes=sizes[i]/2;
        assert(provider_finish()==-1 && cut_seen && !files.mounted);
        mount_files();
        unsigned char got[32];size_t n;
        int rc=surface_os_settings_read(got,sizeof got,&n);
        records(prior,!rc && n==32 && !memcmp(got,replacement,32));++cases;
    }
    return cases;
}
/** @brief Keep old calibration during a pending write and drain cancellation before reuse. */
static void cancelled_settings(void)
{
    prepare_files(1);
    assert(surface_os_settings_poll(bridge)==-1);
    assert(surface_os_settings_begin(bridge,replacement,31)==-1);
    assert(surface_os_settings_begin(bridge,replacement,32)==1);
    assert(surface_os_settings_begin(bridge,replacement,32)==-1);
    unsigned turns=0;
    while(!pending_kind){assert(provider_step()==1 && ++turns<1000);records(1,0);}
    surface_os_settings_cancel(bridge);
    assert(files.wr_open && active_write);
    assert(provider_finish()==-1 && !files.wr_open && !active_write);
    records(1,0);mount_files();records(1,0);
    assert(surface_os_settings_begin(bridge,replacement,32)==1);
    assert(!provider_finish());records(1,1);mount_files();records(1,1);
}
/** @brief Exercise the product provider against the actual file and journal implementations. */
int main(void)
{
    for(unsigned i=0;i<32;i++){original[i]=(uint8_t)i;replacement[i]=(uint8_t)(i^0x6a);}
    cancelled_settings();
    unsigned cases=interrupted_settings(0)+interrupted_settings(1);
    printf("Settings/S31: %u interrupted flash commands, cancellation, replacement and boot recovery pass\n",cases);
    return 0;
}
