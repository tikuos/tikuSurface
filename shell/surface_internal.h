/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * surface_internal.h - product state shared by ownership, apps and painting.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_INTERNAL_H
#define SURFACE_INTERNAL_H
#include "surface.h"
#include "tiku_static_app.h"
#include "tiku_press.h"

/** @brief Product action IDs shared by layout and input routing. */
enum { ACTION_HOME = 1, ACTION_BACK, ACTION_CALCULATOR = 10,
    ACTION_BROWSER, ACTION_SETTINGS, ACTION_HEX = 100, ACTION_ERASE = 116,
    ACTION_CLEAR, ACTION_ROW = 200, ACTION_NEXT = 210, ACTION_REFRESH, ACTION_PREVIOUS, ACTION_SWAP = 300, ACTION_INVERT_X, ACTION_INVERT_Y, ACTION_SAVE };
/** @brief Owned app state; a request lease retains it through cancellation. */
typedef struct {
    struct surface *shell;
    tiku_static_app_t *owner;
    surface_app_t id;
    int stopped, queued, reading;
    tiku_paged_storage_t *store;
    tiku_app_lease_t lease;
    tiku_resource_error_t status;
    surface_touch_map_t calibration;
    unsigned settings_status;
    char hex[17], path[TIKU_RESOURCE_PATH_MAX], target[TIKU_RESOURCE_PATH_MAX];
    char preview[1025];
    uint64_t epoch, after, node;
    size_t count, offset, restore_offset;
    struct { uint64_t after; size_t offset; } history[8];
    unsigned history_count;
    int eof, preview_hex;
    size_t preview_bytes;
} surface_app_state_t;
/** @brief At most one foreground and one retained stopped application. */
typedef struct {
    tiku_static_app_t *owner;
    surface_app_state_t *state;
} surface_instance_t;
/** @brief Serialized shell storage, providers and pending navigation. */
struct surface {
    tiku_owned_buffer_t self;
    surface_config_t config;
    tiku_event_queue_t *events;
    tiku_framebuffer_t *frame;
    tiku_surface_t *painting;
    surface_view_t painted;
    unsigned paint_y;
    surface_instance_t active, retired;
    surface_app_t pending;
    int switching, stopped, dirty, clock_failed, display_failed;
    tiku_resource_error_t status;
    uint64_t now;
    tiku_press_t press;
};
/** @brief Start a static product app; status is reported in the shell. */
void surface_app_start(surface_t *, surface_app_t);
/** @brief Retire provider work and advance one queued browser operation. */
void surface_app_poll(surface_app_state_t *);
/** @brief Deliver a product action to a live app. */
void surface_app_action(surface_app_state_t *, int);
/** @brief Request the parent browser view, returning zero at its root. */
int surface_app_back(surface_app_state_t *);
/** @brief Edit a hexadecimal 64-bit word with digit, erase or clear input. */
void surface_calculator_key(surface_app_state_t *, unsigned);
/** @brief Find the enabled control under one logical pointer position. */
int surface_hit(const surface_view_t *, int, int);
/** @brief Paint one copied shell view into the acquired surface. */
void surface_paint(tiku_surface_t *, const surface_view_t *);
#endif
