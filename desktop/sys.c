/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * sys.c - the system facts the support kit asks of TikuOS.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sys.h"
#include "bridge_os.h"
#include <stdio.h>

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
