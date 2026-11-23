/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * sys.c - the system facts the support kit asks of TikuOS.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sys.h"
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

void tiku_sys_memory(long *total_kb, long *avail_kb)
{
    size_t used = 0, total = 0;

    surface_os_desktop_heap_use(&used, &total, NULL, NULL);
    if (total_kb != NULL) *total_kb = (long)(total / 1024u);
    if (avail_kb != NULL) *avail_kb = (long)((total - used) / 1024u);
}
