/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_orbit.c - presented frame samples and Orbit's window lifetime.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "orbit.h"
#include "bridge.h"
#include "tiku_font.h"
#include "tiku_host.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int present_result, redraws;
static char label[48];
static tiku_rect_t damage;

int surface_desktop_present(const tiku_surface_t *surface, tiku_rect_t damage)
{
    assert(surface != NULL && damage.w > 0 && damage.h > 0);
    return present_result;
}

void tiku_trk_builtin_damage(tiku_window_t *w, tiku_rect_t rect)
{
    assert(w->open && rect.w > 0 && rect.h > 0);
    damage = rect;
    ++redraws;
}

void __real_tiku_text(tiku_surface_t *, const tiku_font_t *, int, int,
                      const char *, tiku_rgb_t);

void __wrap_tiku_text(tiku_surface_t *s, const tiku_font_t *font, int x, int y,
                      const char *text, tiku_rgb_t color)
{
    __real_tiku_text(s, font, x, y, text, color);
    if (strncmp(text, "Desktop FPS", 11) == 0)
        snprintf(label, sizeof label, "%s", text);
}

static void expect_label(tiku_window_t *w, tiku_surface_t *surface, const char *expected)
{
    label[0] = '\0';
    w->draw(w, surface, tiku_window_content(w), w->context);
    assert(strcmp(label, expected) == 0);
}

static void check_partial_frames(tiku_window_t *w, tiku_surface_t *surface)
{
    tiku_rect_t content = tiku_window_content(w);
    size_t bytes = (size_t)surface->w * surface->h * sizeof *surface->px;
    tiku_surface_t reference = *surface;
    reference.px = malloc(bytes);
    assert(reference.px);
    memcpy(reference.px, surface->px, bytes);
    w->draw(w, surface, content, NULL);
    surface_orbit_tick(10000000);
    for (int i = 1; i <= 96; ++i) {
        surface_orbit_tick(10000000 + (int64_t)i * 101333);
        tiku_clip_damage(surface, &damage);
        w->draw(w, surface, content, NULL);
        tiku_clip_damage(surface, NULL);
        w->draw(w, &reference, content, NULL);
        assert(memcmp(surface->px, reference.px, bytes) == 0);
    }
    free(reference.px);
}

int main(void)
{
    tiku_rect_t area = {0, 0, SURFACE_DESKTOP_W, SURFACE_DESKTOP_H};
    static uint32_t pixels[SURFACE_DESKTOP_W * SURFACE_DESKTOP_H];
    tiku_surface_t target = {.px = pixels, .w = area.w, .h = area.h, .clip = area, .scale = 1};
    tiku_surface_t *surface = &target;
    void *storage = calloc(1, tiku_workspace_size());
    assert(storage);
    tiku_workspace_t *desk = tiku_workspace_init(storage, tiku_workspace_size(), surface, NULL);
    tiku_host_t *host = tiku_host_open(area.w, area.h, "Orbit");
    int dirty = 0;
    assert(surface && desk && host);
    tiku_window_t *w = surface_orbit_open(desk, area, &dirty, NULL);
    assert(w && dirty && surface_orbit_open(desk, area, &dirty, NULL) == w);
    assert(w->opaque);
    surface_orbit_tick(0);
    expect_label(w, surface, "Desktop FPS   …");

    surface_orbit_tick(17000);
    tiku_rect_t content = tiku_window_content(w);
    assert(damage.w < content.w && damage.h < content.h);
    assert(damage.y + damage.h <= content.y + content.h - 36);

    for (int i = 0; i < 7; ++i) assert(tiku_host_present(host, surface) == 0);
    present_result = -1;
    assert(tiku_host_present(host, surface) < 0);
    surface_orbit_tick(1500000);
    expect_label(w, surface, "Desktop FPS   4.7");

    w->minimized = 1;
    surface_orbit_tick(2000000);
    assert(redraws == 2);
    w->minimized = 0;
    surface_orbit_tick(8000000);
    expect_label(w, surface, "Desktop FPS   …");
    surface_orbit_tick(9000000);
    expect_label(w, surface, "Desktop FPS   0.0");

    tiku_workspace_close(desk, w);
    surface_orbit_tick(10000000);
    assert(redraws == 3);
    w = surface_orbit_open(desk, area, &dirty, NULL);
    assert(w);
    expect_label(w, surface, "Desktop FPS   …");
    check_partial_frames(w, surface);
    tiku_workspace_close(desk, w);
    tiku_workspace_deinit(desk);
    free(storage);
    tiku_host_close(host);
    puts("Orbit: completed frames, elapsed time, restore and reopen passed");
    return 0;
}
