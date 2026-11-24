/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_sys.c - desktop/sys.c over fake kernel nodes: free memory from the
 * allocator's count (tikuSurface#10) and this computer's memory maps.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sys.h"
#include "bridge.h"
#include "tiku_posix.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const struct { const char *path, *text; } nodes[] = {
    { "/sys/mem/sram_map", "bank\t393216\nstatic\t196224\ntier\t180608\t0\t1\nguard\t0\n"
                           "unassigned\t0\nstack\t16384\t11812\n" },
    { "/sys/mem/nvm_map", "kind\tFlash\ntotal\t16777216\ntier\t32768\t0\nstore\t1736704\t49152\n" },
};
static int open_fd = -1;
static size_t read_at;
static size_t psram_size = 16777216u;

int tiku_posix_open(const char *path, int flags)
{
    (void)flags;
    for (int i = 0; i < (int)(sizeof nodes / sizeof nodes[0]); i++)
        if (!strcmp(nodes[i].path, path)) { open_fd = i; read_at = 0; return 3 + i; }
    return -1;
}

/* At most 40 bytes a read, so a node takes several. */
ssize_t tiku_posix_read(int fd, void *buf, size_t n)
{
    const char *text = nodes[fd - 3].text;
    size_t left = strlen(text) - read_at;

    if (n > 40u) n = 40u;
    if (n > left) n = left;
    memcpy(buf, text + read_at, n);
    read_at += n;
    return (ssize_t)n;
}

int tiku_posix_close(int fd) { (void)fd; open_fd = -1; return 0; }

/* The heap: its break at 7 MB, 3.6 MB handed out of 8,507,392 bytes. */
void surface_os_desktop_heap_use(size_t *used, size_t *total, uint32_t *refused, size_t *largest)
{
    if (used) *used = 7000000u;
    if (total) *total = 8507392u;
    if (refused) *refused = 0;
    if (largest) *largest = 0;
}

size_t surface_os_desktop_heap_live(void) { return 3600000u; }

void surface_desktop_memory(surface_desktop_memory_t *m)
{
    *m = (surface_desktop_memory_t){ psram_size, 1802240u, 14319616u, 121264u, 13369344u, 1536000u,
                                     2883584u, 2716420u, 393216u, 17188u, 49152u, 8507392u };
}

int main(void)
{
    long total = 0, avail = 0;
    char map[2048], small[120];
    int n;

    /* Free is the heap less what the allocator has handed out, not less
     * its break. */
    tiku_sys_memory(&total, &avail);
    assert(total == 8507392L / 1024 && avail == (8507392L - 3600000L) / 1024);

    n = tiku_sys_memory_map(map, sizeof map);
    assert(n == (int)strlen(map) && open_fd == -1);
    assert(strstr(map, "sram\tbank\t393216\nsram\tstatic\t196224\nsram\ttier\t180608\t0\t1\n"));
    assert(strstr(map, "sram\tstack\t16384\t11812\npsram\ttotal\t16777216\n"));
    /* PSRAM in address order: shadow, the tier's free bytes, the drivers'
     * buffers (the tier less its free bytes and the span), the span's parts
     * with the stacks' headroom and the heap in use, then the statics. */
    assert(strstr(map, "psram\tshadow\t1802240\npsram\tfree\t121264\npsram\tdrivers\t829008\n"
                       "psram\tframes\t1536000\npsram\tstack\t2883584\t167164\n"
                       "psram\tjobs\t393216\t376028\npsram\tsettings\t49152\n"
                       "psram\theap\t8507392\t3600000\npsram\tstatic\t655360\n"
                       "nvm\tkind\tFlash\n"));
    assert(strstr(map, "nvm\tstore\t1736704\t49152\n") && map[n - 1] == '\n');

    /* A short buffer keeps whole lines only. */
    n = tiku_sys_memory_map(small, sizeof small);
    assert(n > 0 && n < (int)sizeof small && small[n - 1] == '\n' && n == (int)strlen(small));
    assert(!strncmp(small, "sram\tbank\t393216\n", 17));

    /* Without the PSRAM's extent, no PSRAM map. */
    psram_size = 0;
    n = tiku_sys_memory_map(map, sizeof map);
    assert(n > 0 && !strstr(map, "psram") && strstr(map, "nvm\ttotal\t16777216\n"));
    puts("PASS sys: free memory from the allocator's count; SRAM, PSRAM and flash maps");
    return 0;
}
