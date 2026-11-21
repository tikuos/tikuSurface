/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_settings_store.c - calibration restart and interrupted persistence through TFS.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "runtime.h"
#include "settings.h"
#include "settings_io.h"
#include <kernel/fs/tiku_tfs.h>
#include <arch/esp32s31/tiku_storage_async.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char record_name[] = "tikusurface-cal-v1";
static const unsigned char neighbour[] = "unrelated user data";
static const surface_touch_map_t defaults = {0,799,0,479,0,0,0};
static const surface_touch_map_t first = {12,4095,24,2047,1,0,1};
static const surface_touch_map_t second = {30,4000,50,1900,0,1,0};
static _Alignas(8) unsigned char disk[TIKU_TFS_EXTENT_FOR_SLOTS(257)], snapshot[sizeof disk];
static tiku_tfs_t fs;
static unsigned available, owner = 1, calls, cut_call = ~0u, cut_bytes, failed, lengths[32];

/** @brief Expose the fixture's mounted data store without mounting on access. */
tiku_tfs_t *tiku_vfs_tree_data_store(void) { return available ? &fs : NULL; }
/** @brief Model serialized kernel ownership at the provider boundary. */
int surface_os_in_owner(void) { return owner; }
/** @brief Persist writes, or latch a simulated power loss after a selected prefix. */
static int write_nvm(tiku_nvm_backend_t *backend, size_t off, const void *src, size_t n)
{
    assert(off <= backend->size && n <= backend->size - off);
    if (failed) return -1;
    unsigned call = calls++;
    if (call < 32) lengths[call] = (unsigned)n;
    if (call == cut_call) {
        assert(cut_bytes <= n);
        memcpy(backend->base + off, src, cut_bytes); failed = 1; return -1;
    }
    memcpy(backend->base + off, src, n); return 0;
}
static tiku_nvm_backend_t backend = {.base=disk, .size=sizeof disk, .write=write_nvm};
/** @brief Retain a private-memory request through two asynchronous polls. */
int tiku_s31_storage_write_begin(tiku_s31_storage_write_t *op,tiku_nvm_backend_t *be,
                                  size_t off,const void *data,size_t n)
{
    assert(be==&backend && !op->phase);
    op->data=data;op->offset=off;op->left=n;op->phase=2;return 1;
}
/** @brief Retire the delayed request with the existing prefix-failure model. */
int tiku_s31_storage_write_poll(tiku_s31_storage_write_t *op)
{
    assert(op->phase);
    if(--op->phase)return 1;
    return write_nvm(&backend,op->offset,op->data,op->left);
}

/** @brief Restore power and begin a new write trace. */
static void power_on(void) { calls = failed = 0; cut_call = ~0u; memset(lengths,0,sizeof lengths); }
/** @brief Discard filesystem RAM state and mount persisted bytes. */
static void remount(void)
{
    power_on(); memset(&fs,0,sizeof fs);
    assert(tiku_tfs_mount(&fs,&backend) == TFS_OK); available = 1;
}
/** @brief Release the complete runtime before discarding filesystem RAM. */
static void stop(void)
{
    surface_runtime_stop();
    for (unsigned i=0;i<20000 && surface_runtime_poll();i++) {}
    surface_runtime_stats_t s; surface_runtime_stats(&s);
    assert(!s.running && !s.stopping && !s.live_bytes && !s.allocations);
}
/** @brief Compare mapping fields independently of structure padding. */
static int same(const surface_touch_map_t *a,const surface_touch_map_t *b)
{
    return a->x_min==b->x_min && a->x_max==b->x_max && a->y_min==b->y_min && a->y_max==b->y_max &&
        a->swap==b->swap && a->invert_x==b->invert_x && a->invert_y==b->invert_y;
}
/** @brief Require the runtime's active mapping and persistence state. */
static void expect(unsigned expected,const surface_touch_map_t *map)
{
    surface_touch_map_t got; unsigned state;
    assert(surface_runtime_calibration(&got,&state) && state==expected && same(&got,map));
}
/** @brief Drive a queued save to completion without assuming a turn number. */
static unsigned save(const surface_touch_map_t *map)
{
    assert(surface_runtime_calibrate(map));
    assert(!surface_runtime_calibrate(map));
    unsigned state = SURFACE_SETTINGS_PENDING; surface_touch_map_t got;
    for (unsigned i=0;i<20000 && state==SURFACE_SETTINGS_PENDING;i++) {
        assert(surface_runtime_poll());
        assert(surface_runtime_calibration(&got,&state));
    }
    assert(state != SURFACE_SETTINGS_PENDING); return state;
}
/** @brief Require byte-for-byte preservation of a separate user's file. */
static void check_neighbour(void)
{
    unsigned char got[sizeof neighbour]; size_t length;
    assert(tiku_tfs_read(&fs,"user-note",got,sizeof got,&length)==TFS_OK);
    assert(length==sizeof neighbour && !memcmp(got,neighbour,sizeof got));
}
/** @brief Create a private fixture volume with an unrelated file and optional calibration. */
static void prepare(int prior)
{
    power_on(); memset(disk,0,sizeof disk); memset(&fs,0,sizeof fs);
    assert(tiku_tfs_init(&fs,&backend)==TFS_OK); available=1; power_on();
    assert(tiku_tfs_write(&fs,"user-note",neighbour,sizeof neighbour)==TFS_OK); power_on();
    if (prior) {
        unsigned char bytes[32]; assert(surface_settings_encode(&first,bytes));
        assert(!surface_os_settings_write(bytes,sizeof bytes)); power_on();
    }
}
/** @brief Check interrupted initial and replacement saves at each backend write boundary. */
static unsigned interruptions(int prior)
{
    prepare(prior); memcpy(snapshot,disk,sizeof disk);
    assert(surface_runtime_start()); power_on();
    assert(save(&second)==SURFACE_SETTINGS_SAVED);
    unsigned count=calls, sizes[32]; assert(count && count<=32); memcpy(sizes,lengths,sizeof sizes); stop();
    unsigned cases=0;
    for (unsigned call=0;call<count;call++) {
        for (unsigned prefix=0;prefix<=sizes[call];prefix++) {
            /* The backend contract makes aligned four-byte commit writes atomic. */
            if (sizes[call]==4 && prefix!=0 && prefix!=4) continue;
            memcpy(disk,snapshot,sizeof disk); remount();
            assert(surface_runtime_start()); expect(prior?SURFACE_SETTINGS_SAVED:SURFACE_SETTINGS_DEFAULT,prior?&first:&defaults);
            power_on(); cut_call=call; cut_bytes=prefix;
            assert(save(&second)==SURFACE_SETTINGS_FAILED && failed);
            expect(SURFACE_SETTINGS_FAILED,prior?&first:&defaults); stop();
            remount(); check_neighbour();
            assert(surface_runtime_start());
            surface_touch_map_t got; unsigned state;
            assert(surface_runtime_calibration(&got,&state));
            assert((state==SURFACE_SETTINGS_SAVED && same(&got,&second)) ||
                (state==(prior?SURFACE_SETTINGS_SAVED:SURFACE_SETTINGS_DEFAULT) && same(&got,prior?&first:&defaults)));
            stop(); ++cases;
        }
    }
    return cases;
}
/** @brief Independently reseal schema changes to distinguish incompatible from corrupt records. */
static void reseal(unsigned char bytes[32])
{
    uint32_t crc=UINT32_MAX;
    for (unsigned i=0;i<28;i++) {
        crc^=bytes[i];
        for (unsigned bit=0;bit<8;bit++) crc=(crc>>1)^((crc&1)?0xedb88320u:0);
    }
    crc=~crc; for (unsigned i=0;i<4;i++) bytes[28+i]=(unsigned char)(crc>>(8*i));
}
/** @brief Load malformed/future records without rewriting them and refuse incompatible saves. */
static void refused_records(void)
{
    for (unsigned kind=0;kind<7;kind++) {
        prepare(0); unsigned char bytes[33]; memset(bytes,0xa5,sizeof bytes);
        assert(surface_settings_encode(&first,bytes)); size_t length=32;
        if (kind==0) length=0;
        if (kind==1) length=31;
        if (kind==2) length=33;
        if (kind==3) bytes[12]^=1;
        if (kind==4) {bytes[4]=2;reseal(bytes);}
        if (kind==5) {bytes[21]=1;reseal(bytes);}
        if (kind==6) {bytes[6]=0;reseal(bytes);}
        assert(tiku_tfs_write(&fs,record_name,bytes,length)==TFS_OK);
        remount(); memcpy(snapshot,disk,sizeof disk);
        assert(surface_runtime_start()); expect(kind<4?SURFACE_SETTINGS_CORRUPT:SURFACE_SETTINGS_INCOMPATIBLE,&defaults);
        if (kind>=4) assert(!surface_runtime_calibrate(&second));
        stop(); assert(!memcmp(snapshot,disk,sizeof disk)); check_neighbour();
    }
}
/** @brief Preserve active calibration when another writer holds the store or slots are exhausted. */
static void unavailable_space(void)
{
    prepare(1); tiku_tfs_wr_t writer;
    assert(tiku_tfs_open_w(&fs,&writer,"pending-upload",32)==TFS_OK);
    assert(surface_runtime_start()); power_on();
    assert(save(&second)==SURFACE_SETTINGS_FAILED && !calls);
    expect(SURFACE_SETTINGS_FAILED,&first); stop();
    tiku_tfs_abort(&writer); remount(); check_neighbour();
    memset(snapshot,0x5a,sizeof snapshot);
    size_t length=(size_t)(fs.nslots-2)*TIKU_TFS_SLOT_BYTES-4;
    power_on(); assert(tiku_tfs_write(&fs,"bulk-user-file",snapshot,length)==TFS_OK);
    assert(surface_runtime_start()); power_on();
    assert(save(&second)==SURFACE_SETTINGS_FAILED && !calls);
    expect(SURFACE_SETTINGS_FAILED,&first); stop();
    remount(); check_neighbour();
    assert(surface_runtime_start()); expect(SURFACE_SETTINGS_SAVED,&first); stop();
}
#if SURFACE_PREFERENCES
/** @brief Complete an independent desktop save while calibration requests are excluded. */
static unsigned save_preferences(const surface_preferences_t *p)
{
    assert(surface_runtime_set_preferences(p));assert(!surface_runtime_calibrate(&second));
    unsigned status=SURFACE_SETTINGS_PENDING;surface_preferences_t got;
    for(unsigned i=0;i<20000&&status==SURFACE_SETTINGS_PENDING;++i){assert(surface_runtime_poll());assert(surface_runtime_preferences(&got,&status));}
    assert(status!=SURFACE_SETTINGS_PENDING);return status;
}
/** @brief Tear each preference record write and preserve calibration plus unrelated files. */
static unsigned preferences_interruptions(int prior)
{
    const surface_preferences_t old={1,0,1,2},newer={4,1,2,0},empty={0};
    prepare(1);
    if(prior){assert(surface_runtime_start());assert(save_preferences(&old)==SURFACE_SETTINGS_SAVED);stop();remount();}
    memcpy(snapshot,disk,sizeof disk);assert(surface_runtime_start());power_on();assert(save_preferences(&newer)==SURFACE_SETTINGS_SAVED);
    unsigned count=calls,sizes[32],cases=0;assert(count&&count<=32);memcpy(sizes,lengths,sizeof sizes);stop();
    for(unsigned call=0;call<count;++call)for(unsigned prefix=0;prefix<=sizes[call];++prefix){
        if(sizes[call]==4&&prefix!=0&&prefix!=4)continue;
        memcpy(disk,snapshot,sizeof disk);remount();assert(surface_runtime_start());expect(SURFACE_SETTINGS_SAVED,&first);
        power_on();cut_call=call;cut_bytes=prefix;assert(save_preferences(&newer)==SURFACE_SETTINGS_FAILED&&failed);
        surface_preferences_t got;unsigned status;assert(surface_runtime_preferences(&got,&status)&&status==SURFACE_SETTINGS_FAILED&&!memcmp(&got,prior?&old:&empty,sizeof got));
        stop();remount();check_neighbour();assert(surface_runtime_start());expect(SURFACE_SETTINGS_SAVED,&first);
        assert(surface_runtime_preferences(&got,&status));assert((status==SURFACE_SETTINGS_SAVED&&!memcmp(&got,&newer,sizeof got))||(status==(prior?SURFACE_SETTINGS_SAVED:SURFACE_SETTINGS_DEFAULT)&&!memcmp(&got,prior?&old:&empty,sizeof got)));
        stop();++cases;
    }
    return cases;
}
/** @brief Refuse unknown desktop schemas without overwriting either product record. */
static void preferences_schema(void)
{
    unsigned char bytes[32];surface_preferences_t p={2,1,2,1},got;unsigned status;
    prepare(1);assert(surface_preferences_encode(&p,bytes));bytes[4]=2;reseal(bytes);
    assert(tiku_tfs_write(&fs,"tikusurface-desk-v1",bytes,32)==TFS_OK);remount();memcpy(snapshot,disk,sizeof disk);
    assert(surface_runtime_start());assert(surface_runtime_preferences(&got,&status)&&status==SURFACE_SETTINGS_INCOMPATIBLE&&!got.background);
    assert(!surface_runtime_set_preferences(&p));expect(SURFACE_SETTINGS_SAVED,&first);stop();assert(!memcmp(snapshot,disk,sizeof disk));
}
#endif

/** @brief Exercise the real runtime, settings adapter and TFS across restart and write loss. */
void surface_test_settings_persistence(void)
{
    unsigned char bytes[32]; size_t length=0;
    available=0; assert(surface_runtime_start()); expect(SURFACE_SETTINGS_UNAVAILABLE,&defaults);
    assert(!surface_runtime_calibrate(&first)); stop();
    prepare(0); assert(surface_runtime_start()); expect(SURFACE_SETTINGS_DEFAULT,&defaults);
    assert(save(&first)==SURFACE_SETTINGS_SAVED); expect(SURFACE_SETTINGS_SAVED,&first); stop();
    remount(); assert(surface_runtime_start()); expect(SURFACE_SETTINGS_SAVED,&first); stop(); check_neighbour();
    power_on(); owner=0;
    assert(surface_os_settings_read(bytes,sizeof bytes,&length)==-1);
    assert(surface_os_settings_write(bytes,sizeof bytes)==-1 && !calls); owner=1;
    assert(surface_os_settings_write(bytes,31)==-1 && !calls);
    unsigned cases=interruptions(0)+interruptions(1);
    refused_records(); unavailable_space();
#if SURFACE_PREFERENCES
    unsigned pref_cases=preferences_interruptions(0)+preferences_interruptions(1);preferences_schema();
    printf("Desktop preferences/TFS: %u interrupted saves preserve calibration, neighbours and applied state\n",pref_cases);
#endif
    available=0;
    printf("Settings/TFS: restart, %u interrupted saves, malformed/future records and neighbour preservation pass\n",cases);
}
