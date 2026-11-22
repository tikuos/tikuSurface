/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * runtime.c - PSRAM-owned UI runtime and presentation retirement.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "build_identity.h"
#include "runtime.h"
#include "surface.h"
#if SURFACE_LCD_ENABLE
#include "display.h"
#include "presentation.h"
#include "input.h"
#if SURFACE_TERMINAL
#include "console_io.h"
#if SURFACE_DEVICE
#include "device_io.h"
#if SURFACE_VFS
#include "vfs_io.h"
#endif
#endif
#endif
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
    uint64_t frame_started;
    surface_frame_stats_t frame_stats;
    unsigned frame_active, frame_foreground;
    uint64_t request_started, frame_request_started;
    unsigned request_active, request_app, frame_request_active;
    uint32_t frame_request_id;
    surface_touch_t touch;
    surface_input_stats_t input_stats;
#if SURFACE_TERMINAL
    _Alignas(16) unsigned char console_bridge[SURFACE_CONSOLE_BRIDGE_BYTES];
#endif
    surface_touch_map_t saved_map, pending_map;
    unsigned settings_status, settings_pending;
#if SURFACE_PREFERENCES
    surface_preferences_t saved_preferences,pending_preferences;
    unsigned preferences_status,settings_kind;
#endif
    uint64_t next_save;
    _Alignas(8) unsigned char settings_bridge[SURFACE_SETTINGS_IO_BYTES];
    surface_media_t media;
    _Alignas(8) unsigned char media_bridge[SURFACE_MEDIA_BRIDGE_BYTES];
    unsigned touch_open, touch_bus_owned;
    uint64_t next_touch;
    unsigned work_phase;
#if SURFACE_CONNECTED_ENABLE
    char services_label[96];
    uint64_t next_services;
#endif
#endif
    allocation_t slots[SURFACE_EDITOR_DOCUMENTS?160:SURFACE_NOTES?128:SURFACE_DIFF?80:SURFACE_VFS?64:SURFACE_EDITOR?48:32];
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
#if SURFACE_PREFERENCES
    state->settings_kind=0;
#endif
    state->pending_map = *map; state->settings_pending = 1;
    state->settings_status = SURFACE_SETTINGS_PENDING;
    return 1;
}

#if SURFACE_PREFERENCES
/** @brief Copy applied desktop preferences without storage access. */
static unsigned preferences_read(void *context,surface_preferences_t *out)
{runtime_t *s=context;*out=s->saved_preferences;return s->preferences_status;}
/** @brief Serialize desktop and calibration updates through the retained bridge. */
static int preferences_save(void *context,const surface_preferences_t *p)
{
    runtime_t *s=context;
    if(s->stopping||s->settings_pending||!surface_preferences_valid(p)||s->preferences_status==SURFACE_SETTINGS_INCOMPATIBLE||s->preferences_status==SURFACE_SETTINGS_UNAVAILABLE)return 0;
    s->pending_preferences=*p;s->settings_kind=1;s->settings_pending=1;s->preferences_status=SURFACE_SETTINGS_PENDING;return 1;
}
#endif

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
    if (result == SURFACE_SCANOUT_OK) {
        ++state->completed;
        uint64_t elapsed = surface_os_time() - state->frame_started;
        uint32_t duration = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
        state->frame_stats.frame_us = duration;
        if (duration > state->frame_stats.max_frame_us) state->frame_stats.max_frame_us = duration;
        state->frame_stats.foreground = state->frame_foreground;
        state->frame_stats.completed = state->completed;
        if (state->frame_request_active) {
            elapsed = surface_os_time() - state->frame_request_started;
            duration = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
            state->frame_stats.request_us = duration;
            if (duration > state->frame_stats.max_request_us) state->frame_stats.max_request_us = duration;
            state->frame_stats.presented_request = state->frame_request_id;
            state->frame_request_active = 0;
        }
        state->frame_active = 0;
        return TIKU_PRESENT_OK;
    }
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

/** @brief Find a gap in at most one more pass than the configured allocation-record count. */
static void *allocate(void *context, size_t bytes, size_t alignment)
{
    runtime_t *state = context;
    size_t offset = align_up(state->first, alignment);
    unsigned slot = 0;
    ++state->calls;
    if (!bytes || state->calls == state->fail_at) goto fail;
    while (slot < (SURFACE_EDITOR_DOCUMENTS?160:SURFACE_NOTES?128:SURFACE_DIFF?80:SURFACE_VFS?64:SURFACE_EDITOR?48:32) && state->slots[slot].bytes) ++slot;
    if (slot == (SURFACE_EDITOR_DOCUMENTS?160:SURFACE_NOTES?128:SURFACE_DIFF?80:SURFACE_VFS?64:SURFACE_EDITOR?48:32)) goto fail;
    for (unsigned pass = 0; pass <= (SURFACE_EDITOR_DOCUMENTS?160:SURFACE_NOTES?128:SURFACE_DIFF?80:SURFACE_VFS?64:SURFACE_EDITOR?48:32); ++pass) {
        int overlap = 0;
        if (offset > state->capacity || bytes > state->capacity - offset) goto fail;
        for (unsigned i = 0; i < (SURFACE_EDITOR_DOCUMENTS?160:SURFACE_NOTES?128:SURFACE_DIFF?80:SURFACE_VFS?64:SURFACE_EDITOR?48:32); ++i) {
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
            if (offset + bytes > state->peak) state->peak = offset + bytes;
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
    for (unsigned i = 0; i < (SURFACE_EDITOR_DOCUMENTS?160:SURFACE_NOTES?128:SURFACE_DIFF?80:SURFACE_VFS?64:SURFACE_EDITOR?48:32); ++i) {
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
        surface_app_t current; tiku_resource_error_t status;
        surface_activity(runtime->shell,&current,&status);
        unsigned live = 0;
        for (unsigned i = 0; i < (SURFACE_EDITOR_DOCUMENTS?160:SURFACE_NOTES?128:SURFACE_DIFF?80:SURFACE_VFS?64:SURFACE_EDITOR?48:32); ++i) live += runtime->slots[i].bytes != 0;
        *out = (surface_runtime_stats_t){1, (uint32_t)runtime->stopping, runtime->turns,
            runtime->frames, live, (uint32_t)runtime->live, (uint32_t)runtime->peak,
            runtime->last_us, runtime->max_us, runtime->overruns, runtime->failures,
            (uint32_t)current, status == TIKU_RESOURCE_UNAVAILABLE,
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
#if SURFACE_LCD_ENABLE
    uint64_t requested = surface_os_time();
#endif
    runtime_t *state = surface_os_reserve(SURFACE_RUNTIME_BYTES);
    if (!state) return 0;
    memset(state, 0, sizeof *state);
#if SURFACE_LCD_ENABLE
    state->request_started = requested;
    state->request_active = 1;
    state->frame_stats.requested = 1;
#endif
    state->first = align_up(sizeof *state, 64);
    state->peak = state->first;
    state->capacity = SURFACE_RUNTIME_BYTES;
    state->fail_at = failure_at;
    runtime = state;
#if SURFACE_LCD_ENABLE
    static const surface_scanout_ops_t scanout_ops = {scanout_submit, scanout_poll, NULL};
    for (unsigned i = 0; i < 2; ++i) {
        state->scanout[i] = allocate(state, SURFACE_SCANOUT_BYTES, 64);
        if (!state->scanout[i]) goto fail;
    }
    if (!surface_presentation_init(&state->presentation, state->scanout[0], SURFACE_SCANOUT_BYTES,
        state->scanout[1], SURFACE_SCANOUT_BYTES, 0, SURFACE_DESKTOP ? 4096 : 1024,
        &display_sequence, &scanout_ops, state)) goto fail;
#endif
    surface_config_t config = {
        .build_identity = SURFACE_BUILD_ID,
        .platform_name = SURFACE_LCD_ENABLE ? "TikuSurface S31 LCD runtime" : "TikuSurface S31 diagnostic runtime",
        .media_description = "Media: unavailable",
        .services_description = SURFACE_LCD_ENABLE ? "LCD and touch enabled; calibration requires panel qualification" :
            "LCD, touch and settings controls: unavailable",
        .paint_rows = SURFACE_DESKTOP ? 4 : 2,
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
#if SURFACE_TRANSFERS
    config.transfers=surface_os_transfers();
#endif
#endif
#if SURFACE_TERMINAL
    static const tiku_terminal_provider_ops_t console_ops={surface_os_console_open,surface_os_console_close,
        surface_os_console_write,surface_os_console_read,surface_os_console_interrupt};
    if(surface_os_console_size()>sizeof state->console_bridge)goto fail;
    surface_os_console_init(state->console_bridge);config.terminal=(tiku_terminal_provider_t){&console_ops,state->console_bridge};
#if SURFACE_BASIC
    config.program=(surface_program_provider_t){state->console_bridge,surface_os_program_start,surface_os_program_state,surface_os_program_stop};
#endif
#endif
#if SURFACE_DEVICE
    config.device=(surface_device_provider_t){NULL,surface_os_device_read,surface_os_process_read};
#if SURFACE_VFS
    config.vfs=(surface_vfs_provider_t){NULL,surface_os_vfs_list,surface_os_vfs_stat,surface_os_vfs_read,surface_os_vfs_write};
#endif
#endif
    if (surface_os_media_size() > sizeof state->media_bridge) goto fail;
    surface_os_media_init(state->media_bridge);
    config.storage = surface_media_init(&state->media, state->media_bridge);
    state->saved_map = (surface_touch_map_t){0, 799, 0, 479, 0, 0, 0};
    unsigned char record[SURFACE_SETTINGS_BYTES]; size_t length = 0;
    int loaded = surface_os_settings_read(record, sizeof record, &length);
    state->settings_status = surface_settings_load(loaded, record, (unsigned)length, &state->saved_map);
    config.settings = (surface_settings_provider_t){state, settings_read, settings_save};
#if SURFACE_PREFERENCES
    length=0;loaded=surface_os_preferences_read(record,sizeof record,&length);
    state->preferences_status=loaded==1?SURFACE_SETTINGS_DEFAULT:loaded?SURFACE_SETTINGS_UNAVAILABLE:surface_preferences_decode(record,(unsigned)length,&state->saved_preferences);
    config.preferences=(surface_preferences_provider_t){state,preferences_read,preferences_save};
#endif
    config.media_description = "Media: read-only native SD; mount required";
#endif
    memcpy(config.caller.principal, "surface-local", sizeof "surface-local");
#if SURFACE_FILE_JOBS
    config.caller.authenticated=1;
    config.caller.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_CONTENT|TIKU_RESOURCE_WRITE;
#if SURFACE_DIRECTORIES
    config.caller.scopes|=TIKU_RESOURCE_HOST_CONTROL;
#endif
#if SURFACE_LCD_ENABLE
    config.media_description="Media: native SD file operations; mount required";
#endif
#endif
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
    if (!runtime || id > SURFACE_APP_LAST || runtime->stopping) return 0;
#if SURFACE_LCD_ENABLE
    uint64_t requested = surface_os_time();
    surface_touch_cancel(&runtime->touch, runtime->shell);
#endif
    int accepted = surface_launch(runtime->shell, (surface_app_t)id);
#if SURFACE_LCD_ENABLE
    if (accepted) {
        runtime->request_started = requested;
        runtime->request_app = id;
        runtime->request_active = 1;
        ++runtime->frame_stats.requested;
    }
#endif
    return accepted;
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

int surface_runtime_touch_log(char *buffer, size_t capacity)
{
    (void)buffer; (void)capacity;
    return 0;
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

int surface_runtime_preferences(surface_preferences_t *out,unsigned *status)
{
#if SURFACE_LCD_ENABLE && SURFACE_PREFERENCES
    if(!runtime||!out||!status)return 0;
    *status=preferences_read(runtime,out);return 1;
#else
    (void)out;(void)status;return 0;
#endif
}
int surface_runtime_set_preferences(const surface_preferences_t *p)
{
#if SURFACE_LCD_ENABLE && SURFACE_PREFERENCES
    return runtime&&preferences_save(runtime,p);
#else
    (void)p;return 0;
#endif
}

int surface_runtime_continue(void)
{
#if SURFACE_LCD_ENABLE
    runtime_t *state = runtime;
    if (state && !state->stopping && state->burst_turns && state->burst_turns < (SURFACE_DESKTOP ? 64u : 32u) &&
        surface_os_time() - state->burst_start < (SURFACE_DESKTOP ? 20000u : 2000u) &&
        (state->presentation.source.owner || surface_render_pending(state->shell) || state->media.request ||
         state->settings_pending>=2 || (state->settings_pending==1 && surface_os_time()>=state->next_save))) return 1;
    if (state) state->burst_turns = 0;
#endif
    return 0;
}

int surface_runtime_frame_stats(surface_frame_stats_t *out)
{
#if SURFACE_LCD_ENABLE
    if (!runtime || !out) return 0;
    *out = runtime->frame_stats;
    out->pending = runtime->frame_active || surface_render_pending(runtime->shell);
    return 1;
#else
    (void)out;
    return 0;
#endif
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
            /* A poll with no report leaves the contact where the last report
             * put it; surface_touch_expire() ends one that gets none. */
            if (valid != SURFACE_OS_INPUT_NONE) {
                state->input_stats.errors += !valid || report.cancel;
                state->input_stats.contacts = valid && !report.cancel ? report.count : 0;
                if (valid && !report.cancel && report.count && report.count <= SURFACE_TOUCH_CONTACTS) {
                    state->input_stats.id = report.points[0].id;
                    state->input_stats.x = report.points[0].x;
                    state->input_stats.y = report.points[0].y;
                }
                surface_touch_report(&state->touch, state->shell, &report, started);
            }
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
#if SURFACE_PREFERENCES
            if(state->settings_kind)rc=surface_preferences_encode(&state->pending_preferences,record)?surface_os_preferences_begin(state->settings_bridge,record,sizeof record):-1;
            else
#endif
            rc=surface_settings_encode(&state->pending_map,record) ?
                surface_os_settings_begin(state->settings_bridge,record,sizeof record) : -1;
            if (rc==1) state->settings_pending=2;
        } else rc=surface_os_settings_poll(state->settings_bridge);
        if (rc!=1) {
            state->settings_pending=0;
            state->next_save=started>UINT64_MAX-5000000 ? UINT64_MAX : started+5000000;
#if SURFACE_PREFERENCES
            if(state->settings_kind){
                state->preferences_status=rc==0?SURFACE_SETTINGS_SAVED:SURFACE_SETTINGS_FAILED;
                if(!rc){state->saved_preferences=state->pending_preferences;if(!state->stopping)surface_preferences_refresh(state->shell);}
            }else
#endif
            {
            state->settings_status=rc==0 ? SURFACE_SETTINGS_SAVED : SURFACE_SETTINGS_FAILED;
            if (!rc) {
                state->saved_map=state->pending_map;
                if (!state->stopping) {
                    surface_touch_cancel(&state->touch,state->shell);
                    state->touch.map=state->saved_map;
                }
            }
            }
        }
    }
    if (!state->stopping && !io_turn && state->media.request && (work&3u)==0) {
        surface_media_poll(&state->media);
        io_turn = 1;
    }
#if SURFACE_DESKTOP
    int stepped = !state->stopping;
    if (stepped) surface_step(state->shell);
#endif
    if (!state->burst_turns) state->burst_start = started;
    if (state->burst_turns < (SURFACE_DESKTOP ? 64u : 32u)) ++state->burst_turns;
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
        } else if (!io_turn) {
            if (!state->frame_active && surface_render_pending(state->shell)) {
                surface_app_t current; tiku_resource_error_t status;
                surface_activity(state->shell, &current, &status);
                state->frame_started = started;
                state->frame_foreground = current;
                state->frame_active = 1;
                if (state->request_active && state->request_app == (unsigned)current) {
                    state->frame_request_started = state->request_started;
                    state->frame_request_id = state->frame_stats.requested;
                    state->frame_request_active = 1;
                    state->request_active = 0;
                }
            }
            (void)surface_render(state->shell, (tiku_presenter_t){present, state});
        }
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
#if SURFACE_LCD_ENABLE && SURFACE_DESKTOP
    if (!stepped) surface_step(state->shell);
#else
    surface_step(state->shell);
#endif
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
