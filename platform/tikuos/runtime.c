/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * runtime.c - PSRAM-owned UI runtime and presentation retirement.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "runtime.h"
#include "surface.h"
#if SURFACE_LCD_ENABLE
#include "display.h"
#include "presentation.h"
#include "input.h"
#include "touch.h"
#include "media.h"
#include "settings_io.h"
#endif
#include <string.h>
#if SURFACE_LCD_ENABLE && SURFACE_CONNECTED_ENABLE
#include "services.h"
#endif

/** @brief One bounded allocation record inside the reserved PSRAM region. */
typedef struct { size_t offset, bytes, alignment; } allocation_t;
/** @brief Runtime state and allocations share one exclusively owned PSRAM span. */
typedef struct {
    surface_t *shell;
    tiku_present_view_t frame;
#if SURFACE_LCD_ENABLE
    surface_presentation_t presentation;
    uint16_t *scanout[2];
    unsigned display_attempted, completed, display_errors;
    uint64_t burst_start;
    unsigned burst_turns;
    surface_touch_t touch;
    surface_input_stats_t input_stats;
    surface_touch_map_t saved_map, pending_map;
    unsigned settings_status, settings_pending;
    uint64_t next_save;
    _Alignas(8) unsigned char settings_bridge[SURFACE_SETTINGS_IO_BYTES];
    surface_media_t media;
    _Alignas(8) unsigned char media_bridge[8192];
    unsigned touch_open, touch_bus_owned;
    uint64_t next_touch;
    unsigned work_phase;
#if SURFACE_CONNECTED_ENABLE
    char services_label[96];
    uint64_t next_services;
#endif
#endif
    allocation_t slots[32];
    size_t first, capacity, live, peak;
    unsigned calls, fail_at, failures;
    int stopping;
    uint32_t turns, frames, last_us, max_us, overruns;
} runtime_t;
static runtime_t *runtime;
static surface_runtime_stats_t last;
static unsigned failure_at;
#if SURFACE_LCD_ENABLE
static uint64_t display_sequence;

/** @brief Copy accepted calibration and persistence state without storage I/O. */
static unsigned settings_read(void *context, surface_touch_map_t *out)
{
    runtime_t *state = context;
    *out = state->saved_map;
    return state->settings_status;
}
/** @brief Queue one explicit calibration update; refuse unknown schemas and overlapping saves. */
static int settings_save(void *context, const surface_touch_map_t *map)
{
    runtime_t *state = context;
    if (state->stopping || state->settings_pending || !surface_touch_map_valid(map) ||
        state->settings_status == SURFACE_SETTINGS_INCOMPATIBLE ||
        state->settings_status == SURFACE_SETTINGS_UNAVAILABLE) return 0;
    state->pending_map = *map; state->settings_pending = 1;
    state->settings_status = SURFACE_SETTINGS_PENDING;
    return 1;
}

/** @brief Submit the converted back buffer without transferring ownership on refusal. */
static tiku_present_result_t scanout_submit(void *context, unsigned back, tiku_rect_t r, uint64_t token)
{
    (void)context;
    int result = surface_os_display_submit(back, (unsigned)r.x, (unsigned)r.y,
        (unsigned)r.w, (unsigned)r.h, token);
    return result == SURFACE_SCANOUT_OK ? TIKU_PRESENT_OK :
        result == SURFACE_SCANOUT_BUSY ? TIKU_PRESENT_BUSY : TIKU_PRESENT_ERROR;
}

/** @brief Count completed frames and retain refused polls until hardware close. */
static tiku_present_result_t scanout_poll(void *context, uint64_t token)
{
    runtime_t *state = context;
    int result = surface_os_display_poll(token);
    if (result == SURFACE_SCANOUT_OK) { ++state->completed; return TIKU_PRESENT_OK; }
    if (result == SURFACE_SCANOUT_ERROR) return TIKU_PRESENT_ERROR;
    if (result != SURFACE_SCANOUT_BUSY) surface_runtime_stop();
    return TIKU_PRESENT_BUSY;
}
#endif

/** @brief Return an aligned candidate or SIZE_MAX on invalid alignment/overflow. */
static size_t align_up(size_t value, size_t alignment)
{
    if (!alignment || (alignment & (alignment - 1)) || alignment > 64 || value > SIZE_MAX - alignment + 1) return SIZE_MAX;
    return (value + alignment - 1) & ~(alignment - 1);
}

/** @brief Find a gap in at most 33 passes over 32 fixed allocation records. */
static void *allocate(void *context, size_t bytes, size_t alignment)
{
    runtime_t *state = context;
    size_t offset = align_up(state->first, alignment);
    unsigned slot = 0;
    ++state->calls;
    if (!bytes || state->calls == state->fail_at) goto fail;
    while (slot < 32 && state->slots[slot].bytes) ++slot;
    if (slot == 32) goto fail;
    for (unsigned pass = 0; pass <= 32; ++pass) {
        int overlap = 0;
        if (offset > state->capacity || bytes > state->capacity - offset) goto fail;
        for (unsigned i = 0; i < 32; ++i) {
            allocation_t *entry = &state->slots[i];
            if (entry->bytes && offset < entry->offset + entry->bytes && entry->offset < offset + bytes) {
                offset = align_up(entry->offset + entry->bytes, alignment);
                overlap = 1;
                break;
            }
        }
        if (!overlap) {
            state->slots[slot] = (allocation_t){offset, bytes, alignment};
            state->live += bytes;
            if (state->live > state->peak) state->peak = state->live;
            return (unsigned char *)state + offset;
        }
    }
fail:
    ++state->failures;
    return NULL;
}

/** @brief Release a matching allocation; a contract mismatch keeps it retained. */
static void release(void *context, void *pointer, size_t bytes, size_t alignment)
{
    runtime_t *state = context;
    for (unsigned i = 0; i < 32; ++i) {
        allocation_t *entry = &state->slots[i];
        if (entry->bytes && (unsigned char *)state + entry->offset == pointer &&
            entry->bytes == bytes && entry->alignment == alignment) {
            state->live -= bytes;
            *entry = (allocation_t){0};
            return;
        }
    }
    ++state->failures;
}

/** @brief Adapt the firmware microsecond clock without sharing clock typedefs. */
static int read_clock(void *context, uint64_t *now)
{ (void)context; *now = surface_os_time(); return 1; }

#if !SURFACE_LCD_ENABLE
/** @brief Report diagnostic media as unavailable without retaining a request. */
static tiku_resource_error_t storage_submit(void *context, tiku_paged_storage_t *owner,
                                             const tiku_storage_request_t *request)
{ (void)context; (void)owner; (void)request; return TIKU_RESOURCE_UNAVAILABLE; }
/** @brief The unavailable provider has no accepted requests to cancel. */
static void storage_cancel(void *context, tiku_paged_storage_t *owner, uint64_t token)
{ (void)context; (void)owner; (void)token; }
#endif
/** @brief Retain a rendered view for the selected presentation provider. */
static tiku_present_result_t present(void *context, const tiku_present_view_t *view)
{
    runtime_t *state = context;
#if SURFACE_LCD_ENABLE
    tiku_present_result_t result = surface_presentation_submit(&state->presentation, view);
    if (result == TIKU_PRESENT_OK) ++state->frames;
    return result;
#else
    if (state->frame.owner) return TIKU_PRESENT_BUSY;
    state->frame = *view;
    ++state->frames;
    return TIKU_PRESENT_OK;
#endif
}

void surface_runtime_stats(surface_runtime_stats_t *out)
{
    if (!out) return;
    *out = last;
    if (runtime) {
        surface_view_t view;
        surface_view(runtime->shell, &view);
        unsigned live = 0;
        for (unsigned i = 0; i < 32; ++i) live += runtime->slots[i].bytes != 0;
        *out = (surface_runtime_stats_t){1, (uint32_t)runtime->stopping, runtime->turns,
            runtime->frames, live, (uint32_t)runtime->live, (uint32_t)runtime->peak,
            runtime->last_us, runtime->max_us, runtime->overruns, runtime->failures,
            (uint32_t)view.foreground, view.status == TIKU_RESOURCE_UNAVAILABLE,
            SURFACE_LCD_ENABLE, 0, 0};
#if SURFACE_LCD_ENABLE
        out->completed = runtime->completed;
        out->display_errors = runtime->display_errors;
#endif
    }
}

void surface_runtime_fail_at(unsigned attempt)
{ failure_at = attempt; if (runtime) { runtime->fail_at = attempt; runtime->calls = 0; } }

int surface_runtime_start(void)
{
    static const tiku_allocator_ops_t allocator = {allocate, release};
#if !SURFACE_LCD_ENABLE
    static const tiku_storage_provider_ops_t storage = {storage_submit, storage_cancel};
#endif
    if (runtime) return 0;
    runtime_t *state = surface_os_reserve(SURFACE_RUNTIME_BYTES);
    if (!state) return 0;
    memset(state, 0, sizeof *state);
    state->first = align_up(sizeof *state, 64);
    state->capacity = SURFACE_RUNTIME_BYTES;
    state->fail_at = failure_at;
    runtime = state;
#if SURFACE_LCD_ENABLE
    static const surface_scanout_ops_t scanout_ops = {scanout_submit, scanout_poll};
    for (unsigned i = 0; i < 2; ++i) {
        state->scanout[i] = allocate(state, SURFACE_SCANOUT_BYTES, 64);
        if (!state->scanout[i]) goto fail;
    }
    if (!surface_presentation_init(&state->presentation, state->scanout[0], SURFACE_SCANOUT_BYTES,
        state->scanout[1], SURFACE_SCANOUT_BYTES, 0, 1024, &display_sequence, &scanout_ops, state)) goto fail;
#endif
    surface_config_t config = {
        .platform_name = SURFACE_LCD_ENABLE ? "TikuSurface S31 LCD runtime" : "TikuSurface S31 diagnostic runtime",
        .media_description = "Media: unavailable",
        .services_description = SURFACE_LCD_ENABLE ? "LCD and touch enabled; calibration requires panel qualification" :
            "LCD, touch and settings controls: unavailable",
        .paint_rows = 2,
        .allocator = {&allocator, state}, .clock = {read_clock, state, 0},
#if !SURFACE_LCD_ENABLE
        .storage = {&storage, state, TIKU_RESOURCE_FEATURE_READ | TIKU_RESOURCE_FEATURE_COLLECTION | TIKU_RESOURCE_FEATURE_STREAM}
#endif
    };
#if SURFACE_LCD_ENABLE
#if SURFACE_CONNECTED_ENABLE
    memcpy(state->services_label,"Wi-Fi unavailable; transfer unavailable",
           sizeof "Wi-Fi unavailable; transfer unavailable");
    config.services_description=state->services_label;
#endif
    if (surface_os_media_size() > sizeof state->media_bridge) goto fail;
    surface_os_media_init(state->media_bridge);
    config.storage = surface_media_init(&state->media, state->media_bridge);
    state->saved_map = (surface_touch_map_t){0, 799, 0, 479, 0, 0, 0};
    unsigned char record[SURFACE_SETTINGS_BYTES]; size_t length = 0;
    int loaded = surface_os_settings_read(record, sizeof record, &length);
    state->settings_status = loaded == 1 ? SURFACE_SETTINGS_DEFAULT : loaded ? SURFACE_SETTINGS_UNAVAILABLE :
        surface_settings_decode(record, (unsigned)length, &state->saved_map);
    config.settings = (surface_settings_provider_t){state, settings_read, settings_save};
    config.media_description = "Media: read-only native SD; mount required";
#endif
    memcpy(config.caller.principal, "surface-local", sizeof "surface-local");
    state->shell = surface_create(&config);
    if (!state->shell) goto fail;
#if SURFACE_LCD_ENABLE
    memset(state->scanout[0], 0, SURFACE_SCANOUT_BYTES);
    state->display_attempted = 1;
    if (!surface_os_display_open(state->scanout[0], state->scanout[1])) {
        ++state->display_errors;
        goto fail;
    }
#endif
#if SURFACE_LCD_ENABLE
    if (!surface_touch_init(&state->touch, &state->saved_map)) goto fail;
    state->touch_open = surface_os_input_open(&state->touch_bus_owned);
    if (!state->touch_open) goto fail;
#endif
    last = (surface_runtime_stats_t){0};
    return 1;
fail:
    surface_runtime_stop();
    (void)surface_runtime_poll();
    return 0;
}

void surface_runtime_stop(void)
{
    if (runtime) {
        runtime->stopping = 1;
#if SURFACE_LCD_ENABLE
        surface_touch_cancel(&runtime->touch, runtime->shell);
#endif
        surface_stop(runtime->shell);
    }
}

int surface_runtime_launch(unsigned id)
{
    if (!runtime || id > SURFACE_SETTINGS || runtime->stopping) return 0;
#if SURFACE_LCD_ENABLE
    surface_touch_cancel(&runtime->touch, runtime->shell);
#endif
    return surface_launch(runtime->shell, (surface_app_t)id);
}

int surface_runtime_input_stats(surface_input_stats_t *out)
{
#if SURFACE_LCD_ENABLE
    if (!runtime || !out) return 0;
    *out = runtime->input_stats;
    return 1;
#else
    (void)out;
    return 0;
#endif
}

int surface_runtime_calibration(surface_touch_map_t *map, unsigned *status)
{
#if SURFACE_LCD_ENABLE
    if (!runtime || !map || !status) return 0;
    *status = settings_read(runtime, map);
    return 1;
#else
    (void)map; (void)status;
    return 0;
#endif
}

int surface_runtime_calibrate(const surface_touch_map_t *map)
{
#if SURFACE_LCD_ENABLE
    return runtime && settings_save(runtime, map);
#else
    (void)map;
    return 0;
#endif
}

int surface_runtime_continue(void)
{
#if SURFACE_LCD_ENABLE
    runtime_t *state = runtime;
    if (state && !state->stopping && state->burst_turns && state->burst_turns < 32 &&
        surface_os_time() - state->burst_start < 2000 &&
        (state->presentation.source.owner || surface_render_pending(state->shell) || state->media.request ||
         state->settings_pending>=2 || (state->settings_pending==1 && surface_os_time()>=state->next_save))) return 1;
    if (state) state->burst_turns = 0;
#endif
    return 0;
}

int surface_runtime_poll(void)
{
    runtime_t *state = runtime;
    if (!state) return 0;
    uint64_t started = surface_os_time();
#if SURFACE_LCD_ENABLE
    int io_turn = 0;
#if SURFACE_CONNECTED_ENABLE
    if (!state->stopping && started>=state->next_services) {
        char label[sizeof state->services_label]={0};
        surface_os_services(label,sizeof label);
        if (strcmp(label,state->services_label)) {
            memcpy(state->services_label,label,sizeof label);
            surface_services_changed(state->shell);
        }
        state->next_services=started>UINT64_MAX-250000 ? UINT64_MAX : started+250000;
    }
#endif
    if (state->stopping && state->settings_pending==1) state->settings_pending=0;
    if (state->stopping && state->settings_pending==2) {
        surface_os_settings_cancel(state->settings_bridge);
        state->settings_pending=3;
        io_turn=1;
    }
    if (!state->stopping && state->touch_open) {
        surface_touch_expire(&state->touch, state->shell, started);
        if (!io_turn && started >= state->next_touch) {
            io_turn = 1;
            surface_touch_report_t report;
            int valid = surface_os_input_poll(&report);
            ++state->input_stats.reports;
            state->input_stats.errors += !valid || report.cancel;
            state->input_stats.contacts = valid && !report.cancel ? report.count : 0;
            if (valid && !report.cancel && report.count && report.count <= SURFACE_TOUCH_CONTACTS) {
                state->input_stats.id = report.points[0].id;
                state->input_stats.x = report.points[0].x;
                state->input_stats.y = report.points[0].y;
            }
            surface_touch_report(&state->touch, state->shell, &report, started);
            state->next_touch = started > UINT64_MAX - 10000 ? UINT64_MAX : started + 10000;
        }
    }
    unsigned work=state->work_phase++;
    if (!io_turn && state->settings_pending &&
        (state->stopping || ((work&1u) && started>=state->next_save)) &&
        (state->settings_pending>=2 || !state->presentation.source.owner)) {
        io_turn=1;
        int rc;
        if (state->settings_pending==1) {
            unsigned char record[SURFACE_SETTINGS_BYTES];
            rc=surface_settings_encode(&state->pending_map,record) ?
                surface_os_settings_begin(state->settings_bridge,record,sizeof record) : -1;
            if (rc==1) state->settings_pending=2;
        } else rc=surface_os_settings_poll(state->settings_bridge);
        if (rc!=1) {
            state->settings_pending=0;
            state->settings_status=rc==0 ? SURFACE_SETTINGS_SAVED : SURFACE_SETTINGS_FAILED;
            state->next_save=started>UINT64_MAX-5000000 ? UINT64_MAX : started+5000000;
            if (!rc) {
                state->saved_map=state->pending_map;
                if (!state->stopping) {
                    surface_touch_cancel(&state->touch,state->shell);
                    state->touch.map=state->saved_map;
                }
            }
        }
    }
    if (!state->stopping && !io_turn && state->media.request && (work&3u)==0) {
        surface_media_poll(&state->media);
        io_turn = 1;
    }
    if (!state->burst_turns) state->burst_start = started;
    if (state->burst_turns < 32) ++state->burst_turns;
    if (!state->stopping) {
        if (!surface_os_display_health()) {
            ++state->display_errors;
            surface_runtime_stop();
        } else if (!io_turn && state->presentation.source.owner) {
            (void)surface_presentation_poll(&state->presentation);
            if (state->presentation.failed || state->stopping) {
                ++state->display_errors;
                surface_runtime_stop();
            }
        } else if (!io_turn) (void)surface_render(state->shell, (tiku_presenter_t){present, state});
    }
    if (state->stopping && state->touch_open) {
        surface_os_input_close(&state->touch_bus_owned);
        state->touch_open = 0;
    }
    if (state->stopping && (!state->display_attempted || surface_os_display_close())) {
        state->display_attempted = 0;
        if (surface_presentation_closed(&state->presentation)) {
            for (unsigned i = 0; i < 2; ++i) if (state->scanout[i]) {
                release(state, state->scanout[i], SURFACE_SCANOUT_BYTES, 64);
                state->scanout[i] = NULL;
            }
        }
    }
#else
    if (state->frame.owner) {
        (void)tiku_framebuffer_complete(state->frame.owner, state->frame.token, TIKU_PRESENT_OK);
        state->frame = (tiku_present_view_t){0};
    }
#endif
#if SURFACE_LCD_ENABLE
    if (state->stopping) surface_media_poll(&state->media);
#endif
    surface_step(state->shell);
#if !SURFACE_LCD_ENABLE
    if (!state->stopping) (void)surface_render(state->shell, (tiku_presenter_t){present, state});
#endif
    ++state->turns;
    if (state->stopping &&
#if SURFACE_LCD_ENABLE
        !state->settings_pending &&
#endif
        surface_destroy(&state->shell) && !state->live) {
        surface_runtime_stats(&last);
        if (surface_os_release(state)) {
            runtime = NULL;
            last.running = last.stopping = last.allocations = last.live_bytes = 0;
        }
    }
    uint64_t elapsed = surface_os_time() - started;
    uint32_t duration = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
    if (runtime) {
        state->last_us = duration;
        if (duration > state->max_us) state->max_us = duration;
        if (duration > 5000) ++state->overruns;
    } else {
        last.last_dispatch_us = duration;
        if (duration > last.max_dispatch_us) last.max_dispatch_us = duration;
        if (duration > 5000) ++last.overruns;
    }
    return runtime != NULL;
}
