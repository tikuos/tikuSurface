/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * Exercise native desktop requests with a scheduler that wakes a worker
 * before its device completes. Each simulated driver runs on an owner turn.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../desktop/runtime.c"
#include "tiku_host.h"

static int dispatches, dispatching, waits;
static void (*advance)(void);
static surface_media_io_t *media_io;
static unsigned media_attempts, media_polls;
static int refuse_frame;
static unsigned char settings_record[SURFACE_SETTINGS_BYTES];

int surface_os_desktop_call(int (*fn)(void *), void *arg)
{
    assert(!dispatching);
    dispatches++;
    dispatching = 1;
    int (*volatile callback)(void *) = fn;
    int result = callback(arg);
    dispatching = 0;
    return result;
}

void surface_os_desktop_wait(int (*ready)(void *), void *arg, uint64_t timeout)
{
    (void)timeout;
    assert(!dispatching && advance && !ready(arg));
    if (++waits > 1) advance();
    assert(waits < 10);
}

void surface_os_desktop_kick(void) {}
void surface_os_desktop_wake(void) {}
int surface_os_in_owner(void) { return 0; }
int surface_os_in_desktop(void) { return 1; }
uint64_t surface_os_time(void) { return 8; }

tiku_present_result_t surface_presentation_submit(void *ctx, const tiku_present_view_t *view)
{
    assert(!dispatching && ctx == &rt.presentation);
    assert(view->damage.x == 0 && view->damage.w == 4);
    return refuse_frame ? TIKU_PRESENT_BUSY : TIKU_PRESENT_OK;
}

/* Presentation is simulated above. A direct register operation would bypass it. */
int surface_os_display_submit(unsigned back, unsigned x, unsigned y, unsigned w, unsigned h,
                              uint64_t token)
{
    (void)back; (void)x; (void)y; (void)w; (void)h; (void)token;
    abort();
}

int surface_os_display_amend(unsigned back, unsigned x, unsigned y, unsigned w, unsigned h)
{
    (void)back; (void)x; (void)y; (void)w; (void)h;
    abort();
}

int surface_os_display_convert(unsigned back, const void *pixels, unsigned stride,
                               unsigned x, unsigned y, unsigned w, unsigned h)
{
    (void)back; (void)pixels; (void)stride; (void)x; (void)y; (void)w; (void)h;
    abort();
}

int surface_os_display_convert_poll(void) { abort(); }
int surface_os_display_poll(uint64_t token) { (void)token; abort(); }

static _Alignas(64) uint32_t paint_pixels[256 * 97], paint_source[257 * 97];
static unsigned paint_polls;
static int paint_fails;

int surface_os_display_paint(uint32_t *pixels, unsigned stride, const uint32_t *source,
                             unsigned src_stride, unsigned width, unsigned height, uint32_t color)
{
    assert(dispatching && pixels == paint_pixels + 16 && stride == 256);
    assert(width == 224 && height == 95);
    assert(!source || (source == paint_source + 13 && src_stride == 257));
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            pixels[y*stride+x] = source ? source[y*src_stride+x] : color;
    paint_polls = 0;
    return 1;
}

int surface_os_display_paint_poll(void)
{
    assert(!dispatching);
    if (!paint_polls++) return SURFACE_SCANOUT_BUSY;
    return paint_fails ? SURFACE_SCANOUT_ERROR : SURFACE_SCANOUT_OK;
}

static void advance_paint(void) { (void)poll_paint(); }

static void raster_edges(void)
{
    for (unsigned i = 0; i < 257*97; ++i) paint_source[i] = 0x45908000u + i;
    for (int copy = 0; copy < 2; ++copy) {
        memset(paint_pixels, 0, sizeof paint_pixels);
        waits = 0;
        advance = advance_paint;
        int ok = copy ? tiku_gfx_native_blit(paint_pixels+3, 256, paint_source, 257, 246, 95)
                      : tiku_gfx_native_fill(paint_pixels+3, 256, 246, 95, 0x1234abcd);
        assert(ok && paint_polls == 2 && !active_paint);
        for (unsigned y = 0; y < 97; ++y) {
            for (unsigned x = 0; x < 256; ++x) {
                uint32_t expected = 0;
                if (y < 95 && x >= 3 && x < 249)
                    expected = copy ? paint_source[y*257+x-3] : 0x1234abcdu;
                assert(paint_pixels[y*256+x] == expected);
            }
        }
    }
    paint_fails = 1;
    waits = 0;
    assert(!tiku_gfx_native_fill(paint_pixels+3, 256, 246, 95, 0));
    assert(!active_paint && paint_polls == 2);
    int calls = dispatches;
    assert(!tiku_gfx_native_fill(paint_pixels, 256, 3, 4, 0));
    assert(!tiku_gfx_native_fill(paint_pixels, 255, 246, 95, 0));
    assert(dispatches == calls);
}

static void advance_frame(void)
{
    take_frame();
    if (!refuse_frame) {
        assert(!tiku_framebuffer_complete(&rt.owner, rt.presenting + 1, TIKU_PRESENT_OK));
        assert(tiku_framebuffer_complete(&rt.owner, rt.presenting, TIKU_PRESENT_OK));
    }
}

int surface_os_media_submit(void *bridge, surface_media_io_t *io)
{
    (void)bridge;
    assert(!dispatching);
    if (++media_attempts == 1) return SURFACE_MEDIA_BUSY;
    media_io = io;
    return SURFACE_MEDIA_OK;
}

int surface_os_media_poll(void *bridge)
{
    (void)bridge;
    assert(!dispatching && media_io);
    if (++media_polls == 1) return 0;
    media_io->result = SURFACE_MEDIA_OK;
    return 1;
}

int surface_os_media_cancel(void *bridge) { (void)bridge; return 1; }
static void advance_media(void) { (void)card_step(); }
static void advance_map(void) { assert(map_step()); }

int surface_os_settings_write(const unsigned char *data, size_t n)
{
    assert(!dispatching && n == sizeof settings_record);
    memcpy(settings_record, data, n);
    return 0;
}

int surface_os_settings_read(unsigned char *out, size_t n, size_t *used)
{
    assert(!dispatching && n >= sizeof settings_record);
    memcpy(out, settings_record, sizeof settings_record);
    *used = sizeof settings_record;
    return 0;
}

int surface_os_camera_open(void) { assert(!dispatching); return SURFACE_CAMERA_OK; }
void surface_os_camera_close(void) { assert(!dispatching); }
int surface_os_camera_stream(void *frame) { (void)frame; assert(!dispatching); return 0; }
int surface_os_camera_capture(void *frame) { assert(frame && !dispatching); return SURFACE_CAMERA_BUSY; }
int surface_os_camera_capture_poll(void) { assert(!dispatching); return SURFACE_CAMERA_OK; }

int surface_os_camera_encode(void *frame, unsigned quality, uint8_t *out, uint32_t cap)
{
    assert(!dispatching && frame && quality == 80 && out && cap == 8);
    return SURFACE_CAMERA_BUSY;
}

int surface_os_camera_encode_poll(uint32_t *len)
{
    assert(!dispatching);
    *len = 6;
    return SURFACE_CAMERA_OK;
}

static int mouse_source;
void surface_desktop_pointer_source(int mouse) { mouse_source = mouse; }

static const int mouse_samples[][4] = {
    {0,0,0,1}, {80,-40,0,1}, {-16,7,0,1}, {8,-4,2,1}, {0,0,0,0}
};
static unsigned samples_ready;
int surface_os_mouse_read(uint32_t *sequence, int *dx, int *dy, unsigned *buttons, int *connected)
{
    if (*sequence == samples_ready) return 0;
    const int *sample = mouse_samples[(*sequence)++];
    *dx = sample[0]; *dy = sample[1]; *buttons = (unsigned)sample[2]; *connected = sample[3];
    return 1;
}

surface_mouse_settings_t surface_mouse_settings(void)
{
    return (surface_mouse_settings_t){SURFACE_MOUSE_NORMAL, SURFACE_CURSOR_NORMAL};
}

void surface_presentation_cursor(surface_presentation_t *p, int shape, int size, int x, int y)
{
    p->mark_x = x; p->mark_y = y; p->cursor_size = size;
    p->mark_on = shape < 0 ? 0 : 2;
}

static unsigned hashed;
void surface_desktop_hash_file(const char *path)
{
    assert(!dispatching && !strcmp(path, "/Card/example"));
    assert(!surface_runtime_hash("/busy", 5));
    hashed++;
}

int main(void)
{
    unsigned char pixels[8] = {0};
    uint32_t len = 0;
    rt.running = 1;
    assert(surface_desktop_camera(SURFACE_CAM_OPEN, pixels, NULL, 0, 0) == 0);
    assert(dispatches == 1);
    assert(surface_desktop_camera(SURFACE_CAM_CLOSE, NULL, NULL, 0, 0) == -1);
    assert(surface_desktop_camera_result(NULL) == SURFACE_CAMERA_BUSY && camera_step());
    assert(surface_desktop_camera_result(NULL) == SURFACE_CAMERA_OK);
    assert(surface_desktop_camera(SURFACE_CAM_ENCODE, pixels, pixels, 8, 80) == 0);
    assert(!camera_step() && surface_desktop_camera_result(NULL) == SURFACE_CAMERA_BUSY);
    assert(camera_step() && surface_desktop_camera_result(&len) == 0 && len == 6);

    tiku_host_t *host = tiku_host_open(800, 480, "desktop");
    tiku_surface_t surface = {.w = 800, .h = 480};
    tiku_rect_t damage = {-2, 0, 6, 4};
    advance = advance_frame;
    assert(tiku_host_present_in(host, &surface, &damage) == 0 && waits == 2);
    waits = 0;
    refuse_frame = 1;
    assert(tiku_host_present_in(host, &surface, &damage) == -1 && waits == 2);

    tiku_input_event_t event;
    post(TIKU_EVENT_POINTER_DOWN, 1, 2, 1);
    post(TIKU_EVENT_POINTER_MOVE, 2, 3, 2);
    post(TIKU_EVENT_POINTER_MOVE, 3, 4, 3);
    post(TIKU_EVENT_POINTER_UP, 3, 4, 4);
    int before = dispatches;
    assert(tiku_host_poll(host, &event) && event.type == TIKU_EVENT_POINTER_DOWN);
    assert(tiku_host_poll(host, &event) && event.type == TIKU_EVENT_POINTER_MOVE && event.x == 3);
    assert(tiku_host_poll(host, &event) && event.type == TIKU_EVENT_POINTER_UP);
    assert(!tiku_host_poll(host, &event) && dispatches == before + 3);

    assert(!mouse_source);
    rt.pointer = (surface_mouse_position_t){.x=400,.y=240};
    samples_ready = 2;
    assert(mouse_input(8) && rt.presentation.mark_on == 2);
    assert(tiku_host_poll(host, &event) && mouse_source && event.type == TIKU_EVENT_POINTER_MOVE);
    assert(event.x == 480 && event.y == 200);
    assert(rt.presentation.mark_x == event.x && rt.presentation.mark_y == event.y);
    assert(rt.presentation.cursor_size == 16 && !tiku_host_poll(host, &event));
    samples_ready = 3;
    assert(mouse_input(9) && tiku_host_poll(host, &event));
    assert(event.type == TIKU_EVENT_POINTER_MOVE && event.x == 464 && event.y == 207);
    assert(!tiku_host_poll(host, &event));
    samples_ready = 4;
    assert(mouse_input(10) && tiku_host_poll(host, &event));
    assert(event.type == TIKU_EVENT_POINTER_MOVE && event.x == 472 && event.y == 203);
    assert(tiku_host_poll(host, &event) && event.type == TIKU_EVENT_POINTER_DOWN && event.button == 3);
    samples_ready = 5;
    assert(mouse_input(11) && rt.presentation.mark_on == 0);
    assert(tiku_host_poll(host, &event) && event.type == TIKU_EVENT_POINTER_UP && event.button == 3);

    surface_media_io_t io = {0};
    surface_desktop_card(&io);
    assert(io.result == SURFACE_MEDIA_UNAVAILABLE);
    surface_desktop_bridge.card_mounted = 1;
    waits = 0;
    advance = advance_media;
    surface_desktop_card(&io);
    assert(io.result == SURFACE_MEDIA_OK && media_attempts == 2 && media_polls == 2);

    rt.map = (surface_touch_map_t){0, 799, 0, 479, 0, 0, 0};
    advance = advance_map;
    waits = 0;
    assert(tiku_shell_touch_fit(10, 789, 10, 469) == 0);
    int min = rt.map.x_min;
    waits = 0;
    assert(tiku_shell_touch_keep() == 0);
    rt.map.x_min = 100;
    waits = 0;
    tiku_shell_touch_revert();
    assert(rt.map.x_min == min);

    assert(surface_runtime_hash("/Card/example", 13));
    surface_desktop_hash_poll();
    surface_desktop_hash_poll();
    assert(hashed == 1 && surface_runtime_hash("/Card/example", 13));
    surface_desktop_hash_poll();
    assert(hashed == 2);
    raster_edges();
    puts("Native device requests passed");
    return 0;
}
