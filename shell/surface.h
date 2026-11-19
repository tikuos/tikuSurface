/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * surface.h - serialized local shell and static application ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_H
#define SURFACE_H
#include "tiku_clock.h"
#include "tiku_framebuffer.h"
#include "tiku_paged_storage.h"
#include "tiku_input.h"
#include "settings.h"

/** @brief Static application identities; HOME closes the foreground owner. */
typedef enum { SURFACE_HOME, SURFACE_CALCULATOR, SURFACE_BROWSER, SURFACE_SETTINGS } surface_app_t;
/** @brief Opaque owner; call operations and provider completions on one thread. */
typedef struct surface surface_t;
/** @brief Providers and their contexts outlive the shell and pending work. */
typedef struct {
    tiku_allocator_t allocator;
    tiku_clock_t clock;
    tiku_storage_provider_t storage;
    tiku_resource_caller_t caller;
    const char *platform_name, *media_description, *services_description;
    surface_settings_provider_t settings;
    unsigned paint_rows; /**< rows per render call, 1..480; zero paints the full frame */
} surface_config_t;
/** @brief Copied presentation state; strings remain valid independently of apps. */
typedef struct {
    surface_app_t foreground, pending;
    int retiring, stopped, pressed, clock_failed, display_failed;
    tiku_resource_error_t status;
    uint64_t now;
    char platform_name[64], media_description[96], services_description[96];
    char hex[17], bases[4][65];
    char path[TIKU_RESOURCE_PATH_MAX], rows[6][64], preview[1025];
    surface_touch_map_t calibration;
    unsigned settings_status;
    unsigned row_count;
    int directory[6], more, previous, reading, preview_hex;
    size_t preview_bytes;
} surface_view_t;
/** @brief Construct an 800 by 480 shell; failure releases partial allocations. */
surface_t *surface_create(const surface_config_t *config);
/** @brief Coalesce a launch, invalidate queued input and reject invalid IDs/stop. */
int surface_launch(surface_t *, surface_app_t);
/** @brief Enqueue input; overflow cancels the active interaction. */
int surface_post(surface_t *, const tiku_input_event_t *);
/** @brief Process at most eight shell events, four app events and one storage step. */
void surface_step(surface_t *);
/**
 * @brief Paint up to paint_rows from one copied view; BUSY includes partial work.
 * @note Changes during painting queue another frame. Accepted frames remain
 * immutable until completion; stop abandons a partial frame.
 */
tiku_present_result_t surface_render(surface_t *, tiku_presenter_t);
/** @brief Return nonzero for queued repaint or partial painting on a running shell. */
int surface_render_pending(const surface_t *);
/** @brief Copy visible state; a NULL owner yields a stopped HOME view. */
void surface_view(const surface_t *, surface_view_t *);
/** @brief Stop input and applications; provider requests retain their memory. */
void surface_stop(surface_t *);
/** @brief Destroy a stopped shell after retirement; busy leaves pointer intact. */
int surface_destroy(surface_t **);
/** @brief Repaint visible Device information after its borrowed service label changes.
 * @note Call from the serialized owner; the configured label storage outlives the shell.
 * A partial frame retains its frozen label until completion.
 */
void surface_services_changed(surface_t *);
#endif
