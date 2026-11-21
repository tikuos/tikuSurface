/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * heap_profile.c - live and peak heap bytes by allocating call site.
 *
 * Linked only in a profile build (SURFACE_HEAP_PROFILE=1), whose link wraps
 * malloc, calloc, realloc and free.  Each block the desktop allocates through
 * them is recorded with its size and the return address of the call that
 * made it.  Blocks the C library allocates internally (strdup, stdio
 * buffers) go through _malloc_r and are not recorded.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bridge.h"
#include "bridge_os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TRACK_SLOTS 16384u           /* recorded blocks, a power of two */
#define TRACK_PROBE 64u              /* slots examined before a block goes unrecorded */
#define SITE_SLOTS 1024u             /* distinct call sites, a power of two */
#define REPORT_SITES 32u             /* sites printed, by peak bytes */
#define TOMBSTONE ((void *)1)

typedef struct {
    void *ptr;
    uint32_t size;
    uint32_t site;
} track_t;

typedef struct {
    uintptr_t pc;
    uint32_t live, peak, blocks;
} site_t;

static track_t tracks[TRACK_SLOTS];
static site_t sites[SITE_SLOTS];
static uint32_t unrecorded, live_total, peak_total;

void *__real_malloc(size_t n);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *old, size_t n);
void __real_free(void *p);

/** @brief The slot for call site @p pc, or SITE_SLOTS when the table is full. */
static uint32_t site_of(uintptr_t pc)
{
    uint32_t h = (uint32_t)(pc * 2654435761u) & (SITE_SLOTS - 1u);

    for (uint32_t i = 0; i < SITE_SLOTS; i++, h = (h + 1u) & (SITE_SLOTS - 1u)) {
        if (sites[h].pc == pc) return h;
        if (sites[h].pc == 0) {
            sites[h].pc = pc;
            return h;
        }
    }
    return SITE_SLOTS;
}

static uint32_t block_hash(const void *p)
{
    return (uint32_t)(((uintptr_t)p >> 3) * 2654435761u) & (TRACK_SLOTS - 1u);
}

/** @brief Record block @p p of @p n bytes made by the call at @p pc. */
static void record(void *p, size_t n, uintptr_t pc)
{
    uint32_t h, s, i;

    if (p == NULL) return;
    surface_os_desktop_lock();
    s = site_of(pc);
    h = block_hash(p);
    for (i = 0; i < TRACK_PROBE; i++, h = (h + 1u) & (TRACK_SLOTS - 1u)) {
        if (tracks[h].ptr == NULL || tracks[h].ptr == TOMBSTONE) break;
    }
    if (i == TRACK_PROBE || s == SITE_SLOTS) {
        unrecorded++;
    } else {
        tracks[h] = (track_t){ p, (uint32_t)n, s };
        sites[s].live += (uint32_t)n;
        sites[s].blocks++;
        if (sites[s].live > sites[s].peak) sites[s].peak = sites[s].live;
        live_total += (uint32_t)n;
        if (live_total > peak_total) peak_total = live_total;
    }
    surface_os_desktop_unlock();
}

/** @brief Forget block @p p, if it was recorded. */
static void forget(void *p)
{
    uint32_t h, i;

    if (p == NULL) return;
    surface_os_desktop_lock();
    h = block_hash(p);
    for (i = 0; i < TRACK_PROBE; i++, h = (h + 1u) & (TRACK_SLOTS - 1u)) {
        if (tracks[h].ptr == p) {
            site_t *s = &sites[tracks[h].site];

            s->live -= tracks[h].size;
            s->blocks--;
            live_total -= tracks[h].size;
            tracks[h].ptr = TOMBSTONE;
            break;
        }
        if (tracks[h].ptr == NULL) break;
    }
    surface_os_desktop_unlock();
}

void *__wrap_malloc(size_t n)
{
    void *p = __real_malloc(n);

    record(p, n, (uintptr_t)__builtin_return_address(0));
    return p;
}

void *__wrap_calloc(size_t count, size_t size)
{
    void *p = __real_calloc(count, size);

    record(p, count * size, (uintptr_t)__builtin_return_address(0));
    return p;
}

void *__wrap_realloc(void *old, size_t n)
{
    void *p = __real_realloc(old, n);

    if (p != NULL || n == 0u) forget(old);
    record(p, n, (uintptr_t)__builtin_return_address(0));
    return p;
}

void __wrap_free(void *p)
{
    forget(p);
    __real_free(p);
}

/** @brief Print the recorded totals and the sites with the largest peaks. */
void surface_desktop_heap_report(void (*print)(const void *bytes, size_t length))
{
    char line[112];
    uint8_t shown[SITE_SLOTS / 8u] = { 0 };
    int n = snprintf(line, sizeof line, "heap profile: live=%lu peak=%lu unrecorded=%lu\n",
                     (unsigned long)live_total, (unsigned long)peak_total,
                     (unsigned long)unrecorded);

    print(line, (size_t)n);
    for (uint32_t k = 0; k < REPORT_SITES; k++) {
        uint32_t best = SITE_SLOTS;

        for (uint32_t i = 0; i < SITE_SLOTS; i++) {
            if (sites[i].pc == 0 || (shown[i / 8u] & (1u << (i % 8u)))) continue;
            if (best == SITE_SLOTS || sites[i].peak > sites[best].peak) best = i;
        }
        if (best == SITE_SLOTS) break;
        shown[best / 8u] |= (uint8_t)(1u << (best % 8u));
        n = snprintf(line, sizeof line, "site 0x%08lx live=%lu peak=%lu blocks=%lu\n",
                     (unsigned long)sites[best].pc, (unsigned long)sites[best].live,
                     (unsigned long)sites[best].peak, (unsigned long)sites[best].blocks);
        print(line, (size_t)n);
    }
}
