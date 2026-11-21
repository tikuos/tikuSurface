/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * runtime_image.c - image admission through the firmware allocator.
 * SPDX-License-Identifier: Apache-2.0
 */
#if SURFACE_EDITOR_DOCUMENTS
#include "runtime_documents.c"
#else
#include "../platform/tikuos/runtime.c"
#include "surface_internal.h"
#include <assert.h>
#endif
/** @brief Open the fixture's maximum admitted encoded image in the active viewer. */
void surface_test_open_image(void)
{
    assert(runtime&&runtime->shell->active.state->id==SURFACE_IMAGE_APP);
    assert(surface_image_open(runtime->shell->active.state,"/a",0,0));
}
/** @brief Report completion and validate the maximum decoded source dimensions. */
int surface_test_image_ready(void)
{
    surface_view_t v;surface_view(runtime->shell,&v);
    if(v.image_busy)return 0;
    assert(v.image_frame&&!v.image_error&&v.image_width==512&&v.image_height==256);
    return 1;
}
/** @brief Retain the current Reader page while another page is constructed. */
const void *surface_test_hold_reader(void)
{
    surface_view_t v;surface_view(runtime->shell,&v);assert(v.reader_page);
    surface_reader_hold(v.reader_page);return v.reader_page;
}
/** @brief Verify replacement and release the retained previous Reader page. */
void surface_test_drop_reader(const void *page)
{
    surface_view_t v;surface_view(runtime->shell,&v);assert(v.reader_page&&v.reader_page!=page);
    surface_reader_drop(page);
}
