/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * paint.c - touch geometry and rendering for an 800 by 480 surface.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_font.h"
#include "tiku_text_layout.h"
#include "tiku_ui.h"
#include <string.h>

/** @brief Return the fixed rectangle for a product control. */
static tiku_rect_t box(int id)
{
    if (id == ACTION_HOME) return (tiku_rect_t){16, 12, 88, 40};
    if (id == ACTION_BACK) return (tiku_rect_t){116, 12, 88, 40};
    if (id >= ACTION_CALCULATOR && id <= ACTION_SETTINGS)
        return (tiku_rect_t){32 + (id - ACTION_CALCULATOR) * 252, 160, 232, 144};
    if (id >= ACTION_HEX && id < ACTION_HEX + 16) {
        int n = id - ACTION_HEX;
        return (tiku_rect_t){32 + (n % 4) * 90, 194 + (n / 4) * 56, 78, 46};
    }
    if (id == ACTION_ERASE) return (tiku_rect_t){424, 370, 148, 44};
    if (id == ACTION_CLEAR) return (tiku_rect_t){588, 370, 148, 44};
    if (id >= ACTION_ROW && id < ACTION_ROW + 6)
        return (tiku_rect_t){32, 112 + (id - ACTION_ROW) * 44, 736, 40};
    if (id == ACTION_REFRESH) return (tiku_rect_t){32, 390, 160, 44};
    if (id == ACTION_PREVIOUS) return (tiku_rect_t){420, 390, 160, 44};
    if (id == ACTION_NEXT) return (tiku_rect_t){608, 390, 160, 44};
    if (id >= ACTION_SWAP && id <= ACTION_SAVE) return (tiku_rect_t){32 + (id - ACTION_SWAP) * 184, 330, 168, 44};
    return (tiku_rect_t){0};
}

/** @brief Test logical coordinates without extending the right/bottom edge. */
static int contains(tiku_rect_t r, int x, int y)
{ return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

int surface_hit(const surface_view_t *view, int x, int y)
{
    if (view->stopped) return 0;
    if (contains(box(ACTION_HOME), x, y)) return ACTION_HOME;
    if (contains(box(ACTION_BACK), x, y)) return ACTION_BACK;
    if (view->foreground == SURFACE_HOME) {
        for (int id = ACTION_CALCULATOR; id <= ACTION_SETTINGS; ++id)
            if (contains(box(id), x, y)) return id;
    } else if (view->foreground == SURFACE_CALCULATOR) {
        for (int id = ACTION_HEX; id <= ACTION_CLEAR; ++id)
            if (contains(box(id), x, y)) return id;
    } else if (view->foreground == SURFACE_BROWSER) {
        for (unsigned i = 0; i < view->row_count; ++i)
            if (contains(box(ACTION_ROW + (int)i), x, y)) return ACTION_ROW + (int)i;
        if (contains(box(ACTION_REFRESH), x, y)) return ACTION_REFRESH;
        if (view->previous && contains(box(ACTION_PREVIOUS), x, y)) return ACTION_PREVIOUS;
        if (view->more && contains(box(ACTION_NEXT), x, y)) return ACTION_NEXT;
    } else if (view->foreground == SURFACE_SETTINGS) {
        if (view->settings_status == SURFACE_SETTINGS_UNAVAILABLE || view->settings_status == SURFACE_SETTINGS_INCOMPATIBLE) return 0;
        for (int id = ACTION_SWAP; id <= ACTION_SAVE; ++id)
            if (contains(box(id), x, y)) return id;
    }
    return 0;
}

/** @brief Paint a control with its current press state. */
static void button(tiku_surface_t *s, const surface_view_t *v, int id, const char *label, int enabled)
{
    tiku_rect_t r = box(id);
    if (r.y >= s->clip.y + s->clip.h || r.y + r.h <= s->clip.y) return;
    tiku_ui_button(s, r, label, enabled ? (v->pressed == id ? TIKU_S_PRESSED : 0) : TIKU_S_DISABLED);
}

/** @brief Skip frozen-font text whose ink bounds miss the current paint rows. */
static void colored_text(tiku_surface_t *s, const tiku_font_t *font, int x, int y,
                         const char *value, tiku_rgb_t color)
{
    if (y - font->ascent >= s->clip.y + s->clip.h || y + font->height - font->ascent <= s->clip.y) return;
    tiku_text(s, font, x, y, value, color);
}

/** @brief Draw scale-one text with the selected frozen font. */
static void text(tiku_surface_t *s, int x, int y, const char *value, int bold)
{ colored_text(s, bold ? tiku_font_bold() : tiku_font_plain(), x, y, value, 0x20343f); }

/** @brief Fit media text to its column without splitting valid UTF-8 code points. */
static void fitted_text(tiku_surface_t *s,int x,int y,const char *value,int width,int bold)
{
    const tiku_font_t *font=bold?tiku_font_bold():tiku_font_plain();
    if(y-font->ascent>=s->clip.y+s->clip.h||y+font->height-font->ascent<=s->clip.y)return;
    char line[128];
    (void)tiku_text_elide(bold?TIKU_MEASURE_BOLD:TIKU_MEASURE_PLAIN,value,width,line,sizeof line);
    colored_text(s,font,x,y,line,0x20343f);
}

/** @brief Show storage outcomes with an explicit refresh action. */
static const char *storage_status(tiku_resource_error_t status)
{
    switch (status) {
    case TIKU_RESOURCE_OK: return "Read-only media";
    case TIKU_RESOURCE_PENDING: return "Loading media...";
    case TIKU_RESOURCE_UNAVAILABLE: return "Media unavailable. Refresh to retry.";
    case TIKU_RESOURCE_STALE_CURSOR: return "Media changed. Refresh to reload.";
    case TIKU_RESOURCE_CANCELLED: return "Request cancelled";
    default: return "Media request failed. Refresh to retry.";
    }
}

void surface_paint(tiku_surface_t *s, const surface_view_t *v)
{
    static const char *titles[] = {"TikuSurface", "Word calculator", "Media browser", "Device information"};
    tiku_fill(s, (tiku_rect_t){0, 0, 800, 480}, 0xe9edf1);
    tiku_fill(s, (tiku_rect_t){0, 0, 800, 64}, 0x172b34);
    button(s, v, ACTION_HOME, "Home", 1);
    button(s, v, ACTION_BACK, "Back", 1);
    colored_text(s, tiku_font_bold(), 228, 37, titles[v->foreground], 0xffffff);
    colored_text(s, tiku_font_plain(), 677, 37, "LOCAL DESKTOP", 0xc6d4dc);
    if (v->foreground == SURFACE_HOME) {
        text(s, 32, 112, "Your applications", 1);
        text(s, 32, 139, "Select an application to begin.", 0);
        button(s, v, ACTION_CALCULATOR, "Word calculator", 1);
        button(s, v, ACTION_BROWSER, "Media browser", 1);
        button(s, v, ACTION_SETTINGS, "Device information", 1);
        text(s, 48, 334, "Hex / decimal / octal / binary", 0);
        text(s, 300, 334, "Browse folders and read files", 0);
        text(s, 552, 334, "Platform and capabilities", 0);
        if (v->retiring) text(s, 32, 400, "Finishing the previous request. Home cancels a queued launch.", 0);
        else if (v->status != TIKU_RESOURCE_OK) text(s, 32, 400, "Application could not start. Select an application to retry.", 0);
    } else if (v->foreground == SURFACE_CALCULATOR) {
        text(s, 32, 106, "UNSIGNED 64-BIT WORD", 1);
        tiku_fill(s, (tiku_rect_t){32, 120, 348, 52}, 0xffffff);
        text(s, 48, 151, v->hex[0] ? v->hex : "0", 1);
        for (int i = 0; i < 16; ++i) {
            char label[2] = {"0123456789ABCDEF"[i], 0};
            button(s, v, ACTION_HEX + i, label, 1);
        }
        static const char *labels[] = {"Hexadecimal", "Decimal", "Octal", "Binary"};
        for (int i = 0; i < 4; ++i) {
            text(s, 424, 105 + i * 60, labels[i], 1);
            char line[33] = {0};
            size_t n = strlen(v->bases[i]);
            memcpy(line, v->bases[i], n > 32 ? 32 : n);
            text(s, 424, 129 + i * 60, line, 0);
            if (n > 32) text(s, 424, 145 + i * 60, v->bases[i] + 32, 0);
        }
        button(s, v, ACTION_ERASE, "Erase", 1);
        button(s, v, ACTION_CLEAR, "Clear", 1);
        if (v->status == TIKU_RESOURCE_LIMIT) text(s, 32, 438, "Maximum: 16 hexadecimal digits (64 bits).", 0);
        else if (v->status == TIKU_RESOURCE_INVALID) text(s, 32, 438, "Enter hexadecimal digits 0-9 or A-F.", 0);
    } else if (v->foreground == SURFACE_BROWSER) {
        fitted_text(s, 32, 96, v->path, 736, 1);
        if (v->reading && v->status == TIKU_RESOURCE_OK) {
            text(s, 32, 132, v->preview_hex ? "Binary / non-ASCII: first 128 bytes in hexadecimal" : "Text preview (up to 1024 bytes)", 1);
            size_t at = 0;
            for (unsigned row = 0; row < 10 && v->preview[at]; ++row) {
                char line[91] = {0}; unsigned n = 0;
                while (n < 90 && v->preview[at] && v->preview[at] != '\n') line[n++] = v->preview[at++];
                if (v->preview[at] == '\n') ++at;
                fitted_text(s, 32, 160 + (int)row * 20, line, 736, 0);
            }
        } else if (v->status == TIKU_RESOURCE_OK && !v->row_count) text(s, 32, 148, "This folder is empty.", 0);
        for (unsigned i = 0; i < v->row_count; ++i) {
            tiku_rect_t r = box(ACTION_ROW + (int)i);
            tiku_fill(s, r, v->pressed == ACTION_ROW + (int)i ? 0xc6dce8 : 0xffffff);
            fitted_text(s, 48, r.y + 25, v->rows[i], 632, 0);
            text(s, 696, r.y + 25, v->directory[i] ? "Folder" : "File", 0);
        }
        button(s, v, ACTION_REFRESH, "Refresh", 1);
        button(s, v, ACTION_PREVIOUS, "Previous", v->previous);
        button(s, v, ACTION_NEXT, "Next", v->more);
    } else {
        text(s, 32, 116, v->platform_name, 1);
        text(s, 32, 158, "Stomme portable UI  /  800 x 480  /  one foreground application", 0);
        text(s, 32, 202, v->media_description, 0);
        text(s, 32, 246, v->services_description, 0);
        text(s, 32, 290, "Touch orientation: choose axes, then Save to apply.", 0);
        button(s, v, ACTION_SWAP, v->calibration.swap ? "Swap axes: on" : "Swap axes: off", 1);
        button(s, v, ACTION_INVERT_X, v->calibration.invert_x ? "Reverse X: on" : "Reverse X: off", 1);
        button(s, v, ACTION_INVERT_Y, v->calibration.invert_y ? "Reverse Y: on" : "Reverse Y: off", 1);
        button(s, v, ACTION_SAVE, "Save", v->settings_status != SURFACE_SETTINGS_UNAVAILABLE);
        static const char *states[] = {"Default calibration; panel check required", "Calibration saved",
            "Saving calibration", "Invalid record; using defaults", "Settings unavailable",
            "Save failed; active calibration unchanged", "Unsupported record; using defaults"};
        if (v->settings_status < sizeof states / sizeof states[0]) text(s, 32, 410, states[v->settings_status], 0);
    }
    tiku_fill(s, (tiku_rect_t){0, 448, 800, 32}, 0xd8e1e7);
    const char *status = v->clock_failed ? "Clock unavailable" : v->display_failed ? "Display submission failed" :
        v->foreground == SURFACE_BROWSER ? storage_status(v->status) : v->platform_name;
    text(s, 16, 469, status, 0);
}
