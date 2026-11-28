/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_p4x_panel.c - landscape panel bounds, pixel layout and saved calibration.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../platform/tikuos/runtime.h"
#include "host.h"
#include "presentation.h"
#include "display.h"
#include "settings.h"
#include "touch_map.h"
#include "../desktop/mouse.h"
#include <assert.h>
#include <string.h>

enum { W = 1024, H = 600 };
_Static_assert(SURFACE_SCANOUT_BYTES == W * H * sizeof(uint16_t), "P4X scanout allocation");
_Static_assert(SURFACE_FRAMES_BYTES == 2 * SURFACE_SCANOUT_BYTES, "native desktop frame reservation");
static _Alignas(64) uint16_t scan[2][W * H];
static uint32_t pixels[H][W + 2];
static uint64_t submitted;

static tiku_present_result_t submit(void *ctx, unsigned buffer, tiku_rect_t r, uint64_t token)
{
    (void)ctx;
    assert(buffer == 1 && r.x == 0 && r.y == 0 && r.w == W && r.h == H);
    submitted = token;
    return TIKU_PRESENT_OK;
}

static tiku_present_result_t poll(void *ctx, uint64_t token)
{
    (void)ctx;
    assert(submitted && submitted == token);
    return TIKU_PRESENT_OK;
}

static tiku_present_result_t submit_padded_view(void *ctx, const tiku_present_view_t *source)
{
    tiku_present_view_t view = *source;
    view.stride = sizeof pixels[0];
    view.bytes = sizeof pixels;
    return surface_presentation_submit(ctx, &view);
}

int main(void)
{
    static const surface_scanout_ops_t ops = {.submit=submit, .poll=poll};
    surface_host_t host;
    surface_config_t config = surface_host_config(&host);
    surface_presentation_t p;
    uint64_t sequence = 0;
    tiku_framebuffer_t *frame = tiku_framebuffer_borrow(config.allocator, &pixels[0][0],
        sizeof pixels, W, H);
    assert(frame && surface_presentation_init(&p, scan[0], sizeof scan[0], scan[1],
        sizeof scan[1], 0, 4096, &sequence, &ops, NULL));
    assert(tiku_framebuffer_begin(frame, NULL));
    for (unsigned y = 0; y < H; ++y) {
        pixels[y][W-1] = 0xff0000;
        pixels[y][W] = pixels[y][W+1] = 0xffffff;
    }
    pixels[H-1][0] = 0x00ff00;
    tiku_presenter_t presenter = {submit_padded_view, &p};
    assert(tiku_framebuffer_present(frame, presenter) == TIKU_PRESENT_OK);
    tiku_present_result_t result = TIKU_PRESENT_BUSY;
    for (unsigned i = 0; result == TIKU_PRESENT_BUSY && i < 180; ++i)
        result = surface_presentation_poll(&p);
    assert(result == TIKU_PRESENT_OK && p.front == 1);
    for (unsigned y = 0; y < H; ++y)
        for (unsigned x = 0; x < W; ++x)
            assert(scan[1][y*W+x] == (x == W-1 ? 0xf800 : y == H-1 && !x ? 0x07e0 : 0));
    assert(tiku_framebuffer_destroy(&frame));
    surface_mouse_position_t pos = {.x = 800, .y = 480};
    assert(surface_mouse_move(&pos, (surface_mouse_settings_t){.speed=100}, 400, 400));
    assert(pos.x == 1023 && pos.y == 599);
    surface_touch_map_t map = {0, 1023, 0, 599, 0, 0, 0}, decoded;
    surface_touch_map_t first_boot = surface_touch_map_default();
    int x, y;
    surface_touch_map_point(&first_boot, 59, 576, &x, &y);
    assert(x == 964 && y == 23);
    surface_touch_map_point(&first_boot, 336, 223, &x, &y);
    assert(x == 687 && y == 376);
    unsigned char record[SURFACE_SETTINGS_BYTES];
    assert(surface_settings_encode(&map, record));
    assert(record[6] == 0 && record[7] == 4 && record[8] == 88 && record[9] == 2);
    assert(surface_settings_decode(record, sizeof record, &decoded) == SURFACE_SETTINGS_SAVED);
    assert(decoded.x_max == 1023 && decoded.y_max == 599);
    assert(surface_settings_load(0, record, sizeof record, &first_boot) == SURFACE_SETTINGS_SAVED);
    surface_touch_map_point(&first_boot, 59, 576, &x, &y);
    assert(x == 59 && y == 576);
    return 0;
}
