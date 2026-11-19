/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_diagnostics.c - product control exercised through the production VFS.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "runtime.h"
#include "boot.h"
#include <kernel/vfs/tiku_vfs.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static _Alignas(64) unsigned char region[SURFACE_RUNTIME_BYTES];
static int reserved, owner = 1, available = 1;
static uint64_t now;
int surface_host_allocating;

void *surface_os_reserve(size_t bytes)
{ assert(!reserved && bytes == sizeof region); reserved = 1; return region; }
int surface_os_release(void *base)
{ assert(reserved && base == region); reserved = 0; return 1; }
uint64_t surface_os_time(void)
{ now += 100; return now; }
int surface_os_in_owner(void)
{ return owner; }
int surface_os_free_bytes(uint32_t *bytes)
{ *bytes = 16000000u - (reserved ? sizeof region : 0); return available; }
int tiku_application_start(void)
{ return surface_runtime_start(); }
void tiku_application_stop(void)
{ surface_runtime_stop(); }

/** @brief Submit a bounded text command through capability enforcement and dispatch. */
static int command(const char *text)
{ return tiku_vfs_write("/surface/control", text, strlen(text)); }
/** @brief Read status without modifying runtime progress. */
static void snapshot(char *text, size_t capacity)
{ assert(tiku_vfs_read("/surface/status", text, capacity) > 0); }
/** @brief Reject malformed, restricted and wrong-owner operations without side effects. */
static void refusals(void)
{
    char before[512], after[512];
    snapshot(before, sizeof before);
    tiku_vfs_caller_cap_set(TIKU_VFS_CAP_NONE);
    assert(command("start") == TIKU_VFS_EPERM);
    tiku_vfs_caller_cap_set(TIKU_VFS_CAP_SYS);
    owner = 0;
    assert(command("start") == TIKU_VFS_EACCES);
    assert(tiku_vfs_read("/surface/status", after, sizeof after) == TIKU_VFS_EACCES);
    owner = 1;
    const char *invalid[] = {"", "start ", "stop\n", "launch=", "launch=4", "launch=-1",
        "launch=0x1", "launch=4294967296", "fail=33", "fail=+1", "fail=1x",
        "fail=999999999999999999999", "unknown"};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
        assert(command(invalid[i]) == TIKU_VFS_EINVAL);
    const char embedded[] = {'s','t','a','r','t',0,'x'};
    assert(tiku_vfs_write("/surface/control", embedded, sizeof embedded) == TIKU_VFS_EINVAL);
    snapshot(after, sizeof after);
    assert(!strcmp(before, after) && !reserved);
    const char *calibration_bad[] = {"", "0,0,0,479,0,0,0", "0,799,480,479,0,0,0", "0,799,0,479,2,0,0",
        "0,65536,0,479,0,0,0", "0,799,0,479,0,0", "0,799,0,479,0,0,0,", "-1,799,0,479,0,0,0"};
    for (unsigned i = 0; i < sizeof calibration_bad / sizeof calibration_bad[0]; ++i)
        assert(tiku_vfs_write("/surface/calibration", calibration_bad[i], strlen(calibration_bad[i])) == TIKU_VFS_EINVAL);
    const char *valid = "0,799,0,479,0,0,0";
    assert(tiku_vfs_write("/surface/calibration", valid, strlen(valid)) == TIKU_VFS_EIO);
    tiku_vfs_caller_cap_set(TIKU_VFS_CAP_NONE);
    assert(tiku_vfs_write("/surface/calibration", valid, strlen(valid)) == TIKU_VFS_EPERM);
    tiku_vfs_caller_cap_set(TIKU_VFS_CAP_SYS);
    available = 0;
    assert(tiku_vfs_read("/surface/psram_free", after, sizeof after) == TIKU_VFS_EIO);
    available = 1;
}
/** @brief Observe deferred stop, failure injection and restored PSRAM through actual paths. */
static void lifecycle(void)
{
    char text[512], tiny[4];
    for (unsigned i = 1; i <= 5; ++i) {
        char failure[] = "fail=0";
        failure[5] = (char)('0' + i);
        assert(!command(failure));
        assert(command("start") == TIKU_VFS_EIO);
        assert(!reserved);
    }
    assert(!command("fail=0"));
    const char start[] = {'s','t','a','r','t'};
    assert(!tiku_vfs_write("/surface/control", start, sizeof start));
    assert(reserved && surface_runtime_poll());
    snapshot(text, sizeof text);
    assert(strstr(text, "frames=0"));
    for (unsigned turn = 1; turn < 480 && !strstr(text, "frames=1"); ++turn) {
        assert(surface_runtime_poll());
        snapshot(text, sizeof text);
    }
    assert(strstr(text, "running=1 stopping=0") && strstr(text, "frames=1"));
    unsigned long total;
    assert(tiku_vfs_read_total("/surface/status", tiny, sizeof tiny, &total) == 3);
    assert(total == strlen(text) && !memcmp(tiny, "run", 4));
    assert(command("start") == TIKU_VFS_EIO);
    assert(!command("launch=1"));
    assert(surface_runtime_poll());
    assert(!command("launch=2"));
    assert(surface_runtime_poll());
    assert(surface_runtime_poll());
    snapshot(text, sizeof text);
    assert(strstr(text, "app=2 media_unavailable=1"));
    assert(!command("stop") && reserved);
    snapshot(text, sizeof text);
    assert(strstr(text, "running=1 stopping=1"));
    assert(command("launch=2") == TIKU_VFS_EIO);
    assert(!surface_runtime_poll() && !reserved);
    assert(!command("stop"));
    snapshot(text, sizeof text);
    assert(strstr(text, "running=0 stopping=0") && strstr(text, "live_bytes=0"));
    assert(tiku_vfs_read("/surface/psram_free", text, sizeof text) > 0);
    assert(!strcmp(text, "16000000\n"));
}
/** @brief Mount and remount the static subtree through the production resolver. */
int main(void)
{
    static const tiku_vfs_node_t root = {.name = "", .type = TIKU_VFS_DIR};
    for (unsigned i = 0; i < 2; ++i) {
        tiku_vfs_init(&root);
        assert(!tiku_vfs_mount("/", tiku_application_vfs_get()));
        refusals();
        lifecycle();
    }
    puts("Product VFS: capabilities, context, exact commands, failure and deferred stop passed");
    return 0;
}
