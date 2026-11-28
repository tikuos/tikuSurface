/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * orbit.c - orbiting satellites and the desktop's presented frame rate.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "orbit.h"
#include "bridge.h"
#include "tiku_font.h"
#include "tiku_trk_builtin.h"
#include "tiku_ui.h"
#include <math.h>
#include <stdio.h>

static tiku_window_t *window;
static int64_t sample_start, last_step;
static uint64_t first_frame;
static unsigned fps_tenths;
static int measured;
static float phase;
static const float speeds[] = {1.4f, -0.85f, 0.6f};
static const tiku_rgb_t colors[] = {0x64ddd2, 0xff9696, 0xa3b9ff};
static struct { int x, y; } points[3][6];
static tiku_rect_t previous_bounds;

static int dot_radius(int trail)
{
    return trail == 0 ? 5 : trail < 3 ? 2 : 1;
}

/* Positions are relative to the content origin so a moved window keeps its orbit. */
static tiku_rect_t position_satellites(tiku_rect_t content)
{
    int width = content.w - 16, height = content.h - 44;
    tiku_rect_t bounds = {0};
    for (int i = 0; i < 3; ++i) {
        int rx = (width - 20) * (i + 2) / 8;
        int ry = (height - 18) * (i + 2) / 8;
        for (int trail = 0; trail < 6; ++trail) {
            float a = phase * speeds[i] + i * 2.1f - trail * 0.10f * speeds[i];
            int x = 8 + width / 2 + (int)(rx * cosf(a));
            int y = 8 + height / 2 + (int)(ry * sinf(a));
            int r = dot_radius(trail);
            points[i][trail].x = x;
            points[i][trail].y = y;
            tiku_rect_t dot = {x - r, y - r, 2 * r + 1, 2 * r + 1};
            bounds = tiku_rect_union(bounds, dot);
        }
    }
    return bounds;
}

static void disc(tiku_surface_t *s, int x, int y, int radius, tiku_rgb_t color)
{
    int side = radius * 2 + 1;
    tiku_fill_round(s, (tiku_rect_t){x - radius, y - radius, side, side},
                    radius, TIKU_CORNER_ALL, color);
}

static void paint(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r, void *ctx)
{
    const tiku_rgb_t sky = 0x152236;
    char label[48];
    (void)w;
    (void)ctx;

    tiku_rect_t field = {r.x + 8, r.y + 8, r.w - 16, r.h - 44};
    if (s->clip.x < field.x || s->clip.y < field.y ||
        s->clip.x + s->clip.w > field.x + field.w ||
        s->clip.y + s->clip.h > field.y + field.h || s->record)
        tiku_ui_panel(s, r);
    tiku_fill(s, field, sky);
    int cx = field.x + field.w / 2, cy = field.y + field.h / 2;
    for (int y = field.y + 8; y < field.y + field.h; y += 16) {
        for (int x = field.x + 8; x < field.x + field.w - 4; x += 16)
            tiku_pixel(s, x, y, 0x34485d);
    }
    disc(s, cx, cy, 9, 0x86592e);
    disc(s, cx, cy, 6, 0xffc760);
    for (int i = 0; i < 3; ++i) {
        for (int trail = 5; trail >= 0; --trail) {
            int x = r.x + points[i][trail].x, y = r.y + points[i][trail].y;
            tiku_rgb_t color = trail ? tiku_tint(colors[i], 1.0f - trail * 0.12f) : colors[i];
            disc(s, x, y, dot_radius(trail), color);
        }
    }
    if (!s->record && s->clip.y + s->clip.h <= field.y + field.h) return;
    if (measured)
        snprintf(label, sizeof label, "Desktop FPS   %u.%u", fps_tenths / 10, fps_tenths % 10);
    else
        snprintf(label, sizeof label, "Desktop FPS   …");
    const tiku_font_t *font = tiku_font_bold();
    tiku_text(s, font, r.x + 10, field.y + field.h + 8 + font->ascent, label, TIKU_C_TEXT);
}

void surface_orbit_tick(int64_t now)
{
    if (!window || window->minimized || window->hidden) {
        last_step = -1;
        return;
    }
    if (last_step < 0 || now < last_step) {
        last_step = sample_start = now;
        first_frame = surface_desktop_presented_frames();
        measured = 0;
    }
    int64_t elapsed = now - last_step;
    if (elapsed < 16667) return;
    last_step = now;
    /* The three angular velocities repeat together after 40 pi radians. */
    phase = fmodf(phase + (float)elapsed / 1000000.0f, 125.663706f);
    tiku_rect_t content = tiku_window_content(window);
    tiku_rect_t bounds = position_satellites(content);
    tiku_rect_t damage = tiku_rect_union(previous_bounds, bounds);
    previous_bounds = bounds;
    damage.x += content.x;
    damage.y += content.y;
    if (now - sample_start >= 1000000) {
        uint64_t frames = surface_desktop_presented_frames();
        uint64_t interval = (uint64_t)(now - sample_start);
        unsigned reading = (unsigned)(((frames - first_frame) * 10000000u + interval / 2) / interval);
        if (!measured || reading != fps_tenths) damage = content;
        fps_tenths = reading;
        measured = 1;
        sample_start = now;
        first_frame = frames;
    }
    tiku_trk_builtin_damage(window, damage);
}

static void closed(tiku_window_t *w, void *ctx)
{
    (void)w;
    (void)ctx;
    window = NULL;
}

tiku_window_t *surface_orbit_open(tiku_workspace_t *workspace, tiku_rect_t area,
                                  int *dirty, const char *path)
{
    (void)path;
    if (!window) {
        int width = 316, height = 200;
        tiku_rect_t frame = {area.x + (area.w - width) / 2, area.y + (area.h - height) / 2,
                             width, height};
        last_step = -1;
        measured = 0;
        phase = 0;
        window = tiku_workspace_open(workspace, "Orbit", frame, paint, NULL, NULL);
        if (!window) return NULL;
        tiku_window_set_opaque(window, 1);
        previous_bounds = position_satellites(tiku_window_content(window));
        tiku_window_set_handlers(window, NULL, NULL, closed);
        tiku_window_set_size_controls(window, 0, 0);
    }
    tiku_workspace_activate(workspace, window);
    if (dirty) *dirty = 1;
    return window;
}
