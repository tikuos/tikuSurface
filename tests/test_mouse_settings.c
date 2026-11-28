/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_mouse_settings.c - desktop pointer controls and their saved settings.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "mouse.h"
#include "touch_preferences.h"
#include "tiku_window.h"
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static char saved[16], stage[16];
static size_t saved_size, staged_size;
static int writing, refuse_close, writes, calibrations;
static const char *expected_path = "/data/ts/mouse";

void surface_desktop_calibrate_touch(void) { ++calibrations; }
tiku_window_t *surface_mouse_preferences_open(tiku_workspace_t *, tiku_rect_t, int *, const char *);

int tiku_posix_open(const char *path, int flags)
{
    assert(strcmp(path, expected_path) == 0);
    writing = flags != O_RDONLY;
    staged_size = 0;
    return 3;
}

ssize_t tiku_posix_read(int fd, void *out, size_t size)
{
    assert(!writing && fd == 3 && size >= saved_size);
    memcpy(out, saved, saved_size);
    return (ssize_t)saved_size;
}

ssize_t tiku_posix_write(int fd, const void *data, size_t size)
{
    assert(writing && fd == 3 && size <= sizeof stage);
    ++writes;
    memcpy(stage, data, size);
    staged_size = size;
    return (ssize_t)size;
}

int tiku_posix_close(int fd)
{
    assert(fd == 3);
    if (writing) {
        if (refuse_close) return -1;
        memcpy(saved, stage, staged_size);
        saved_size = staged_size;
    }
    return 0;
}

static void pointer(tiku_window_t *w, tiku_event_type_t type, int x, int y)
{
    tiku_event_t ev = {.type = type, .button = 1, .x = x, .y = y};
    assert(tiku_window_send(w, &ev));
}

static void check_window(void)
{
    void *storage = calloc(1, tiku_workspace_size());
    assert(storage);
    tiku_workspace_t *ws = tiku_workspace_init(storage, tiku_workspace_size(), NULL, NULL);
    int dirty = 0;
    tiku_window_t *w = surface_mouse_preferences_open(ws, (tiku_rect_t){0,0,800,480}, &dirty, NULL);
    assert(w && dirty);
    tiku_rect_t r = tiku_window_content(w);
    int old_writes = writes;
    pointer(w, TIKU_EVENT_POINTER_DOWN, r.x+18, r.y+45);
    pointer(w, TIKU_EVENT_POINTER_MOVE, r.x+r.w, r.y+45);
    assert(writes == old_writes);
    pointer(w, TIKU_EVENT_POINTER_UP, r.x+r.w+10, r.y+45);
    assert(surface_mouse_settings().speed == 400 && writes == old_writes+1);
    tiku_event_t left = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_LEFT};
    assert(tiku_window_send(w, &left) && surface_mouse_settings().speed == 375);
    for (int i = 0; i < SURFACE_CURSOR_SIZES; i++) {
        int x = r.x+16+(2*i+1)*(r.w-32)/(2*SURFACE_CURSOR_SIZES);
        pointer(w, TIKU_EVENT_POINTER_DOWN, x, r.y+120);
        pointer(w, TIKU_EVENT_POINTER_UP, x, r.y+120);
        assert(surface_mouse_settings().size == surface_cursor_size(i));
    }
    tiku_workspace_deinit(ws);
    free(storage);
}

static void check_touch_window(void)
{
    expected_path = "/data/ts/touch";
    saved_size = 0;
    surface_touch_preferences_load();
    assert(surface_touch_magnifier());
    void *storage = calloc(1, tiku_workspace_size());
    assert(storage);
    tiku_workspace_t *ws = tiku_workspace_init(storage, tiku_workspace_size(), NULL, NULL);
    tiku_rect_t area = {0, 0, 800, 480};
    int dirty = 0;
    tiku_window_t *w = surface_touch_preferences_open(ws, area, &dirty, NULL);
    assert(w && dirty && surface_touch_preferences_open(ws, area, &dirty, NULL) == w);
    tiku_rect_t r = tiku_window_content(w);
    int count = writes;
    pointer(w, TIKU_EVENT_POINTER_DOWN, r.x+30, r.y+26);
    assert(surface_touch_magnifier() && writes == count);
    pointer(w, TIKU_EVENT_POINTER_UP, r.x+30, r.y+26);
    assert(!surface_touch_magnifier() && writes == count+1);
    assert(saved_size == 1 && saved[0] == '0');
    refuse_close = 1;
    pointer(w, TIKU_EVENT_POINTER_DOWN, r.x+30, r.y+26);
    pointer(w, TIKU_EVENT_POINTER_UP, r.x+30, r.y+26);
    assert(!surface_touch_magnifier() && saved[0] == '0');
    refuse_close = 0;
    saved[0] = '1';
    surface_touch_preferences_load();
    assert(surface_touch_magnifier());
    saved[0] = '0';
    surface_touch_preferences_load();
    assert(!surface_touch_magnifier());
    saved[1] = 'x';
    saved_size = 2;
    surface_touch_preferences_load();
    assert(surface_touch_magnifier());
    count = writes;
    pointer(w, TIKU_EVENT_POINTER_DOWN, r.x+40, r.y+80);
    assert(calibrations == 0);
    pointer(w, TIKU_EVENT_POINTER_UP, r.x+40, r.y+80);
    assert(calibrations == 1 && writes == count);
    tiku_workspace_deinit(ws);
    free(storage);
}

int main(void)
{
    surface_mouse_settings_t prefs = {325, 32};
    assert(surface_mouse_settings().speed == 100 && surface_mouse_settings().size == 16);
    assert(surface_mouse_save(prefs) && saved_size == 10 && !memcmp(saved, "v2 325 32\n", 10));
    refuse_close = 1;
    assert(!surface_mouse_save((surface_mouse_settings_t){200, 16}));
    assert(surface_mouse_settings().speed == 325 && surface_mouse_settings().size == 32);
    assert(!surface_mouse_save((surface_mouse_settings_t){425, 16}));
    assert(!surface_mouse_save((surface_mouse_settings_t){125, 18}));
    memcpy(saved, "1 1\n", 4);
    saved_size = 4;
    surface_mouse_load();
    assert(surface_mouse_settings().speed == 75 && surface_mouse_settings().size == 16);
    memcpy(saved, "v2 050 24\n", 10);
    saved_size = 10;
    surface_mouse_load();
    assert(surface_mouse_settings().speed == 50 && surface_mouse_settings().size == 24);
    const char *invalid[] = {"9 1\n", "2 1\nx", "v2 650 16\n", "v2 125 14\n", "v2 100 32\nx"};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        saved_size = strlen(invalid[i]);
        memcpy(saved, invalid[i], saved_size);
        surface_mouse_load();
        assert(surface_mouse_settings().speed == 50 && surface_mouse_settings().size == 24);
    }
    refuse_close = 0;
    check_window();
    check_touch_window();
    return 0;
}
