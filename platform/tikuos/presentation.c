/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * presentation.c - chunked conversion and per-buffer accumulated damage.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "presentation.h"

#define RGB_BYTES (800u * 480u * sizeof(uint16_t))
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
    int x = a.x < b.x ? a.x : b.x, y = a.y < b.y ? a.y : b.y;
    int right = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int bottom = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return (tiku_rect_t){x, y, right - x, bottom - y};
}

int surface_presentation_init(surface_presentation_t *p, uint16_t *a, size_t an,
    uint16_t *b, size_t bn, unsigned front, unsigned budget, uint64_t *sequence,
    const surface_scanout_ops_t *ops, void *context)
{
    if (!p || !a || !b || an < RGB_BYTES || bn < RGB_BYTES ||
        ((uintptr_t)a & 63u) || ((uintptr_t)b & 63u) ||
        !disjoint(a, RGB_BYTES, b, RGB_BYTES) || front > 1 || !budget || budget > 4096 ||
        !sequence || !ops || !ops->submit || !ops->poll) return 0;
    *p = (surface_presentation_t){.buffers = {a, b}, .front = front,
        .pixel_budget = budget, .sequence = sequence, .ops = ops, .context = context,
        .pending = {{0, 0, 800, 480}, {0, 0, 800, 480}}};
    return 1;
}

tiku_present_result_t surface_presentation_submit(void *context, const tiku_present_view_t *v)
{
    surface_presentation_t *p = context;
    if (!p || !p->ops || p->failed) return TIKU_PRESENT_ERROR;
    if (p->phase) return TIKU_PRESENT_BUSY;
    if (!v || !v->owner || !v->token || !v->pixels ||
        ((uintptr_t)v->pixels & 3u) || v->format != TIKU_PIXELS_XRGB8888 ||
        v->width != 800 || v->height != 480 || v->stride < 3200 ||
        (v->stride & 3u) || v->stride > SIZE_MAX / 480 || v->bytes < v->stride * 480 ||
        v->damage.x < 0 || v->damage.y < 0 || v->damage.x >= 800 || v->damage.y >= 480 ||
        v->damage.w <= 0 || v->damage.h <= 0 || v->damage.w > 800 - v->damage.x ||
        v->damage.h > 480 - v->damage.y ||
        !disjoint(v->pixels, v->bytes, p->buffers[0], RGB_BYTES) ||
        !disjoint(v->pixels, v->bytes, p->buffers[1], RGB_BYTES)) return TIKU_PRESENT_ERROR;
    p->source = *v;
    for (unsigned i = 0; i < 2; ++i) p->pending[i] = unite(p->pending[i], v->damage);
    p->region = p->pending[p->front ^ 1u];
    p->x = (unsigned)p->region.x; p->y = (unsigned)p->region.y;
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
        unsigned right = (unsigned)(p->region.x + p->region.w);
        unsigned bottom = (unsigned)(p->region.y + p->region.h);
        unsigned remaining = p->pixel_budget;
        while (remaining && p->y < bottom) {
            const uint32_t *row = (const uint32_t *)((const unsigned char *)p->source.pixels + p->y * p->source.stride);
            unsigned count = right - p->x;
            if (count > remaining) count = remaining;
            const uint32_t *source = row + p->x;
            uint16_t *destination = p->buffers[back] + p->y * 800u + p->x;
            for (unsigned i = 0; i < count; ++i) {
                uint32_t color = source[i];
                destination[i] = (uint16_t)(((color >> 8) & 0xf800u) |
                    ((color >> 5) & 0x07e0u) | ((color >> 3) & 0x001fu));
            }
            p->x += count;
            remaining -= count;
            if (p->x == right) { p->x = (unsigned)p->region.x; ++p->y; }
        }
        if (p->y == bottom) p->phase = SUBMIT;
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
        p->pending[back] = (tiku_rect_t){0};
    }
    if (result != TIKU_PRESENT_OK && result != TIKU_PRESENT_ERROR) return TIKU_PRESENT_BUSY;
    return complete(p, result);
}

int surface_presentation_closed(surface_presentation_t *p)
{
    if (!p) return 0;
    p->failed = 1;
    if (!p->phase) return 1;
    return complete(p, TIKU_PRESENT_ERROR) == TIKU_PRESENT_ERROR;
}
