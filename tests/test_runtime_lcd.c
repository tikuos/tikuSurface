/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_runtime_lcd.c - composed presentation, display faults and retained PSRAM.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "runtime.h"
#include "display.h"
#include "input.h"
#include "media_io.h"
#include "console_io.h"
#include "settings_io.h"
#include "settings.h"
#if SURFACE_VFS
#include "vfs_contract.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static _Alignas(64) unsigned char region[SURFACE_RUNTIME_BYTES];
static int reserved, opened, pending, deny_close, deny_release, open_failure, unhealthy;
static int submit_result, poll_result = SURFACE_SCANOUT_BUSY;
static unsigned opens, closes, submissions, front;
static uint16_t *buffers[2];
static uint64_t now, ticket, last_ticket;
static unsigned clock_step = 100;
#if SURFACE_CONNECTED_ENABLE
#include "services.h"
#if SURFACE_TRANSFERS
surface_transfer_provider_t surface_os_transfers(void)
{ return (surface_transfer_provider_t){0}; }
#endif
static unsigned service_queries;
static const char *service_label="Wi-Fi joined; transfer stopped";
void surface_os_services(char *out,size_t capacity)
{ assert(capacity==96);++service_queries;snprintf(out,capacity,"%s",service_label); }
#endif

static unsigned delayed_polls;
#ifndef SURFACE_SETTINGS_REAL_TFS
static unsigned char settings_record[32];
static unsigned settings_writes;
static int settings_present, settings_fail;
static void *settings_borrowed;
#if SURFACE_PREFERENCES
static unsigned char preferences_record[32];
static unsigned preferences_writes;
static int preferences_present,preferences_fail,settings_kind_test;
#endif
static unsigned settings_delay, settings_cancelled;
#endif
static unsigned touch_bus_open, input_failure;
static surface_touch_report_t touch_report;
static unsigned close_us, release_us;
static int media_enabled, media_retired;
static surface_media_io_t *media_view;
#if SURFACE_CONTENT_JOBS
static int copy_media;
#if SURFACE_IMAGE
static int image_media;
#include "png_fixture.h"
static unsigned char image_png[1048576];
#endif
#if SURFACE_DIFF
static int diff_media;
#if SURFACE_NOTES
static int notes_media;
static unsigned notes_bytes_read;
static const char note_header[]="# tiku-note paper=Yellow important=0\n";
#endif
static unsigned diff_bytes_read;
#endif
#endif
int surface_host_allocating;

#if SURFACE_TERMINAL
static int terminal_owned;
static unsigned console_oversize;
size_t surface_os_console_size(void){return SURFACE_CONSOLE_BRIDGE_BYTES+console_oversize;}
void surface_os_console_init(void *p){assert((uintptr_t)p%16==0);memset(p,0x5a,SURFACE_CONSOLE_BRIDGE_BYTES);}
int surface_os_console_open(void *p){(void)p;assert(!terminal_owned);terminal_owned=1;return 1;}
int surface_os_console_close(void *p){(void)p;terminal_owned=0;return 1;}
int surface_os_console_write(void *p,const void *b,size_t n){(void)p;(void)b;assert(terminal_owned);return (int)n;}
int surface_os_console_read(void *p,void *b,size_t n,unsigned *lost){(void)p;(void)b;(void)n;assert(terminal_owned);*lost=0;return 0;}
int surface_os_console_interrupt(void *p){(void)p;assert(terminal_owned);return 1;}
#if SURFACE_DEVICE
#include "process_contract.h"
#if SURFACE_VFS
int surface_os_vfs_list(void *p,const char *path,unsigned off,surface_vfs_node_t *rows,size_t cap,size_t *count,int *more,int *dynamic){(void)p;(void)path;(void)off;(void)rows;(void)cap;*count=0;*more=*dynamic=0;return 0;}
int surface_os_vfs_stat(void *p,const char *path,surface_vfs_node_t *out){(void)p;(void)path;memset(out,0,sizeof *out);out->flags=SURFACE_NODE_DIRECTORY;return 0;}
int surface_os_vfs_read(void *p,const char *path,uint32_t id,char *out,size_t cap,size_t *total){(void)p;(void)path;(void)id;(void)out;(void)cap;*total=0;return -1;}
int surface_os_vfs_write(void *p,const char *path,uint32_t id,const char *value,size_t n,const char *before,size_t b){(void)p;(void)path;(void)id;(void)value;(void)n;(void)before;(void)b;return -1;}
#endif
int surface_os_process_read(void *p,unsigned i,surface_process_info_t *out){(void)p;(void)i;memset(out,0,sizeof *out);return 0;}
int surface_os_device_read(void *p,const char *path,char *out,size_t n){(void)p;(void)path;if(n<2)return -1;out[0]='1';out[1]=0;return 1;}
#endif
#if SURFACE_BASIC
int surface_os_program_start(void *p,const char *source,size_t n){(void)p;(void)source;(void)n;return 0;}
int surface_os_program_state(void *p){(void)p;return 0;}
int surface_os_program_stop(void *p){(void)p;return 1;}
#endif
#endif

void *surface_os_reserve(size_t bytes)
{ assert(!reserved && bytes == sizeof region); reserved = 1; memset(region, 0xa5, sizeof region); return region; }
int surface_os_release(void *base)
{
    assert(base == region && reserved && !opened && !pending && !touch_bus_open);
    if (deny_release) return 0;
#ifndef SURFACE_SETTINGS_REAL_TFS
    assert(!settings_borrowed);
#endif
    now += release_us;
    reserved = 0; memset(region, 0xdd, sizeof region); return 1;
}
uint64_t surface_os_time(void) { now += clock_step; return now; }
int surface_os_display_open(uint16_t *a, uint16_t *b)
{
    assert(reserved && !opened && a != b && !((uintptr_t)a & 63u) && !((uintptr_t)b & 63u));
    assert((unsigned char *)a >= region && (unsigned char *)a + SURFACE_SCANOUT_BYTES <= region + sizeof region);
    assert((unsigned char *)b >= region && (unsigned char *)b + SURFACE_SCANOUT_BYTES <= region + sizeof region);
    for (unsigned i = 0; i < 800 * 480; ++i) assert(!a[i]);
    ++opens;
    if (open_failure == 1) return 0;
    opened = 1; buffers[0] = a; buffers[1] = b; front = 0;
    return open_failure != 2;
}
int surface_os_display_submit(unsigned back, unsigned x, unsigned y, unsigned w, unsigned h, uint64_t token)
{
    assert(opened && !pending && back == (front ^ 1u));
    assert(x < 800 && y < 480 && w && h && w <= 800 - x && h <= 480 - y);
    ++submissions;
    if (submit_result) return submit_result;
    assert(token > last_ticket); last_ticket = ticket = token; pending = 1;
    return SURFACE_SCANOUT_OK;
}
int surface_os_display_poll(uint64_t token)
{
    assert(opened && pending && token == ticket);
    if (delayed_polls) { --delayed_polls; return SURFACE_SCANOUT_BUSY; }
    if (poll_result == SURFACE_SCANOUT_OK) { pending = 0; front ^= 1u; }
    if (poll_result == SURFACE_SCANOUT_ERROR) pending = 0;
    return poll_result;
}
int surface_os_display_health(void) { assert(opened); return !unhealthy; }
int surface_os_display_close(void)
{
    ++closes;
    if (deny_close) return 0;
    now += close_us;
    opened = pending = 0;
    return 1;
}

/** @brief Supply an idle touch provider for lifecycle tests. */
int surface_os_input_open(unsigned *owned) { if (input_failure) return 0; assert(!touch_bus_open); *owned = touch_bus_open = 1; return 1; }
/** @brief Release the fixture bus. */
void surface_os_input_close(unsigned *owned) { assert(touch_bus_open && *owned); *owned = touch_bus_open = 0; }
static int touch_none;
/** @brief Hand over the fixture's report, or none while touch_none is set. */
int surface_os_input_poll(surface_touch_report_t *out)
{
    if (touch_none) { memset(out, 0, sizeof *out); return SURFACE_OS_INPUT_NONE; }
    *out = touch_report;
    return out->cancel ? SURFACE_OS_INPUT_FAILED : SURFACE_OS_INPUT_REPORT;
}

static unsigned media_oversize;
/** @brief Require the complete bridge reservation and permit one-byte overrun injection. */
size_t surface_os_media_size(void) { return SURFACE_MEDIA_BRIDGE_BYTES+media_oversize; }
/** @brief Initialize the unavailable-media fixture. */
void surface_os_media_init(void *data) { assert((uintptr_t)data%8==0);memset(data,0x3c,SURFACE_MEDIA_BRIDGE_BYTES); }
/** @brief Retain an enumeration when the fixture enables media. */
int surface_os_media_submit(void *data, surface_media_io_t *io)
{ (void)data; if (!media_enabled) return SURFACE_MEDIA_UNAVAILABLE; assert(!media_view); media_view = io; return SURFACE_MEDIA_OK; }
/** @brief Mark a retained request ready for cancelled completion. */
int surface_os_media_cancel(void *data) { (void)data; media_retired = 1; return 1; }
/** @brief Delay enumeration until cancellation permits retirement. */
int surface_os_media_poll(void *data)
{
    (void)data;
#if SURFACE_CONTENT_JOBS
    if(copy_media&&media_view&&!media_retired){
        surface_media_io_t *r=media_view;
#if SURFACE_IMAGE
        if(image_media){
            static const unsigned char header[54]={'B','M',[10]=54,[14]=40,[19]=2,[23]=1,[26]=1,[28]=32};
            r->result=SURFACE_MEDIA_OK;r->epoch=7;r->size=1048576;r->id=37;r->rank=0;
            if(r->operation==SURFACE_MEDIA_ENUMERATE){if(r->exclusive)r->result=SURFACE_MEDIA_END;else strcpy(r->found,"a");}
            else if(r->operation==SURFACE_MEDIA_OPEN)r->reader=1;
            else if(r->operation==SURFACE_MEDIA_READ){
                r->count=r->offset>=r->size?0:r->size-r->offset;if(r->count>r->length)r->count=r->length;
                for(size_t at=0;at<r->count;at++){size_t offset=r->offset+at;r->bytes[at]=image_media==2?image_png[offset]:offset<54?header[offset]:(unsigned char)offset;}
            }else if(r->operation==SURFACE_MEDIA_CLOSE)r->reader=0;
            media_view=NULL;return 1;
        }
#endif
#if SURFACE_NOTES
        if(notes_media){
            r->result=SURFACE_MEDIA_OK;r->epoch=7;r->size=sizeof note_header-1+8192+1;r->id=101;r->rank=1;
            if(r->operation==SURFACE_MEDIA_ENUMERATE){
                const char *candidate="Notes";unsigned rank=0;char path[64];int found=0;
                if(strcmp(r->name,candidate)<0||(!strcmp(r->name,candidate)&&!r->exclusive)){
                    strcpy(r->found,candidate);r->directory=1;r->rank=rank;found=1;
                }
                for(unsigned i=1;i<=24&&!found;++i){snprintf(path,sizeof path,"Notes/note-%03u.txt",i);
                    if(strcmp(r->name,path)<0||(!strcmp(r->name,path)&&!r->exclusive)){strcpy(r->found,path);r->directory=0;r->rank=i;found=1;}}
                if(!found)r->result=SURFACE_MEDIA_END;
            }else if(r->operation==SURFACE_MEDIA_OPEN)r->reader=1;
            else if(r->operation==SURFACE_MEDIA_READ){
                r->count=r->offset>=r->size?0:r->size-r->offset;if(r->count>r->length)r->count=r->length;
                for(size_t i=0;i<r->count;++i){size_t at=r->offset+i;
                    r->bytes[i]=at<sizeof note_header-1?(unsigned char)note_header[at]:at==r->size-1?'\n':((at-(sizeof note_header-1))%32==31?'\n':'x');}
                notes_bytes_read+=(unsigned)r->count;
            }else if(r->operation==SURFACE_MEDIA_CLOSE)r->reader=0;
            media_view=NULL;return 1;
        }
#endif
        if(r->operation==SURFACE_MEDIA_REPLACE)return 0;
        r->result=SURFACE_MEDIA_OK;r->epoch=7;r->size=65536;r->id=17;r->rank=0;
#if SURFACE_DIFF
        if(diff_media)r->size=8192;
#endif
        if(r->operation==SURFACE_MEDIA_ENUMERATE){
            if(r->exclusive)r->result=SURFACE_MEDIA_END;
            else memcpy(r->found,"a",2);
        }else if(r->operation==SURFACE_MEDIA_OPEN)r->reader=1;
        else if(r->operation==SURFACE_MEDIA_READ){
            r->count=r->offset>=r->size?0:r->size-r->offset;
            if(r->count>r->length)r->count=r->length;
            for(size_t i=0;i<r->count;++i)r->bytes[i]=(unsigned char)(r->offset+i);
#if SURFACE_DIFF
            if(diff_media){for(size_t i=0;i<r->count;++i)r->bytes[i]=(r->offset+i)%32==31?'\n':diff_bytes_read>=8192?'y':'x';diff_bytes_read+=(unsigned)r->count;}
#endif
        }else if(r->operation==SURFACE_MEDIA_CLOSE)r->reader=0;
        media_view=NULL;return 1;
    }
#endif
    if(!media_view||!media_retired)return 0;
    media_view->result=SURFACE_MEDIA_END;media_view=NULL;return 1;
}

#ifndef SURFACE_SETTINGS_REAL_TFS
/** @brief Begin with an absent calibration record. */
int surface_os_settings_read(unsigned char *out, size_t capacity, size_t *length)
{ assert(capacity == 32); *length = settings_present ? 32 : 0; if (settings_present) memcpy(out, settings_record, 32); return !settings_present; }
/** @brief Refuse persistence in the runtime display fixture. */
int surface_os_settings_write(const unsigned char *data, size_t length)
{ assert(length == 32); ++settings_writes; if (settings_fail) return -1; memcpy(settings_record, data, 32); settings_present = 1; return 0; }

/** @brief Copy the record into borrowed runtime memory and delay its completion. */
int surface_os_settings_begin(void *bridge,const unsigned char *data,size_t length)
{
    assert(reserved && !settings_borrowed && length==32);
#if SURFACE_PREFERENCES
    settings_kind_test=0;
#endif
    settings_borrowed=bridge;settings_delay=6;settings_cancelled=0;
    memcpy(bridge,data,32);return 1;
}
#if SURFACE_PREFERENCES
/** @brief Keep desktop persistence separate from calibration in the fixture. */
int surface_os_preferences_read(unsigned char *out,size_t cap,size_t *n)
{assert(cap==32);*n=preferences_present?32:0;if(preferences_present)memcpy(out,preferences_record,32);return !preferences_present;}
/** @brief Reuse one delayed transaction while selecting its independent key. */
int surface_os_preferences_begin(void *bridge,const unsigned char *data,size_t n)
{int rc=surface_os_settings_begin(bridge,data,n);settings_kind_test=1;return rc;}
#endif
/** @brief Retain borrowed memory through delayed or cancelled completion. */
int surface_os_settings_poll(void *bridge)
{
    assert(bridge==settings_borrowed && reserved);
    if(settings_delay){--settings_delay;return 1;}
    settings_borrowed=NULL;
#if SURFACE_PREFERENCES
    if(settings_kind_test){if(settings_cancelled)return -1;++preferences_writes;if(preferences_fail)return -1;memcpy(preferences_record,bridge,32);preferences_present=1;return 0;}
#endif
    return settings_cancelled ? -1 : surface_os_settings_write(bridge,32);
}
/** @brief Mark cancellation without retiring an outstanding storage callback. */
void surface_os_settings_cancel(void *bridge)
{assert(bridge==settings_borrowed);settings_cancelled=1;}
#endif

#if SURFACE_PREFERENCES && !defined(SURFACE_SETTINGS_REAL_TFS)
/** @brief Check serialized save, applied state, restart and cancelled desktop publication. */
static void desktop_persistence(void)
{
    surface_preferences_t p={2,1,2,1},got={0};unsigned state;surface_touch_map_t map={0,799,0,479,0,0,0};
    preferences_present=preferences_writes=preferences_fail=0;poll_result=SURFACE_SCANOUT_OK;
    assert(surface_runtime_start());assert(surface_runtime_preferences(&got,&state)&&state==SURFACE_SETTINGS_DEFAULT&&!got.background);
    assert(surface_runtime_set_preferences(&p)&&!surface_runtime_calibrate(&map)&&!surface_runtime_set_preferences(&p));
    assert(surface_runtime_preferences(&got,&state)&&state==SURFACE_SETTINGS_PENDING&&!got.background);
    for(unsigned i=0;i<20000&&state==SURFACE_SETTINGS_PENDING;++i){assert(surface_runtime_poll());assert(surface_runtime_preferences(&got,&state));}
    assert(state==SURFACE_SETTINGS_SAVED&&!memcmp(&got,&p,sizeof p)&&preferences_writes==1);
    surface_runtime_stop();while(surface_runtime_poll()){}
    assert(surface_runtime_start());assert(surface_runtime_preferences(&got,&state)&&state==SURFACE_SETTINGS_SAVED&&!memcmp(&got,&p,sizeof p));
    surface_preferences_t changed={5,0,0,0};preferences_fail=1;assert(surface_runtime_set_preferences(&changed));state=SURFACE_SETTINGS_PENDING;
    for(unsigned i=0;i<20000&&state==SURFACE_SETTINGS_PENDING;++i){assert(surface_runtime_poll());assert(surface_runtime_preferences(&got,&state));}
    assert(state==SURFACE_SETTINGS_FAILED&&!memcmp(&got,&p,sizeof p));preferences_fail=0;now+=5000000;
    assert(surface_runtime_set_preferences(&changed));for(unsigned i=0;i<20000&&!settings_borrowed;++i)assert(surface_runtime_poll());assert(settings_borrowed);settings_delay=20;
    surface_runtime_stop();while(surface_runtime_poll()){}assert(!settings_borrowed&&preferences_writes==2);
    assert(surface_runtime_start());assert(surface_runtime_preferences(&got,&state)&&state==SURFACE_SETTINGS_SAVED&&!memcmp(&got,&p,sizeof p));
    surface_runtime_stop();while(surface_runtime_poll()){}preferences_present=0;
    puts("Desktop persistence: serialized calibration, applied state, restart, failure and cancellation passed");
}
#endif

/** @brief Refuse a failed touch startup and route a raw contact into the launcher. */
static void touch_lifecycle(void)
{
    input_failure = 1; assert(!surface_runtime_start()); assert(!reserved && !touch_bus_open);
    input_failure = 0; assert(surface_runtime_start());
    touch_report = (surface_touch_report_t){.count = 1, .points = {{80,200,4}}};
    now += 10000; assert(surface_runtime_poll());
    surface_input_stats_t input;
    assert(surface_runtime_input_stats(&input) && input.reports == 1 && input.contacts == 1 && input.x == 80 && input.y == 200 && input.id == 4);
    /* Polls between two of the controller's reports find none: the finger
     * stays down, and the tap ends at the report with no contacts. */
    surface_runtime_stats_t result;
    touch_none = 1;
    for (unsigned i = 0; i < 3; i++) { now += 10000; assert(surface_runtime_poll()); }
    surface_runtime_stats(&result); assert(result.foreground == 0);
    assert(surface_runtime_input_stats(&input) && input.contacts == 1);
    touch_none = 0;
    touch_report.count = 0; now += 10000; assert(surface_runtime_poll());
    surface_runtime_stats(&result); assert(result.foreground == 1);
    surface_runtime_stop(); while (surface_runtime_poll()) {}
    assert(!touch_bus_open);
}

#ifndef SURFACE_SETTINGS_REAL_TFS
/** @brief Verify explicit save, restart loading, pacing and failure retention. */
static void calibration_persistence(void)
{
    surface_touch_map_t map={0,799,0,479,1,0,1},decoded;
    unsigned status;
    poll_result=SURFACE_SCANOUT_OK;
    assert(surface_runtime_start());assert(surface_runtime_calibrate(&map));
    assert(!surface_runtime_calibrate(&map));
    for(unsigned i=0;i<1000 && settings_writes==0;i++)assert(surface_runtime_poll());
    assert(settings_writes==1 && !settings_borrowed);
    assert(surface_settings_decode(settings_record,32,&decoded)==SURFACE_SETTINGS_SAVED && decoded.swap);
    map.swap=0;settings_fail=1;assert(surface_runtime_calibrate(&map));
    assert(surface_runtime_poll() && settings_writes==1);
    now+=5000000;
    for(unsigned i=0;i<1000 && settings_writes==1;i++)assert(surface_runtime_poll());
    assert(settings_writes==2 && !settings_borrowed);
    assert(surface_runtime_calibration(&decoded,&status) && decoded.swap && status==SURFACE_SETTINGS_FAILED);
    surface_runtime_stop();while(surface_runtime_poll()){}
    settings_fail=0;assert(surface_runtime_start());
    assert(surface_runtime_calibration(&decoded,&status) && decoded.swap && status==SURFACE_SETTINGS_SAVED);
    assert(surface_runtime_calibrate(&map));
    for(unsigned i=0;i<1000 && !settings_borrowed;i++)assert(surface_runtime_poll());
    assert(settings_borrowed);settings_delay=20;
    unsigned reports_before;surface_input_stats_t input;
    assert(surface_runtime_input_stats(&input));reports_before=input.reports;
    now+=10000;assert(surface_runtime_poll());
    assert(surface_runtime_input_stats(&input) && input.reports>reports_before);
    surface_runtime_stop();assert(surface_runtime_poll() && reserved && settings_borrowed);
    for(unsigned i=0;i<10;i++)assert(surface_runtime_poll() && reserved);
    while(surface_runtime_poll()){}
    assert(settings_writes==2 && !settings_borrowed);
    assert(surface_runtime_start());assert(surface_runtime_calibrate(&map));
    surface_runtime_stop();while(surface_runtime_poll()){}
    assert(settings_writes==2 && !settings_borrowed);
    settings_present=0;poll_result=SURFACE_SCANOUT_BUSY;
}

#else
void surface_test_settings_persistence(void);
#endif

/** @brief Copy counters without advancing the runtime or transport. */
static surface_runtime_stats_t stats(void)
{ surface_runtime_stats_t s; surface_runtime_stats(&s); return s; }
/** @brief Finish a stop after the fixture acknowledges close and reservation release. */
static void drain(void)
{
    deny_close = deny_release = 0;
    surface_runtime_stop();
    for (unsigned i = 0; i < 8 && surface_runtime_poll(); ++i) {}
    assert(!reserved && !opened && !pending && !stats().running && !stats().live_bytes);
}
/** @brief Reach accepted DMA ownership while completion remains delayed. */
static void await_pending(void)
{
    for (unsigned i = 0; i < 1000 && !pending; ++i) assert(surface_runtime_poll());
    assert(pending && stats().frames && !stats().completed);
}
/** @brief Unwind both scanout allocations and five shell allocations before display open. */
static void startup_failures(void)
{
    media_oversize=1;assert(!surface_runtime_start());assert(!reserved&&!stats().running);
    media_oversize=0;
#if SURFACE_TERMINAL
    console_oversize=1;assert(!surface_runtime_start());assert(!reserved&&!stats().running);
    console_oversize=0;
#endif
    for (unsigned fail = 1; fail <= 7; ++fail) {
        unsigned before = opens;
        surface_runtime_fail_at(fail);
        assert(!surface_runtime_start());
        assert(!reserved && !stats().running && opens == before && stats().allocation_failures == 1);
    }
    surface_runtime_fail_at(0);
    for (open_failure = 1; open_failure <= 2; ++open_failure) {
        deny_close = 1;
        assert(!surface_runtime_start());
        assert(reserved && stats().stopping && stats().display_errors == 1);
        assert(stats().live_bytes >= 2 * SURFACE_SCANOUT_BYTES);
        assert(!surface_runtime_start());
        drain();
    }
    open_failure = 0;
}
/** @brief Stop during painting, conversion and DMA wait without premature buffer release. */
static void cancellation(void)
{
    const unsigned turns[] = {0, 1, 120, 123, 496};
    for (unsigned n = 0; n < sizeof turns / sizeof turns[0]; ++n) {
        assert(surface_runtime_start());
        for (unsigned i = 0; i < turns[n]; ++i) assert(surface_runtime_poll());
        deny_close = 1;
        surface_runtime_stop();
        assert(!surface_runtime_launch(1));
        assert(surface_runtime_poll() && opened && reserved);
        assert(stats().live_bytes >= 2 * SURFACE_SCANOUT_BYTES);
        if (pending) assert(stats().live_bytes > 2 * SURFACE_SCANOUT_BYTES + 1536000);
        deny_close = 0; deny_release = 1;
        assert(surface_runtime_poll() && !opened && !pending && reserved && !stats().live_bytes);
        drain();
    }
}
/** @brief Complete repeated lifecycles and retain the token sequence across reopen. */
static void lifecycle(void)
{
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        assert(surface_runtime_start());
        await_pending();
        assert(stats().allocations == 7 && stats().live_bytes > 3072000 && stats().peak_bytes < sizeof region);
        assert(stats().peak_bytes>=stats().live_bytes+SURFACE_MEDIA_BRIDGE_BYTES);
        uint16_t sample = buffers[front ^ 1u][2000];
        for (unsigned i = 0; i < 3; ++i) assert(surface_runtime_poll());
        assert(!stats().completed && buffers[front ^ 1u][2000] == sample);
        poll_result = SURFACE_SCANOUT_OK;
        assert(surface_runtime_poll() && stats().completed == 1 && !pending);
        poll_result = SURFACE_SCANOUT_BUSY;
        assert(surface_runtime_launch(1));
        for (unsigned i = 0; i < 1000 && !pending; ++i) assert(surface_runtime_poll());
        assert(pending && stats().frames == 2);
        drain();
    }
}
/** @brief Refused polls keep leases until close; retired errors and idle faults stop the runtime. */
static void faults(void)
{
    const int results[] = {SURFACE_SCANOUT_REFUSED, SURFACE_SCANOUT_ERROR};
    for (unsigned i = 0; i < 2; ++i) {
        assert(surface_runtime_start()); await_pending();
        deny_close = 1; poll_result = results[i];
        assert(surface_runtime_poll() && stats().stopping && stats().display_errors == 1 && opened);
        if (!i) assert(pending && stats().live_bytes > 2 * SURFACE_SCANOUT_BYTES + 1536000);
        else assert(!pending && stats().live_bytes >= 2 * SURFACE_SCANOUT_BYTES);
        drain(); poll_result = SURFACE_SCANOUT_BUSY;
    }
    assert(surface_runtime_start());
    unhealthy = deny_close = 1;
    assert(surface_runtime_poll() && stats().stopping && stats().display_errors == 1 && opened);
    drain(); unhealthy = 0;
    assert(surface_runtime_start()); submit_result = SURFACE_SCANOUT_ERROR; deny_close = 1;
    for (unsigned i = 0; i < 1000 && !stats().stopping; ++i) assert(surface_runtime_poll());
    assert(stats().stopping && stats().display_errors == 1 && opened && !pending);
    drain(); submit_result = 0;
}
/** @brief Include hardware close and final reservation release in the terminal dispatch sample. */
static void stop_timing(void)
{
    assert(surface_runtime_start());
    close_us = 7000; release_us = 9000;
    drain();
    assert(stats().last_dispatch_us >= 16000 && stats().max_dispatch_us >= 16000 && stats().overruns == 1);
    close_us = release_us = 0;
}
/** @brief Bound zero-cost bursts by turns, expensive turns by time, and idle work by demand. */
static void continuations(void)
{
    assert(!surface_runtime_continue());
    clock_step = 0;
    assert(surface_runtime_start());
    for (unsigned i = 0; i < 32; ++i) {
        assert(surface_runtime_poll());
        assert(surface_runtime_continue() == (i != 31));
    }
    assert(surface_runtime_poll() && surface_runtime_continue());
    now += 2000;
    assert(!surface_runtime_continue());
    clock_step = 2500;
    assert(surface_runtime_poll() && !surface_runtime_continue());
    clock_step = 0;
    poll_result = SURFACE_SCANOUT_OK;
    for (unsigned i = 0; i < 1000 && !stats().completed; ++i) {
        assert(surface_runtime_poll());
        (void)surface_runtime_continue();
    }
    assert(stats().completed == 1 && !surface_runtime_continue());
    assert(surface_runtime_launch(1));
    assert(surface_runtime_poll() && surface_runtime_continue());
    surface_runtime_stop();
    assert(!surface_runtime_continue());
    drain();
    poll_result = SURFACE_SCANOUT_BUSY; clock_step = 100;
}

/** @brief Continue pending media after the browser frame completes, then retire on stop. */
static void media_continuations(void)
{
    clock_step = 0; poll_result = SURFACE_SCANOUT_OK; media_enabled = 1;
    assert(surface_runtime_start() && surface_runtime_launch(2));
    for (unsigned i = 0; i < 2000; ++i) {
        assert(surface_runtime_poll());
        (void)surface_runtime_continue();
    }
    assert(media_view && stats().completed && !pending);
    unsigned completed = stats().completed, continued = 0;
    for (unsigned i = 0; i < (SURFACE_DESKTOP ? 64u : 32u); ++i) {
        assert(surface_runtime_poll());
        continued += surface_runtime_continue() != 0;
    }
    assert(continued == (SURFACE_DESKTOP ? 63u : 31u) && stats().completed == completed);
    drain(); assert(!media_view);
    media_enabled = media_retired = 0; clock_step = 100; poll_result = SURFACE_SCANOUT_BUSY;
}

/** @brief Model queued work with zero CPU cost and 190 delayed publication/handoff polls. */
static void frame_cadence(void)
{
    clock_step = 0; delayed_polls = 190; poll_result = SURFACE_SCANOUT_OK;
    assert(surface_runtime_start());
    unsigned waits = 0;
    for (unsigned i = 0; i < 1000 && !stats().completed; ++i) {
        assert(surface_runtime_poll());
        if (!surface_runtime_continue() && !stats().completed) { ++waits; now += 7813; }
    }
    assert(stats().completed == 1 && waits <= 26 && !delayed_polls);
    drain();
    clock_step = 100; poll_result = SURFACE_SCANOUT_BUSY;
}

#if SURFACE_CONNECTED_ENABLE
/** @brief Poll cached status at 250 ms spacing and stop observation before runtime retirement. */
static void connected_services(void)
{
    unsigned previous_step=clock_step;clock_step=0;now=1000000;service_queries=0;
    assert(surface_runtime_start());assert(surface_runtime_poll());assert(service_queries==1);
    for(unsigned i=0;i<20;i++)assert(surface_runtime_poll());
    assert(service_queries==1);now+=249999;assert(surface_runtime_poll());assert(service_queries==1);
    now++;assert(surface_runtime_poll());assert(service_queries==2);
    int previous_poll=poll_result;poll_result=SURFACE_SCANOUT_OK;
    assert(surface_runtime_launch(3));
    for(unsigned i=0;i<2000;i++)assert(surface_runtime_poll());
    unsigned frames=stats().frames;
    for(unsigned i=0;i<100;i++)assert(surface_runtime_poll());
    assert(frames && stats().frames==frames);
    service_label="Wi-Fi scanning; transfer stopped";now+=250000;
    for(unsigned i=0;i<2000 && stats().frames==frames;i++)assert(surface_runtime_poll());
    assert(stats().frames>frames && service_queries==3);
    surface_runtime_stop();now+=1000000;
    drain();assert(service_queries==3);clock_step=previous_step;poll_result=previous_poll;
    service_label="Wi-Fi joined; transfer stopped";
}
#endif

#if SURFACE_ARITHMETIC
/** @brief Retain the configured tasks inside the firmware allocator and release their leases. */
static void arithmetic_lifetime(void)
{
    unsigned old_clock=clock_step;int old_poll=poll_result;
    clock_step=0;poll_result=SURFACE_SCANOUT_OK;
    assert(surface_runtime_start());
    for(unsigned round=0;round<10;++round)for(unsigned id=1;id<=(SURFACE_IMAGE?13:SURFACE_NOTES?12:SURFACE_DIFF?11:SURFACE_HEX?10:SURFACE_READER?9:SURFACE_VFS?8:SURFACE_DEVICE?7:SURFACE_TERMINAL?6:SURFACE_EDITOR?5:4);++id){
        assert(surface_runtime_launch(id));
        for(unsigned turn=0;turn<500;++turn)assert(surface_runtime_poll());
        assert(stats().foreground==id && !stats().allocation_failures);
    }
    assert(stats().peak_bytes<sizeof region);
    printf("%u-task PSRAM: peak %u of %zu bytes, %u live allocations\n",SURFACE_IMAGE?13:SURFACE_NOTES?12:SURFACE_DIFF?11:SURFACE_HEX?10:SURFACE_READER?9:SURFACE_VFS?8:SURFACE_DEVICE?7:SURFACE_TERMINAL?6:SURFACE_EDITOR?5:4,stats().peak_bytes,sizeof region,stats().allocations);
    drain();clock_step=old_clock;poll_result=old_poll;
}
#endif

#if SURFACE_CONTENT_JOBS && SURFACE_ARITHMETIC && !defined(SURFACE_SETTINGS_REAL_TFS)
/** @brief Route a calibrated touch press and release through the firmware poller. */
static void copy_touch(unsigned x,unsigned y)
{
    touch_report=(surface_touch_report_t){.count=1,.points={{x,y,4}}};
    for(unsigned i=0;i<40;++i){now+=1000;assert(surface_runtime_poll());}
    touch_report.count=0;
    for(unsigned i=0;i<40;++i){now+=1000;assert(surface_runtime_poll());}
    for(unsigned i=0;i<300;++i)assert(surface_runtime_poll());
}
/** @brief Retain a complete copy buffer with the configured applications inside the PSRAM budget. */
static void copy_lifetime(void)
{
    unsigned old_clock=clock_step;int old_poll=poll_result;
    clock_step=0;poll_result=SURFACE_SCANOUT_OK;settings_present=0;
    media_enabled=copy_media=1;media_retired=0;
    assert(surface_runtime_start());
    for(unsigned id=1;id<=(SURFACE_IMAGE?13:SURFACE_NOTES?12:SURFACE_DIFF?11:SURFACE_HEX?10:SURFACE_READER?9:SURFACE_VFS?8:SURFACE_DEVICE?7:SURFACE_TERMINAL?6:SURFACE_EDITOR?5:4);++id){
#if SURFACE_NOTES
        if(id==12){notes_media=1;notes_bytes_read=0;}
#endif
        assert(surface_runtime_launch(id));for(unsigned n=0;n<(SURFACE_NOTES&&id==12?50000u:500u);++n)assert(surface_runtime_poll());
#if SURFACE_NOTES
        if(id==12){assert(notes_bytes_read==24*(sizeof note_header-1+8192+1)&&!stats().allocation_failures);notes_media=0;}
#endif
    }
#if SURFACE_EDITOR_DOCUMENTS
    extern void surface_test_fill_editor_documents(void);
    assert(surface_runtime_launch(5));for(unsigned n=0;n<500;++n)assert(surface_runtime_poll());
    surface_test_fill_editor_documents();assert(!stats().allocation_failures);
    printf("Eight Editor documents and 24 Notes: peak %u of %zu bytes, %u live allocations\n",stats().peak_bytes,sizeof region,stats().allocations);
#endif
#if SURFACE_IMAGE
    extern void surface_test_open_image(void);
    extern int surface_test_image_ready(void);
    assert(surface_runtime_launch(13));for(unsigned n=0;n<500;n++)assert(surface_runtime_poll());
    (void)png_fixture(image_png,sizeof image_png,512,256,sizeof image_png);
    for(unsigned replacement=0;replacement<4;replacement++){
        image_media=replacement<2?1:2;
        surface_test_open_image();unsigned turns=0;
        while(!surface_test_image_ready()){assert(++turns<60000);assert(surface_runtime_poll());}
        assert(!stats().allocation_failures&&stats().peak_bytes<sizeof region);
    }
    image_media=0;
    printf("Maximum Image replacement with eight Editor documents and 24 Notes: peak %u of %zu bytes, %u allocations\n",stats().peak_bytes,sizeof region,stats().allocations);
#endif
#if SURFACE_DIFF
    assert(surface_runtime_launch(11));for(unsigned n=0;n<500;++n)assert(surface_runtime_poll());
    touch_report.count=0;for(unsigned n=0;n<40;++n){now+=1000;assert(surface_runtime_poll());}
    diff_media=1;diff_bytes_read=0;
    for(unsigned side=0;side<2;++side){
        copy_touch(side?480:100,110);copy_touch(160,418);copy_touch(514,274);
        copy_touch(160,418);copy_touch(58,320);copy_touch(700,418);
        for(unsigned n=0;n<8000;++n)assert(surface_runtime_poll());
    }
    assert(diff_bytes_read==16384&&!stats().allocation_failures);diff_media=0;
    copy_touch(680,366);unsigned patch_allocations=stats().allocations;
    touch_report=(surface_touch_report_t){.count=1,.points={{570,150,4}}};
    for(unsigned i=0;i<40;++i){now+=1000;assert(surface_runtime_poll());}
    touch_report.count=0;for(unsigned i=0;i<40;++i){now+=1000;assert(surface_runtime_poll());}
    assert(stats().allocations==patch_allocations+1&&!stats().allocation_failures&&stats().peak_bytes<sizeof region);
    printf("Patch output with retained tasks: peak %u of %zu bytes, %u live allocations\n",stats().peak_bytes,sizeof region,stats().allocations);
    for(unsigned i=0;i<2000;++i)assert(surface_runtime_poll());
    assert(stats().allocations==patch_allocations);copy_touch(140,208);
#endif
#if SURFACE_PREFERENCES
    assert(surface_runtime_launch(3));for(unsigned n=0;n<500;++n)assert(surface_runtime_poll());
    touch_report.count=0;for(unsigned n=0;n<40;++n){now+=1000;assert(surface_runtime_poll());}
    unsigned prior=stats().allocations;copy_touch(400,110);copy_touch(100,165);
    assert(stats().allocations==prior+1&&!stats().allocation_failures);
#endif
    assert(surface_runtime_launch(2));for(unsigned n=0;n<500;++n)assert(surface_runtime_poll());
    touch_report.count=0;for(unsigned n=0;n<40;++n){now+=1000;assert(surface_runtime_poll());}
    copy_touch(70,130);copy_touch(300,410);
    copy_touch(160,418);copy_touch(514,274);copy_touch(160,418);copy_touch(362,366);copy_touch(700,418);
    unsigned n;for(n=0;n<10000&&(!media_view||media_view->operation!=SURFACE_MEDIA_REPLACE);++n)assert(surface_runtime_poll());
    assert(n<10000&&media_view->length==65536&&!strcmp(media_view->name,"b"));
    for(unsigned i=0;i<65536;++i)assert(media_view->bytes[i]==(unsigned char)i);
    assert(stats().peak_bytes<sizeof region&&!stats().allocation_failures);
    printf("%u tasks and copy: peak %u of %zu bytes, %u live allocations\n",SURFACE_IMAGE?13:SURFACE_NOTES?12:SURFACE_DIFF?11:SURFACE_HEX?10:SURFACE_READER?9:SURFACE_VFS?8:SURFACE_DEVICE?7:SURFACE_TERMINAL?6:SURFACE_EDITOR?5:4,stats().peak_bytes,sizeof region,stats().allocations);
#if SURFACE_READER
    assert(surface_runtime_launch(9));for(unsigned i=0;i<500;++i)assert(surface_runtime_poll());
    touch_report.count=0;for(unsigned i=0;i<40;++i){now+=1000;assert(surface_runtime_poll());}
#if SURFACE_IMAGE
    extern const void *surface_test_hold_reader(void);
    extern void surface_test_drop_reader(const void *);
    const void *old_page=surface_test_hold_reader();unsigned single=stats().live_bytes;
    copy_touch(220,410);copy_touch(220,164);
    assert(stats().live_bytes>single+200000&&!stats().allocation_failures);
    surface_test_drop_reader(old_page);
#else
    unsigned single=stats().peak_bytes;copy_touch(220,410);copy_touch(220,164);
    assert(stats().peak_bytes>single+200000&&!stats().allocation_failures);
#endif
    printf("Reader replacement with retained tasks and copy: peak %u of %zu bytes\n",stats().peak_bytes,sizeof region);
#endif
    assert(surface_runtime_launch(4));assert(media_view&&media_view->bytes);
    surface_runtime_stop();drain();assert(!media_view);copy_media=media_enabled=media_retired=0;
    clock_step=old_clock;poll_result=old_poll;
}
#endif

int main(void)
{
#if SURFACE_CONTENT_JOBS && SURFACE_ARITHMETIC && !defined(SURFACE_SETTINGS_REAL_TFS)
    copy_lifetime();
#endif
#if SURFACE_ARITHMETIC
    arithmetic_lifetime();
#endif
#if SURFACE_CONNECTED_ENABLE
    connected_services();
#endif
    startup_failures(); cancellation(); lifecycle(); faults(); stop_timing(); continuations(); frame_cadence(); media_continuations();
    assert(opens && closes && submissions);
    touch_lifecycle();
#ifdef SURFACE_SETTINGS_REAL_TFS
    surface_test_settings_persistence();
#else
    calibration_persistence();
#if SURFACE_PREFERENCES
    desktop_persistence();
#endif
#endif
    puts("LCD runtime: allocation failures, 100 cycles, delayed DMA, cancellation and close ownership passed");
    return 0;
}
