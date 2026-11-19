/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_runtime_lcd.c - composed presentation, display faults and retained PSRAM.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "runtime.h"
#include "display.h"
#include "input.h"
#include "media_io.h"
#include "settings_io.h"
#include "settings.h"
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
static unsigned settings_delay, settings_cancelled;
#endif
static unsigned touch_bus_open, input_failure;
static surface_touch_report_t touch_report;
static unsigned close_us, release_us;
static int media_enabled, media_retired;
static surface_media_io_t *media_view;
int surface_host_allocating;

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
/** @brief Report complete contact release. */
int surface_os_input_poll(surface_touch_report_t *out) { *out = touch_report; return !out->cancel; }

/** @brief Reserve no firmware bridge memory in the unavailable-media fixture. */
size_t surface_os_media_size(void) { return 1; }
/** @brief Initialize the unavailable-media fixture. */
void surface_os_media_init(void *data) { (void)data; }
/** @brief Retain an enumeration when the fixture enables media. */
int surface_os_media_submit(void *data, surface_media_io_t *io)
{ (void)data; if (!media_enabled) return SURFACE_MEDIA_UNAVAILABLE; assert(!media_view); media_view = io; return SURFACE_MEDIA_OK; }
/** @brief Mark a retained request ready for cancelled completion. */
int surface_os_media_cancel(void *data) { (void)data; media_retired = 1; return 1; }
/** @brief Delay enumeration until cancellation permits retirement. */
int surface_os_media_poll(void *data)
{ (void)data; if (!media_view || !media_retired) return 0; media_view->result = SURFACE_MEDIA_END; media_view = NULL; return 1; }

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
    settings_borrowed=bridge;settings_delay=6;settings_cancelled=0;
    memcpy(bridge,data,32);return 1;
}
/** @brief Retain borrowed memory through delayed or cancelled completion. */
int surface_os_settings_poll(void *bridge)
{
    assert(bridge==settings_borrowed && reserved);
    if(settings_delay){--settings_delay;return 1;}
    settings_borrowed=NULL;
    return settings_cancelled ? -1 : surface_os_settings_write(bridge,32);
}
/** @brief Mark cancellation without retiring an outstanding storage callback. */
void surface_os_settings_cancel(void *bridge)
{assert(bridge==settings_borrowed);settings_cancelled=1;}
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
    touch_report.count = 0; now += 10000; assert(surface_runtime_poll());
    surface_runtime_stats_t result; surface_runtime_stats(&result); assert(result.foreground == 1);
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
    for (unsigned i = 0; i < 32; ++i) {
        assert(surface_runtime_poll());
        continued += surface_runtime_continue() != 0;
    }
    assert(continued == 31 && stats().completed == completed);
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

int main(void)
{
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
#endif
    puts("LCD runtime: allocation failures, 100 cycles, delayed DMA, cancellation and close ownership passed");
    return 0;
}
