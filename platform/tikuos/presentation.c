/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * presentation.c - chunked conversion and per-buffer accumulated damage.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../system/product/surface_panel.h"
#include "presentation.h"
#include "../../desktop/mouse.h"
#include <string.h>

enum { IDLE, CONVERT, SUBMIT, WAIT };

/** @brief Test complete pointer spans without overflowing their end addresses. */
static int disjoint(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= UINTPTR_MAX - an && y <= UINTPTR_MAX - bn &&
        (x + an <= y || y + bn <= x);
}

/** @brief Union two valid on-screen rectangles; zero width denotes empty. */
static tiku_rect_t unite(tiku_rect_t a, tiku_rect_t b)
{
    if (!a.w) return b;
    if (!b.w) return a;
    int x = a.x < b.x ? a.x : b.x, y = a.y < b.y ? a.y : b.y;
    int right = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int bottom = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return (tiku_rect_t){x, y, right - x, bottom - y};
}

/** @brief Pixels in a rectangle; zero width denotes empty. */
static uint32_t area(tiku_rect_t r)
{
    return r.w > 0 ? (uint32_t)r.w * (uint32_t)r.h : 0u;
}

/**
 * @brief Add @p r to one buffer's parts.
 * @note Two parts merge while their union is at most twice their area; past
 * SURFACE_PRESENT_PARTS the pair whose union adds the least area merges.
 */
static void add_part(tiku_rect_t *parts, tiku_rect_t r)
{
    tiku_rect_t all[SURFACE_PRESENT_PARTS + 1];
    unsigned n = 0;
    for (unsigned i = 0; i < SURFACE_PRESENT_PARTS; ++i)
        if (parts[i].w) all[n++] = parts[i];
    all[n++] = r;
    for (;;) {
        unsigned a = 0, b = 0, near = 0;
        uint32_t least = UINT32_MAX;
        for (unsigned i = 0; i < n && !near; ++i)
            for (unsigned j = i + 1; j < n && !near; ++j) {
                uint32_t joined = area(unite(all[i], all[j]));
                uint32_t apart = area(all[i]) + area(all[j]);
                uint32_t growth = joined > apart ? joined - apart : 0u;
                near = joined <= 2u * apart;
                if (near || growth < least) { a = i; b = j; least = growth; }
            }
        if (!near && n <= SURFACE_PRESENT_PARTS) break;
        all[a] = unite(all[a], all[b]);
        all[b] = all[--n];
    }
    for (unsigned i = 0; i < SURFACE_PRESENT_PARTS; ++i)
        parts[i] = i < n ? all[i] : (tiku_rect_t){0};
}

/* The touch mark: a disc of radius MARK_R, a one-pixel dark edge, a white
 * ring two pixels wide and a half-tone selection blue inside, so it reads on
 * the white lists and on the blue desk alike. */
#define MARK_R 14
#define MARK_EDGE 0x2104u
#define MARK_RING 0xffffu
#define MARK_FILL 0x3333u

/** @brief The part of the screen a mark centred on (@p x, @p y) covers. */
static tiku_rect_t mark_rect(int x, int y, int size, int hotspot)
{
    int x0 = x - hotspot, y0 = y - hotspot;
    int x1 = x0 + size, y1 = y0 + size;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SURFACE_DESKTOP_W) x1 = SURFACE_DESKTOP_W;
    if (y1 > SURFACE_DESKTOP_H) y1 = SURFACE_DESKTOP_H;
    return x1 > x0 && y1 > y0 ? (tiku_rect_t){x0, y0, x1 - x0, y1 - y0} : (tiku_rect_t){0};
}

/** @brief Put back the pixels buffer @p b's mark covers. */
static void unmark(surface_presentation_t *p, unsigned b)
{
    tiku_rect_t r = p->marked[b];
    for (int row = 0; row < r.h; ++row)
        memcpy(p->buffers[b] + (r.y + row) * SURFACE_DESKTOP_W + r.x, p->under[b] + row * r.w,
            (size_t)r.w * sizeof(uint16_t));
    p->marked[b] = (tiku_rect_t){0};
}

/* Each row runs from bit 0 at the left to bit 15 at the right. Shapes
 * follow tiku_host_set_cursor: arrow, text, horizontal, vertical and
 * diagonal resize, and hand. */
static const uint16_t cursors[][16] = {
    {1,3,7,15,31,63,127,255,511,1023,2047,127,123,241,240,96},
    {0,0x0770,0x03e0,0x01c0,0x01c0,0x01c0,0x01c0,0x01c0,
     0x01c0,0x01c0,0x01c0,0x01c0,0x03e0,0x0770},
    {0,0,0,0,0x1008,0x300c,0x7ffe,0xffff,0x7ffe,0x300c,0x1008},
    {0x0080,0x01c0,0x03e0,0x07f0,0x01c0,0x01c0,0x01c0,0x01c0,
     0x01c0,0x01c0,0x01c0,0x07f0,0x03e0,0x01c0,0x0080},
    {0,0x007e,0x003e,0x001e,0x003e,0x0076,0x00e2,0x01c0,
     0x0380,0x4700,0x6e00,0x7c00,0x7800,0x7c00,0x7e00},
    {0x0030,0x0078,0x0078,0x0078,0x03f8,0x0ff8,0x1ff8,0x1ffb,
     0x1fff,0x1ffe,0x1ffc,0x0ff8,0x0ff0,0x07f0,0x07f0}
};

int surface_cursor_pixel(int shape, int size, int x, int y)
{
    if (shape < 0 || shape >= (int)(sizeof cursors / sizeof cursors[0]) ||
        x < 0 || y < 0 || x >= size || y >= size) return -1;
    x = x * 16 / size;
    y = y * 16 / size;
    const uint16_t *rows = cursors[shape];
    unsigned bit = 1u << x;
    if (!(rows[y] & bit)) return -1;
    if (x == 0 || y == 0 || y == 15 || !(rows[y-1] & rows[y+1] & bit) ||
        !(rows[y] & (bit >> 1)) || !(rows[y] & (bit << 1))) return 0;
    return 0xffff;
}

/** @brief Draw the wanted mark into buffer @p b, keeping the pixels it covers. */
static void mark(surface_presentation_t *p, unsigned b)
{
    unmark(p, b);
    p->drawn[b] = p->mark_seq;
    if (!p->mark_on) return;
    int cursor = p->mark_on == 2;
    int size = cursor ? p->cursor_size : SURFACE_MARK_SIZE;
    int hotspot = cursor ? (p->cursor_shape == 0 ? 0 : 7 * size / 16) : size / 2;
    tiku_rect_t r = mark_rect(p->mark_x, p->mark_y, size, hotspot);
    for (int row = 0; row < r.h; ++row) {
        uint16_t *line = p->buffers[b] + (r.y + row) * SURFACE_DESKTOP_W + r.x;
        int dy = r.y + row - p->mark_y;
        memcpy(p->under[b] + row * r.w, line, (size_t)r.w * sizeof(uint16_t));
        for (int i = 0; i < r.w; ++i) {
            int dx = r.x + i - p->mark_x, d = dx * dx + dy * dy;
            if (cursor) {
                int color = surface_cursor_pixel(p->cursor_shape, size, dx + hotspot, dy + hotspot);
                if (color >= 0) line[i] = (uint16_t)color;
                continue;
            }
            if (d > MARK_R * MARK_R) continue;
            if (d > (MARK_R - 1) * (MARK_R - 1)) line[i] = MARK_EDGE;
            else if (d > (MARK_R - 3) * (MARK_R - 3)) line[i] = MARK_RING;
            else line[i] = (uint16_t)(((line[i] & 0xf7deu) >> 1) + ((MARK_FILL & 0xf7deu) >> 1));
        }
    }
    p->marked[b] = r;
}

/** @brief Bring the buffer on show to the wanted mark and make the change visible. */
static void mark_front(surface_presentation_t *p)
{
    unsigned f = p->front;
    if (p->drawn[f] == p->mark_seq) return;
    tiku_rect_t was = p->marked[f];
    mark(p, f);
    tiku_rect_t changed = unite(was, p->marked[f]);
    if (p->mark_on == 2 && was.w && p->marked[f].w &&
        area(changed) > 2 * (area(was) + area(p->marked[f]))) {
        (void)p->ops->amend(p->context, f, was);
        changed = p->marked[f];
    }
    if (changed.w) (void)p->ops->amend(p->context, f, changed);
}

static void set_mark(surface_presentation_t *p, int on, int shape, int size, int x, int y)
{
    if (!p || !p->ops || !p->ops->amend || p->failed) return;
    if (on == p->mark_on && (!on || (shape == p->cursor_shape && size == p->cursor_size &&
        x == p->mark_x && y == p->mark_y))) return;
    p->cursor_shape = shape; p->cursor_size = size;
    p->mark_on = on; p->mark_x = x; p->mark_y = y; ++p->mark_seq;
    /* A submission keeps both buffers as they are until it completes. */
    if (p->phase == IDLE || p->phase == CONVERT) mark_front(p);
}

void surface_presentation_mark(surface_presentation_t *p, int on, int x, int y)
{
    set_mark(p, on != 0, 0, SURFACE_MARK_SIZE, x, y);
}

void surface_presentation_cursor(surface_presentation_t *p, int shape, int size, int x, int y)
{
    if (shape >= (int)(sizeof cursors / sizeof cursors[0])) shape = 0;
    if (size < 1 || size > SURFACE_MARK_SIZE) size = SURFACE_CURSOR_NORMAL;
    set_mark(p, shape < 0 ? 0 : 2, shape, size, x, y);
}

/** @brief Step past converted parts of @p back; zero once none remains. */
static int next_part(surface_presentation_t *p, unsigned back)
{
    while (p->part < SURFACE_PRESENT_PARTS && p->pending[back][p->part].w) {
        tiku_rect_t r = p->pending[back][p->part];
        if (p->y < (unsigned)(r.y + r.h)) return 1;
        if (++p->part < SURFACE_PRESENT_PARTS) {
            p->x = (unsigned)p->pending[back][p->part].x;
            p->y = (unsigned)p->pending[back][p->part].y;
        }
    }
    return 0;
}

int surface_presentation_init(surface_presentation_t *p, uint16_t *a, size_t an,
    uint16_t *b, size_t bn, unsigned front, unsigned budget, uint64_t *sequence,
    const surface_scanout_ops_t *ops, void *context)
{
    if (!p || !a || !b || an < SURFACE_SCANOUT_BYTES || bn < SURFACE_SCANOUT_BYTES ||
        ((uintptr_t)a & 63u) || ((uintptr_t)b & 63u) ||
        !disjoint(a, SURFACE_SCANOUT_BYTES, b, SURFACE_SCANOUT_BYTES) || front > 1 || !budget || budget > 4096 ||
        !sequence || !ops || !ops->submit || !ops->poll) return 0;
    *p = (surface_presentation_t){.buffers = {a, b}, .front = front,
        .pixel_budget = budget, .sequence = sequence, .ops = ops, .context = context,
        .pending = {{{0, 0, SURFACE_DESKTOP_W, SURFACE_DESKTOP_H}}, {{0, 0, SURFACE_DESKTOP_W, SURFACE_DESKTOP_H}}}};
    return 1;
}

tiku_present_result_t surface_presentation_submit(void *context, const tiku_present_view_t *v)
{
    surface_presentation_t *p = context;
    if (!p || !p->ops || p->failed) return TIKU_PRESENT_ERROR;
    if (p->phase) return TIKU_PRESENT_BUSY;
    if (!v || !v->owner || !v->token || !v->pixels ||
        ((uintptr_t)v->pixels & 3u) || v->format != TIKU_PIXELS_XRGB8888 ||
        v->width != SURFACE_DESKTOP_W || v->height != SURFACE_DESKTOP_H || v->stride < SURFACE_DESKTOP_W * sizeof(uint32_t) ||
        (v->stride & 3u) || v->stride > SIZE_MAX / SURFACE_DESKTOP_H || v->bytes < v->stride * SURFACE_DESKTOP_H ||
        v->damage.x < 0 || v->damage.y < 0 || v->damage.x >= SURFACE_DESKTOP_W || v->damage.y >= SURFACE_DESKTOP_H ||
        v->damage.w <= 0 || v->damage.h <= 0 || v->damage.w > SURFACE_DESKTOP_W - v->damage.x ||
        v->damage.h > SURFACE_DESKTOP_H - v->damage.y ||
        !disjoint(v->pixels, v->bytes, p->buffers[0], SURFACE_SCANOUT_BYTES) ||
        !disjoint(v->pixels, v->bytes, p->buffers[1], SURFACE_SCANOUT_BYTES)) return TIKU_PRESENT_ERROR;
    p->source = *v;
    for (unsigned i = 0; i < 2; ++i) add_part(p->pending[i], v->damage);
    const tiku_rect_t *parts = p->pending[p->front ^ 1u];
    p->region = (tiku_rect_t){0}; p->convert_px = 0;
    for (unsigned i = 0; i < SURFACE_PRESENT_PARTS; ++i)
        if (parts[i].w) { p->region = unite(p->region, parts[i]); p->convert_px += area(parts[i]); }
    p->part = 0;
    p->cpu = 0;
    p->x = (unsigned)parts[0].x; p->y = (unsigned)parts[0].y;
    /* The back buffer is converted without the mark it held when shown. */
    unmark(p, p->front ^ 1u);
    p->phase = CONVERT;
    return TIKU_PRESENT_OK;
}

/** @brief Complete the source lease; a rejected completion retains provider state. */
static tiku_present_result_t complete(surface_presentation_t *p, tiku_present_result_t result)
{
    if (result == TIKU_PRESENT_ERROR) p->failed = 1;
    if (!tiku_framebuffer_complete(p->source.owner, p->source.token, result)) {
        p->failed = 1;
        return TIKU_PRESENT_BUSY;
    }
    p->source = (tiku_present_view_t){0};
    p->phase = IDLE; p->ticket = 0;
    return result;
}

tiku_present_result_t surface_presentation_poll(surface_presentation_t *p)
{
    if (!p || !p->ops) return TIKU_PRESENT_ERROR;
    if (!p->phase) return p->failed ? TIKU_PRESENT_ERROR : TIKU_PRESENT_OK;
    if (p->failed) return TIKU_PRESENT_BUSY;
    unsigned back = p->front ^ 1u;
    if (p->phase == CONVERT) {
        unsigned remaining = p->pixel_budget;
        if (p->hw) {
            int state = p->ops->convert_poll(p->context);
            if (state > 0) return TIKU_PRESENT_BUSY;
            p->hw = 0;
            /* A part done moves on; a failed one the CPU converts again. */
            if (state == 0) p->y = (unsigned)(p->pending[back][p->part].y + p->pending[back][p->part].h);
            else p->cpu = 1;
        }
        /* Each part starts at its corner here: the hardware takes it whole. */
        if (p->ops->convert && !p->cpu && next_part(p, back)) {
            if (p->ops->convert(p->context, back, &p->source, p->pending[back][p->part]) == 0) {
                p->hw = 1;
                return TIKU_PRESENT_BUSY;
            }
            p->cpu = 1;
        }
        while (remaining && next_part(p, back)) {
            tiku_rect_t r = p->pending[back][p->part];
            unsigned right = (unsigned)(r.x + r.w);
            const uint32_t *row = (const uint32_t *)((const unsigned char *)p->source.pixels + p->y * p->source.stride);
            unsigned count = right - p->x;
            if (count > remaining) count = remaining;
            const uint32_t *source = row + p->x;
            uint16_t *destination = p->buffers[back] + p->y * SURFACE_DESKTOP_W + p->x;
            for (unsigned i = 0; i < count; ++i) {
                uint32_t color = source[i];
                destination[i] = (uint16_t)(((color >> 8) & 0xf800u) |
                    ((color >> 5) & 0x07e0u) | ((color >> 3) & 0x001fu));
            }
            p->x += count;
            remaining -= count;
            if (p->x == right) { p->x = (unsigned)r.x; ++p->y; }
        }
        if (!next_part(p, back)) {
            /* The frame goes out with the mark, so a flip never hides it. */
            if (p->ops->amend) {
                mark(p, back);
                p->region = unite(p->region, p->marked[back]);
            }
            p->phase = SUBMIT;
        }
        return TIKU_PRESENT_BUSY;
    }
    if (p->phase == SUBMIT) {
        if (!p->ticket) {
            if (*p->sequence == UINT64_MAX) return complete(p, TIKU_PRESENT_ERROR);
            p->ticket = ++*p->sequence;
        }
        tiku_present_result_t result = p->ops->submit(p->context, back, p->region, p->ticket);
        if (result == TIKU_PRESENT_ERROR) return complete(p, result);
        if (result == TIKU_PRESENT_OK) p->phase = WAIT;
        return TIKU_PRESENT_BUSY;
    }
    tiku_present_result_t result = p->ops->poll(p->context, p->ticket);
    if (result == TIKU_PRESENT_OK) {
        p->front = back;
        for (unsigned i = 0; i < SURFACE_PRESENT_PARTS; ++i) p->pending[back][i] = (tiku_rect_t){0};
    }
    if (result != TIKU_PRESENT_OK && result != TIKU_PRESENT_ERROR) return TIKU_PRESENT_BUSY;
    tiku_present_result_t done = complete(p, result);
    /* A mark moved or lifted during the submission follows now. */
    if (result == TIKU_PRESENT_OK && !p->failed && p->ops->amend) mark_front(p);
    return done;
}

int surface_presentation_closed(surface_presentation_t *p)
{
    if (!p) return 0;
    p->failed = 1;
    /* A hardware conversion still writing the back buffer ends first. */
    while (p->hw && p->ops->convert_poll(p->context) > 0) {
    }
    p->hw = 0;
    if (!p->phase) return 1;
    return complete(p, TIKU_PRESENT_ERROR) == TIKU_PRESENT_ERROR;
}
