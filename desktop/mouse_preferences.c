/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * mouse_preferences.c - durable pointer tracking choices in a desktop dialog.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "mouse.h"
#include "tiku_dialog.h"
#include "tiku_posix.h"
#include "tiku_slider_paint.h"
#include "tiku_text_layout_paint.h"
#include "tiku_trk_builtin.h"
#include "tiku_ui.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#define KEY "/data/ts/mouse"
enum { SPEED, FIRST_SIZE };
static unsigned active = SURFACE_MOUSE_NORMAL | (SURFACE_CURSOR_NORMAL << 16);
static tiku_window_t *window;
static int *redraw;
static tiku_dialog_t dialog;
static tiku_slider_t slider;
static int save_failed;

surface_mouse_settings_t surface_mouse_settings(void)
{
    unsigned prefs = __atomic_load_n(&active, __ATOMIC_ACQUIRE);
    return (surface_mouse_settings_t){prefs & 0xffffu, prefs >> 16};
}

static void apply(surface_mouse_settings_t prefs)
{
    __atomic_store_n(&active, prefs.speed | ((unsigned)prefs.size << 16), __ATOMIC_RELEASE);
}

static int valid(surface_mouse_settings_t prefs)
{
    if (prefs.speed < SURFACE_MOUSE_MIN || prefs.speed > SURFACE_MOUSE_MAX ||
        prefs.speed % SURFACE_MOUSE_STEP) return 0;
    for (unsigned i = 0; i < SURFACE_CURSOR_SIZES; i++)
        if (prefs.size == surface_cursor_size(i)) return 1;
    return 0;
}

static int decimal(const char *s, int n)
{
    int value = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        value = value * 10 + (s[i] - '0');
    }
    return value;
}

void surface_mouse_load(void)
{
    char record[11];
    int fd = tiku_posix_open(KEY, O_RDONLY);
    if (fd < 0) return;
    ssize_t n = tiku_posix_read(fd, record, sizeof record);
    tiku_posix_close(fd);
    if (n == 4 && record[0] >= '0' && record[0] <= '4' && record[1] == ' ' &&
        (record[2] == '0' || record[2] == '1') && record[3] == '\n') {
        static const uint8_t speeds[] = {50, 75, 100, 150, 200};
        apply((surface_mouse_settings_t){speeds[record[0] - '0'], SURFACE_CURSOR_NORMAL});
    } else if (n == 10 && !memcmp(record, "v2 ", 3) && record[6] == ' ' && record[9] == '\n') {
        surface_mouse_settings_t prefs = {decimal(record + 3, 3), decimal(record + 7, 2)};
        if (valid(prefs)) apply(prefs);
    }
}

int surface_mouse_save(surface_mouse_settings_t prefs)
{
    char record[11];
    if (!valid(prefs)) return 0;
    int length = snprintf(record, sizeof record, "v2 %03u %02u\n",
                          (unsigned)prefs.speed, (unsigned)prefs.size);
    if (length != 10) return 0;
    int fd = tiku_posix_open(KEY, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return 0;
    ssize_t n = tiku_posix_write(fd, record, (size_t)length);
    int rc = tiku_posix_close(fd);
    if (n != length || rc < 0) return 0;
    apply(prefs);
    return 1;
}

static void layout(tiku_rect_t r)
{
    int x = r.x + 16, width = r.w - 32;
    tiku_dialog_begin(&dialog, -1, -1);
    tiku_dialog_add(&dialog, SPEED, (tiku_rect_t){x, r.y+34, width, 24}, TIKU_DIALOG_BUTTON, 1);
    for (int i = 0; i < SURFACE_CURSOR_SIZES; i++) {
        int left = x + i * width / SURFACE_CURSOR_SIZES;
        int right = x + (i + 1) * width / SURFACE_CURSOR_SIZES;
        tiku_dialog_add(&dialog, FIRST_SIZE+i, (tiku_rect_t){left, r.y+100, right-left-4, 46},
                        TIKU_DIALOG_BUTTON, 1);
    }
    tiku_dialog_end(&dialog);
}

static void paint(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r, void *ctx)
{
    (void)w; (void)ctx;
    char label[32];
    surface_mouse_settings_t prefs = surface_mouse_settings();
    layout(r);
    tiku_ui_panel(s, r);
    tiku_rect_t track = dialog.control[0].rect;
    snprintf(label, sizeof label, "Tracking speed   %d%%", slider.value * SURFACE_MOUSE_STEP);
    tiku_text_line_draw(s, (tiku_rect_t){track.x,r.y+4,track.w,26}, 0, label, TIKU_C_TEXT);
    tiku_slider_draw(&slider, s, track);
    if (tiku_dialog_focused(&dialog, SPEED)) tiku_frame(s, track, TIKU_C_FOCUS);
    snprintf(label, sizeof label, "Cursor size   %u px", (unsigned)prefs.size);
    tiku_text_line_draw(s, (tiku_rect_t){track.x,r.y+68,track.w,26}, 0, label, TIKU_C_TEXT);
    for (int i = 0; i < SURFACE_CURSOR_SIZES; i++) {
        int id = FIRST_SIZE+i, size = surface_cursor_size(i);
        tiku_rect_t cell = dialog.control[i+1].rect;
        unsigned flags = prefs.size == size ? TIKU_S_DEFAULT : 0;
        if (tiku_dialog_pressed(&dialog, id)) flags |= TIKU_S_PRESSED;
        if (tiku_dialog_focused(&dialog, id)) flags |= TIKU_S_FOCUS;
        tiku_ui_button(s, cell, "", flags);
        int left = cell.x + (cell.w-size)/2, top = cell.y + (cell.h-size)/2;
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                int ink = surface_cursor_pixel(0, size, x, y);
                if (ink >= 0) tiku_pixel(s, left+x, top+y, ink ? 0xffffffu : 0u);
            }
        }
    }
    if (save_failed)
        tiku_text_line_draw(s, (tiku_rect_t){track.x,r.y+151,track.w,24}, 0,
                            "Could not save mouse settings.", TIKU_C_TEXT);
}

static int event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    (void)ctx;
    layout(tiku_window_content(w));
    tiku_rect_t track = dialog.control[0].rect;
    tiku_dialog_result_t result = tiku_dialog_event(&dialog, ev);
    surface_mouse_settings_t previous = surface_mouse_settings(), prefs = previous;
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 && dialog.capture == SPEED) {
        tiku_slider_press(&slider, track, ev->x);
    } else if (slider.tracking && ev->type == TIKU_EVENT_POINTER_MOVE) {
        tiku_slider_drag(&slider, track, ev->x);
    } else if (slider.tracking && ev->type == TIKU_EVENT_POINTER_UP && ev->button == 1) {
        prefs.speed = tiku_slider_drag(&slider, track, ev->x) * SURFACE_MOUSE_STEP;
        tiku_slider_release(&slider);
    } else if (ev->type == TIKU_EVENT_DEACTIVATED) {
        tiku_slider_release(&slider);
        tiku_slider_set(&slider, prefs.speed / SURFACE_MOUSE_STEP);
    } else if (ev->type == TIKU_EVENT_KEY_DOWN && dialog.focus == SPEED &&
               (ev->key == TIKU_KEY_LEFT || ev->key == TIKU_KEY_RIGHT)) {
        prefs.speed = tiku_slider_step(&slider, ev->key == TIKU_KEY_LEFT ? -1 : 1) * SURFACE_MOUSE_STEP;
        result.taken = 1;
    } else if (result.activated >= FIRST_SIZE) {
        prefs.size = surface_cursor_size(result.activated - FIRST_SIZE);
    }
    if (prefs.speed != previous.speed || prefs.size != previous.size) {
        save_failed = !surface_mouse_save(prefs);
        if (save_failed) tiku_slider_set(&slider, previous.speed / SURFACE_MOUSE_STEP);
    }
    if (result.taken && redraw) *redraw = 1;
    return result.taken;
}

static void closed(tiku_window_t *w, void *ctx)
{
    (void)w; (void)ctx;
    window = NULL;
}

tiku_window_t *surface_mouse_preferences_open(tiku_workspace_t *workspace, tiku_rect_t area,
                                              int *dirty, const char *path)
{
    (void)path;
    redraw = dirty;
    if (!window) {
        tiku_rect_t frame = {area.x+(area.w-352)/2, area.y+(area.h-216)/2, 352, 216};
        window = tiku_workspace_open(workspace, "Mouse Preferences", frame, paint, NULL, NULL);
        if (!window) return NULL;
        tiku_dialog_init(&dialog);
        tiku_slider_init(&slider, SURFACE_MOUSE_MIN / SURFACE_MOUSE_STEP,
                         SURFACE_MOUSE_MAX / SURFACE_MOUSE_STEP,
                         surface_mouse_settings().speed / SURFACE_MOUSE_STEP, 1);
        tiku_window_set_handlers(window, event, NULL, closed);
        tiku_window_set_size_controls(window, 0, 0);
        save_failed = 0;
    }
    tiku_workspace_activate(workspace,window);
    if (dirty) *dirty = 1;
    return window;
}
