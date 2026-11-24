/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * builtins.c - the applications linked into the desktop, whose windows open
 * in its own workspace: TikuImage, for the pictures on the card.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_app_product.h"
#include "tiku_image_window.h"
#include "tiku_trk_builtin.h"

/* TikuImage's viewer, made at its first open and kept while its window
 * closes and opens again. */
static tiku_image_window_t *viewer;

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

/** @brief Each desktop turn: the viewer's Watch refresh. */
static void image_tick(int64_t now)
{
    tiku_image_window_tick(viewer, now);
}

const tiku_trk_builtin_t *tiku_trk_builtins(int *count)
{
    static const tiku_trk_builtin_t list[] = {
        {TIKU_PRODUCT_IMAGE_ID, TIKU_PRODUCT_IMAGE_NAME,
         "image/png,image/jpeg,image/bmp,image/x-ms-bmp", "image_viewer", "graphics",
         image_open, image_tick},
    };

    *count = (int)(sizeof list / sizeof list[0]);
    return list;
}
