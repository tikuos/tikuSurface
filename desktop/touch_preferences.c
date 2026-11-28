/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * touch_preferences.c - touch magnification and calibration in Settings.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "touch_preferences.h"
#include "tiku_dialog.h"
#include "tiku_posix.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include <fcntl.h>

static const char magnifier_file[] = "/data/ts/touch";
static int magnifier = 1;
static int save_failed;
static tiku_window_t *window;
static tiku_dialog_t dialog;

void surface_touch_preferences_load(void)
{
    char value[2];
    int fd = tiku_posix_open(magnifier_file, O_RDONLY);
    magnifier = 1;
    if (fd < 0) return;
    ssize_t n = tiku_posix_read(fd, value, sizeof value);
    tiku_posix_close(fd);
    if (n == 1 && value[0] == '0') magnifier = 0;
}

int surface_touch_magnifier(void)
{
    return magnifier;
}

static int save_magnifier(int enabled)
{
    char value = enabled ? '1' : '0';
    int fd = tiku_posix_open(magnifier_file, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return 0;
    ssize_t n = tiku_posix_write(fd, &value, 1);
    int rc = tiku_posix_close(fd);
    if (n != 1 || rc < 0) return 0;
    magnifier = enabled;
    return 1;
}

enum { MAGNIFY, CALIBRATE };

static void layout(tiku_rect_t r)
{
    tiku_dialog_begin(&dialog, -1, -1);
    tiku_dialog_add(&dialog, MAGNIFY, (tiku_rect_t){r.x+16, r.y+6, r.w-32, 44},
                    TIKU_DIALOG_TOGGLE, 1);
    tiku_dialog_add(&dialog, CALIBRATE, (tiku_rect_t){r.x+16, r.y+58, r.w-32, 44},
                    TIKU_DIALOG_BUTTON, 1);
    tiku_dialog_end(&dialog);
}

static unsigned flags(int id)
{
    return (tiku_dialog_focused(&dialog, id) ? TIKU_S_FOCUS : 0) |
           (tiku_dialog_pressed(&dialog, id) ? TIKU_S_PRESSED : 0);
}

static void paint(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r, void *ctx)
{
    (void)w; (void)ctx;
    layout(r);
    tiku_ui_panel(s, r);
    tiku_ui_checkbox(s, dialog.control[MAGNIFY].rect, "Magnify small controls",
                     flags(MAGNIFY) | (magnifier ? TIKU_S_ON : 0));
    tiku_ui_button(s, dialog.control[CALIBRATE].rect, "Calibrate touchscreen…",
                   flags(CALIBRATE));
    if (save_failed)
        tiku_text_line_draw(s, (tiku_rect_t){r.x+16, r.y+106, r.w-32, 26}, 0,
                            "Could not save touch settings.", TIKU_C_TEXT);
}

static int event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    int *dirty = ctx;
    layout(tiku_window_content(w));
    tiku_dialog_result_t result = tiku_dialog_event(&dialog, ev);
    if (result.activated == MAGNIFY)
        save_failed = !save_magnifier(!magnifier);
    else if (result.activated == CALIBRATE)
        surface_desktop_calibrate_touch();
    if (result.taken && dirty) *dirty = 1;
    return result.taken;
}

static void closed(tiku_window_t *w, void *ctx)
{
    (void)w; (void)ctx;
    window = NULL;
}

tiku_window_t *surface_touch_preferences_open(tiku_workspace_t *workspace, tiku_rect_t area,
                                              int *dirty, const char *path)
{
    (void)path;
    if (!window) {
        tiku_rect_t frame = {area.x+(area.w-312)/2, area.y+(area.h-166)/2, 312, 166};
        tiku_dialog_init(&dialog);
        save_failed = 0;
        window = tiku_workspace_open(workspace, "Touch Preferences", frame, paint, dirty, NULL);
        if (!window) return NULL;
        tiku_window_set_handlers(window, event, NULL, closed);
        tiku_window_set_size_controls(window, 0, 0);
    }
    tiku_workspace_activate(workspace, window);
    if (dirty) *dirty = 1;
    return window;
}
