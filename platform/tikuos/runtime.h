/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * runtime.h - neutral boundary between the UI SDK and firmware bindings.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_RUNTIME_H
#define SURFACE_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
#include "../../shell/touch_report.h"
#include "../../shell/settings.h"
#ifndef SURFACE_LCD_ENABLE
#define SURFACE_LCD_ENABLE 0
#endif
#ifndef SURFACE_CONNECTED_ENABLE
#define SURFACE_CONNECTED_ENABLE 0
#endif
/** @brief Input counters and the first raw contact from the most recent nonempty report. */
typedef struct { uint32_t reports, errors, contacts, id, x, y; } surface_input_stats_t;
/** @brief Copy input observations without bus access; zero means no LCD runtime. */
int surface_runtime_input_stats(surface_input_stats_t *);
/** @brief Reserved UI PSRAM, including pixels, app allowance and pool metadata. */
#ifndef SURFACE_DESKTOP
#define SURFACE_DESKTOP 0
#endif
#ifndef SURFACE_READER
#define SURFACE_READER 0
#endif
#ifndef SURFACE_EDITOR_DOCUMENTS
#define SURFACE_EDITOR_DOCUMENTS 0
#endif
#ifndef SURFACE_IMAGE
#define SURFACE_IMAGE 0
#endif
#ifndef SURFACE_NOTES
#define SURFACE_NOTES 0
#endif
#ifndef SURFACE_DIFF
#define SURFACE_DIFF 0
#endif
#define SURFACE_RUNTIME_BYTES (1536000u + (SURFACE_IMAGE?8388608u:SURFACE_NOTES?3145728u:SURFACE_DIFF?1048576u:SURFACE_READER?786432u:SURFACE_DESKTOP?786432u:262144u) + 65536u + 4096u + SURFACE_LCD_ENABLE * 1536000u)
/** @brief Diagnostic counters copied without exposing SDK types to firmware.
 * @note live_bytes counts allocation payloads. peak_bytes is the highest used
 * offset in the reserved region, including runtime state, alignment and gaps. */
typedef struct {
    uint32_t running, stopping, turns, frames, allocations, live_bytes, peak_bytes;
    uint32_t last_dispatch_us, max_dispatch_us, overruns, allocation_failures;
    uint32_t foreground, media_unavailable;
    uint32_t lcd, completed, display_errors;
} surface_runtime_stats_t;
/** @brief Completed paint-to-handoff duration and outstanding frame work. */
typedef struct {
    uint32_t frame_us, max_frame_us, foreground, completed, pending;
    uint32_t request_us, max_request_us, requested, presented_request;
} surface_frame_stats_t;
/** @brief Copy live LCD frame counters; zero means no running LCD runtime. */
int surface_runtime_frame_stats(surface_frame_stats_t *out);
/** @brief Reserve the UI region and construct the selected runtime; 0 on failure. */
int surface_runtime_start(void);
/** @brief Request stop; completion and storage retirement continue on owner turns. */
void surface_runtime_stop(void);
/** @brief Dispatch one owner turn; return nonzero while resources remain owned. */
int surface_runtime_poll(void);
/** @brief Continue within 32 turns and 20 ms for desktop or 2 ms for launcher. */
int surface_runtime_continue(void);
/** @brief Request an app by public numeric ID; refuse invalid IDs or stopped state. */
int surface_runtime_launch(unsigned id);
/** @brief Queue measured calibration for persistent save; diagnostic or stopping runtimes refuse. */
int surface_runtime_calibrate(const surface_touch_map_t *);
/** @brief Copy active calibration and persistence status; zero means no LCD runtime. */
int surface_runtime_calibration(surface_touch_map_t *, unsigned *status);
/** @brief Copy applied desktop choices and persistence status; zero means unavailable. */
int surface_runtime_preferences(surface_preferences_t *,unsigned *);
/** @brief Queue one explicit desktop update; serialized with calibration writes. */
int surface_runtime_set_preferences(const surface_preferences_t *);
/** @brief Copy runtime counters, including the completed stop result. */
void surface_runtime_stats(surface_runtime_stats_t *out);
/** @brief Fail the specified one-based allocation attempt; zero disables injection. */
void surface_runtime_fail_at(unsigned attempt);
/** @brief Firmware PSRAM reservation, 64-byte aligned; exclusive until release. */
void *surface_os_reserve(size_t bytes);
/** @brief Release that reservation after its occupants retire; 0 keeps it owned. */
int surface_os_release(void *base);
/** @brief Read monotonic microseconds in the kernel owner context. */
uint64_t surface_os_time(void);
/** @brief Return nonzero in serialized kernel execution outside an interrupt. */
int surface_os_in_owner(void);
/** @brief Copy unreserved PSRAM bytes; zero reports an unavailable tier. */
int surface_os_free_bytes(uint32_t *bytes);
#endif
