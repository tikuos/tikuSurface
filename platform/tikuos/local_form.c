/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * local_form.c - writes to the C program's registered form.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../desktop/board.h"
#include <compat/posix/tiku_posix.h>
#include <kernel/threads/tiku_thread.h>
#include <kernel/vfs/tiku_vfs.h>
#include "tiku_gui.h"
#include <string.h>

extern struct tiku_process gui_demo_process;

static int row_access(void *arg)
{
    const char *path = arg;
    if (!path || strncmp(path, "/gui/", 5) || !tiku_process_is_running(&gui_demo_process))
        return -1;
    return tiku_gui_row_access(&gui_demo_process, path + 5);
}

int surface_os_form_access(const char *path)
{
    return tiku_thread_call(row_access, (void *)path);
}

struct form_write {
    const char *path;
    const void *value;
    size_t length;
};

static int write_row(void *arg)
{
    const struct form_write *w = arg;
    if (row_access((void *)w->path) != 1) return -1;
    tiku_vfs_cap_t caller = tiku_vfs_caller_cap_set(tiku_posix_authority());
    int rc = tiku_vfs_write(w->path, w->value, w->length);
    (void)tiku_vfs_caller_cap_set(caller);
    return rc == 0 ? 0 : -1;
}

int surface_os_form_write(const char *path, const void *buf, size_t len)
{
    struct form_write w = { path, buf, len };
    if (!path || strncmp(path, "/gui/demo/", 10) != 0 ||
        strchr(path + 10, '/') || !buf || len > 512) return -1;
    return tiku_thread_call(write_row, &w);
}
