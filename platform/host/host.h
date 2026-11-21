/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * host.h - deterministic allocator, clock, media and delayed presenter.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_HOST_H
#define SURFACE_HOST_H
#include "surface.h"

/** @brief Allocation accounting and one-based injected failure position. */
typedef struct {
    size_t live, bytes, peak, calls, fail_at;
    struct { void *pointer; size_t bytes, alignment; } records[SURFACE_EDITOR_DOCUMENTS?160:SURFACE_NOTES?128:SURFACE_DIFF?80:64];
} surface_host_memory_t;
/** @brief Fixed host providers; advance time and poll from the owner thread. */
typedef struct {
#if SURFACE_TERMINAL
#if SURFACE_BASIC
    char program_source[8193];
    int program_state,program_busy,program_stops;
#endif
    int terminal_open,terminal_close_delay,terminal_write_limit,terminal_error;
    size_t terminal_in_size,terminal_out_size;
    char terminal_input[1024],terminal_output[4096];
#endif
#if SURFACE_VFS
    unsigned vfs_reads,vfs_writes,vfs_lists,vfs_stats;
    int vfs_fail_read,vfs_fail_write,vfs_readonly;
    char vfs_value[256];
#endif
#if SURFACE_DEVICE
    unsigned device_reads,process_reads;
    int process_missing,process_error;
    int device_failed;
    uint32_t device_used;
#endif
    surface_host_memory_t memory;
    uint64_t now, storage_delay, present_delay, epoch;
    int clock_error, unavailable, present_error, present_busy;
    struct {
        tiku_paged_storage_t *owner;
        const tiku_storage_request_t *request;
        uint64_t due;
        int cancelled;
    } request;
    tiku_present_view_t frame;
    uint64_t frame_due;
    unsigned submissions, completions, cancellations;
} surface_host_t;
/** @brief Initialize fixtures and return provider bindings for shell creation. */
surface_config_t surface_host_config(surface_host_t *);
/** @brief Return the deferred memory presenter. */
tiku_presenter_t surface_host_presenter(surface_host_t *);
/** @brief Complete due storage/frame requests; returns the number completed. */
unsigned surface_host_poll(surface_host_t *);
/** @brief Copy an accepted frame into a PPM file before completing it. */
int surface_host_capture(surface_host_t *, const char *path);
/** @brief Whether direct heap calls belong to the host allocator provider. */
extern volatile int surface_host_allocating;
#endif
