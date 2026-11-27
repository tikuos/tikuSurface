/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * builtins.c - the applications linked into the desktop, whose windows open
 * in its own workspace: Camera, for the board's camera and the pictures on
 * the card, which it shows in TikuImage's viewer; Sound Recorder, for the
 * board's microphones.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "camera.h"
#include "recorder.h"
#include "tiku_posix.h"
#include <fcntl.h>
#include "tiku_app_product.h"
#include "terminal.h"
#include "tiku_image_window.h"
#include "tiku_trk_builtin.h"
/* TikuImage's viewer, made at its first open and kept while its window
 * closes and opens again. */
static tiku_image_window_t *viewer;
/* Where Camera last opened, for the viewer it opens on a saved photo. */
static tiku_workspace_t *last_workspace;
static tiku_rect_t last_area;
static int *last_dirty;
/** @brief Open @p path, or the viewer alone, its window filling @p area
 *         whenever it opens anew. */
static tiku_window_t *image_open(tiku_workspace_t *workspace, tiku_rect_t area,
                                 int *dirty, const char *path)
{
    int fresh;
    if (viewer == NULL) {
        viewer = tiku_image_window_new(workspace, dirty, NULL, NULL);
        if (viewer == NULL) return NULL;
        /* The heap holds a picture the size of the view, not a camera's
         * whole frame beside it. */
        tiku_image_window_bound(viewer, 1);
    }
    fresh = tiku_image_window_handle(viewer) == NULL;
    tiku_image_window_open(viewer);
    if (fresh && tiku_image_window_handle(viewer) != NULL)
        (void)tiku_image_window_zoom(viewer, area);
    if (path != NULL) (void)tiku_image_window_file(viewer, path);
    return tiku_image_window_handle(viewer);
}
/** @brief Show the photo Camera saved at @p path in the viewer. */
static void review(const char *path)
{
    if (last_workspace != NULL) (void)image_open(last_workspace, last_area, last_dirty, path);
}
/** @brief Open picture @p path in the viewer, or Camera when there is none. */
static tiku_window_t *camera_entry(tiku_workspace_t *workspace, tiku_rect_t area,
                                   int *dirty, const char *path)
{
    last_workspace = workspace;
    last_area = area;
    last_dirty = dirty;
    if (path != NULL) return image_open(workspace, area, dirty, path);
    return surface_camera_open(workspace, area, dirty, tiku_trk_builtin_damage, review);
}
/** @brief Each desktop turn: the viewer's Watch refresh and the camera's
 *         requests. */
static void camera_tick(int64_t now)
{
    tiku_image_window_tick(viewer, now);
    surface_camera_tick(now);
}
/** @brief Open Sound Recorder; a recording opened from Tracker opens it too. */
static tiku_window_t *recorder_entry(tiku_workspace_t *workspace, tiku_rect_t area,
                                     int *dirty, const char *path)
{
    (void)path;
    return surface_recorder_open(workspace, area, dirty, tiku_trk_builtin_damage);
}

static int basic_start(void)
{
    int fd = tiku_posix_open("/surface/control", O_WRONLY);
    if (fd < 0) return 0;
    int ok = tiku_posix_write(fd, "basic-window", 12) == 12;
    tiku_posix_close(fd);
    return ok;
}

const tiku_trk_builtin_t *tiku_trk_builtins(int *count)
{
    static const tiku_trk_builtin_t list[] = {
        {"camera", "Camera", "image/png,image/jpeg,image/bmp,image/x-ms-bmp",
         "camera", "graphics", camera_entry, camera_tick, NULL},
        {"recorder", "Sound Recorder", "audio/wav,audio/x-wav", "sound_recorder", "audio",
         recorder_entry, surface_recorder_tick, NULL},
        {.id = "terminal", .name = TIKU_PRODUCT_TERMINAL_NAME, .icon = "terminal",
         .category = "utilities", .start = surface_terminal_start},
        {.id = "basic-counter", .name = "BASIC Counter", .icon = "calculator",
         .category = "utilities", .start = basic_start},
    };
    *count = (int)(sizeof list / sizeof list[0]);
    return list;
}
