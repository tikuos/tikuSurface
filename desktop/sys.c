/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * sys.c - the system facts the support kit asks of TikuOS.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sys.h"
#include "bridge.h"
#include "bridge_os.h"
#include "tiku_posix.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief The first line of kernel node @p path, without its newline, in
 *  @p out.  @return its length, or -1 */
static int node_line(const char *path, char *out, size_t max)
{
    int fd = tiku_posix_open(path, O_RDONLY);
    ssize_t n;

    if (fd < 0) return -1;
    n = tiku_posix_read(fd, out, max - 1u);
    (void)tiku_posix_close(fd);
    if (n <= 0) return -1;
    out[n] = '\0';
    out[strcspn(out, "\r\n")] = '\0';
    return (int)strlen(out);
}

/** @brief The running kernel release shown in the desktop About window. */
void surface_about_system_release(char *out, size_t size)
{
    char version[48];

    if (!out || !size) return;
    out[0] = '\0';
    if (node_line("/sys/version", version, sizeof version) > 0)
        snprintf(out, size, "TikuOS %s", version);
}

/* The kernel's part name and its CPU rate in force, which `freq` changes
 * while the desktop runs. */
int tiku_sys_processor(char *name, size_t max, unsigned long *hz)
{
    char rate[24];

    *hz = node_line("/sys/cpu/freq", rate, sizeof rate) > 0 ? strtoul(rate, NULL, 10) : 0UL;
    if (max == 0u || node_line("/sys/device/mcu", name, max) <= 0) {
        if (max != 0u) name[0] = '\0';
        return -1;
    }
    return 0;
}

void tiku_sys_frame_name(const char *raw, char *out, size_t max)
{
    if (out == NULL || max == 0u) return;
    snprintf(out, max, "%s", raw != NULL ? raw : "");
}

/* The detected PSRAM and the bytes available in its kernel tier and desktop
 * heap. A runtime without a PSRAM extent reports its allocator's limits. */
void tiku_sys_memory(long *total_kb, long *avail_kb)
{
    surface_desktop_memory_t memory;
    size_t total = 0, live = surface_os_desktop_heap_live();

    surface_os_desktop_heap_use(NULL, &total, NULL, NULL);
    size_t available = total > live ? total - live : 0u;
    surface_desktop_memory(&memory);
    if (memory.psram) {
        total = memory.psram;
        available += memory.spare;
    }
    if (total_kb != NULL) *total_kb = (long)(total / 1024u);
    if (avail_kb != NULL) *avail_kb = (long)(available / 1024u);
}

/** @brief Kernel node @p path whole in @p out.  @return its length, or 0 */
static size_t node_text(const char *path, char *out, size_t max)
{
    int fd = tiku_posix_open(path, O_RDONLY);
    size_t at = 0;
    ssize_t n;

    if (fd < 0) return 0;
    while (at + 1u < max && (n = tiku_posix_read(fd, out + at, max - 1u - at)) > 0) at += (size_t)n;
    (void)tiku_posix_close(fd);
    out[at] = '\0';
    return at;
}

/** @brief Append @p text to @p out at @p *at, each line after @p tag and a
 *  tab; a line that does not fit is left out. */
static void tagged(char *out, size_t max, size_t *at, const char *tag, const char *text)
{
    while (*text != '\0') {
        size_t len = strcspn(text, "\n");
        int n = snprintf(out + *at, max - *at, "%s\t%.*s\n", tag, (int)len, text);

        if (len > 0u && n > 0 && (size_t)n < max - *at) *at += (size_t)n;
        else out[*at] = '\0';
        text += len + (text[len] == '\n');
    }
}

/* The kernel SRAM and flash maps, with the PSRAM reservations in address order. */
int tiku_sys_memory_map(char *out, size_t max)
{
    char text[768];
    size_t at = 0;
    surface_desktop_memory_t m;

    if (max == 0u) return 0;
    out[0] = '\0';
    if (node_text("/sys/mem/sram_map", text, sizeof text) > 0) tagged(out, max, &at, "sram", text);
    surface_desktop_memory(&m);
    if (m.psram > 0u) {
        size_t live = surface_os_desktop_heap_live(), spare = m.spare;
        size_t above = m.below <= m.psram && m.tier <= m.psram - m.below
            ? m.psram - m.below - m.tier : 0;
        size_t store = m.top_store <= above ? m.top_store : 0;
        char psram[512];

        snprintf(psram, sizeof psram,
                 "total\t%lu\n%s\t%lu\nfree\t%lu\ndrivers\t%lu\n"
                 "stack\t%lu\t%lu\njobs\t%lu\t%lu\nsettings\t%lu\nheap\t%lu\t%lu\n"
                 "frames\t%lu\n%s\t%lu\n",
                 (unsigned long)m.psram, store ? "unassigned" : "shadow", (unsigned long)m.below, (unsigned long)spare,
                 (unsigned long)(m.tier > spare + m.span ? m.tier - spare - m.span : 0u),
                 (unsigned long)m.stack, (unsigned long)(m.stack - m.stack_used),
                 (unsigned long)m.jobs, (unsigned long)(m.jobs - m.jobs_used),
                 (unsigned long)m.stages, (unsigned long)m.heap, (unsigned long)live,
                 (unsigned long)m.frames,
                 store ? "Image space" : "static", (unsigned long)(above - store));
        tagged(out, max, &at, "psram", psram);
        if (store) {
            snprintf(psram, sizeof psram, "store\t%lu\n", (unsigned long)store);
            tagged(out, max, &at, "psram", psram);
        }
    }
    if (node_text("/sys/mem/nvm_map", text, sizeof text) > 0) tagged(out, max, &at, "nvm", text);
    return (int)at;
}
