/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * diagnostics.c - serialized runtime control and observation through the VFS.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "boot.h"
#include "runtime.h"
#include <kernel/vfs/tiku_vfs.h>
#include <stdio.h>
#include <string.h>

/** @brief Copy current counters without dispatching or completing runtime work. */
static int status_read(char *buffer, size_t capacity)
{
    if (!surface_os_in_owner()) return TIKU_VFS_EACCES;
    surface_runtime_stats_t s;
    surface_runtime_stats(&s);
    return snprintf(buffer, capacity,
        "running=%lu stopping=%lu turns=%lu frames=%lu allocations=%lu "
        "live_bytes=%lu peak_bytes=%lu last_us=%lu max_us=%lu "
        "overruns=%lu failures=%lu app=%lu media_unavailable=%lu\n",
        (unsigned long)s.running, (unsigned long)s.stopping,
        (unsigned long)s.turns, (unsigned long)s.frames,
        (unsigned long)s.allocations, (unsigned long)s.live_bytes,
        (unsigned long)s.peak_bytes, (unsigned long)s.last_dispatch_us,
        (unsigned long)s.max_dispatch_us, (unsigned long)s.overruns,
        (unsigned long)s.allocation_failures, (unsigned long)s.foreground,
        (unsigned long)s.media_unavailable);
}

/** @brief Read currently unreserved PSRAM bytes from the firmware tier. */
static int free_read(char *buffer, size_t capacity)
{
    uint32_t bytes;
    if (!surface_os_in_owner()) return TIKU_VFS_EACCES;
    if (!surface_os_free_bytes(&bytes)) return TIKU_VFS_EIO;
    return snprintf(buffer, capacity, "%lu\n", (unsigned long)bytes);
}

/** @brief Report the selected display and completed handoffs without advancing it. */
static int display_read(char *buffer, size_t capacity)
{
    if (!surface_os_in_owner()) return TIKU_VFS_EACCES;
    surface_runtime_stats_t s;
    surface_runtime_stats(&s);
    return snprintf(buffer, capacity, "enabled=%lu completed=%lu errors=%lu\n",
        (unsigned long)SURFACE_LCD_ENABLE, (unsigned long)s.completed, (unsigned long)s.display_errors);
}

/** @brief Copy completed frame timing and identity without advancing rendering. */
static int timing_read(char *buffer, size_t capacity)
{
    surface_frame_stats_t s;
    if (!surface_os_in_owner()) return TIKU_VFS_EACCES;
    if (!surface_runtime_frame_stats(&s)) return TIKU_VFS_EIO;
    return snprintf(buffer, capacity,
        "frame_us=%lu max_frame_us=%lu frame_app=%lu frame_count=%lu pending=%lu "
        "request_us=%lu max_request_us=%lu requested=%lu presented_request=%lu\n",
        (unsigned long)s.frame_us, (unsigned long)s.max_frame_us,
        (unsigned long)s.foreground, (unsigned long)s.completed, (unsigned long)s.pending,
        (unsigned long)s.request_us, (unsigned long)s.max_request_us,
        (unsigned long)s.requested, (unsigned long)s.presented_request);
}

/** @brief Accept a decimal suffix up to limit, with no sign, spaces or trailing data. */
static int number(const char *text, size_t length, unsigned limit, unsigned *value)
{
    unsigned result = 0;
    if (!length) return 0;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') return 0;
        unsigned digit = (unsigned)(text[i] - '0');
        if (digit > limit || result > (limit - digit) / 10u) return 0;
        result = result * 10u + digit;
    }
    *value = result;
    return 1;
}

/** @brief Copy raw contact observations without polling the touch controller. */
static int input_read(char *buffer, size_t capacity)
{
    surface_input_stats_t s;
    if (!surface_os_in_owner()) return TIKU_VFS_EACCES;
    if (!surface_runtime_input_stats(&s)) return TIKU_VFS_EIO;
    return snprintf(buffer, capacity, "reports=%lu errors=%lu contacts=%lu id=%lu x=%lu y=%lu"
        " downs=%lu ups=%lu quick=%lu gap_ms=%lu\n",
        (unsigned long)s.reports, (unsigned long)s.errors, (unsigned long)s.contacts,
        (unsigned long)s.id, (unsigned long)s.x, (unsigned long)s.y,
        (unsigned long)s.downs, (unsigned long)s.ups, (unsigned long)s.quick,
        (unsigned long)s.gap_ms);
}
/** @brief Copy the touch path's last pointer events without polling the controller. */
static int touchlog_read(char *buffer, size_t capacity)
{
    if (!surface_os_in_owner()) return TIKU_VFS_EACCES;
    return surface_runtime_touch_log(buffer, capacity);
}
/** @brief Copy active calibration and its persistence state without storage I/O. */
static int calibration_read(char *buffer, size_t capacity)
{
    surface_touch_map_t m; unsigned state;
    if (!surface_os_in_owner()) return TIKU_VFS_EACCES;
    if (!surface_runtime_calibration(&m, &state)) return TIKU_VFS_EIO;
    return snprintf(buffer, capacity, "state=%u x_min=%u x_max=%u y_min=%u y_max=%u swap=%u invert_x=%u invert_y=%u\n",
        state, (unsigned)m.x_min, (unsigned)m.x_max, (unsigned)m.y_min, (unsigned)m.y_max,
        (unsigned)m.swap, (unsigned)m.invert_x, (unsigned)m.invert_y);
}
/** @brief Queue exactly seven comma-separated calibration fields for explicit persistence. */
static int calibration_write(const char *text, size_t length)
{
    if (!surface_os_in_owner()) return TIKU_VFS_EACCES;
    if (!text || !length || length > 29) return TIKU_VFS_EINVAL;
    unsigned values[7], at = 0;
    for (unsigned i = 0; i < 7; ++i) {
        unsigned start = at;
        while (at < length && text[at] != ',') ++at;
        if (!number(text + start, at - start, i < 4 ? 65535 : 1, &values[i])) return TIKU_VFS_EINVAL;
        if (i < 6) { if (at == length) return TIKU_VFS_EINVAL; ++at; }
        else if (at != length) return TIKU_VFS_EINVAL;
    }
    if (values[0] >= values[1] || values[2] >= values[3]) return TIKU_VFS_EINVAL;
    surface_touch_map_t m = {(uint16_t)values[0], (uint16_t)values[1], (uint16_t)values[2],
        (uint16_t)values[3], (uint8_t)values[4], (uint8_t)values[5], (uint8_t)values[6]};
    return surface_runtime_calibrate(&m) ? TIKU_VFS_OK : TIKU_VFS_EIO;
}

/** @brief Dispatch an exact command in the kernel owner; stop completion is deferred. */
static int control_write(const char *text, size_t length)
{
    unsigned value;
    if (!surface_os_in_owner()) return TIKU_VFS_EACCES;
    if (!text || !length || length > 64) return TIKU_VFS_EINVAL;
    if (length == 5 && !memcmp(text, "start", 5))
        return tiku_application_start() ? TIKU_VFS_OK : TIKU_VFS_EIO;
    if (length == 4 && !memcmp(text, "stop", 4)) {
        tiku_application_stop();
        return TIKU_VFS_OK;
    }
    if (length > 7 && !memcmp(text, "launch=", 7) &&
        number(text + 7, length - 7, 3, &value))
        return surface_runtime_launch(value) ? TIKU_VFS_OK : TIKU_VFS_EIO;
#if SURFACE_NATIVE_DESKTOP
    if (length == 7 && !memcmp(text, "capture", 7))
        return surface_runtime_capture() ? TIKU_VFS_OK : TIKU_VFS_EIO;
    if (length > 5 && !memcmp(text, "hash=", 5))
        return surface_runtime_hash(text + 5, length - 5) ? TIKU_VFS_OK : TIKU_VFS_EIO;
    if (length == 6 && !memcmp(text, "report", 6))
        return surface_runtime_report() ? TIKU_VFS_OK : TIKU_VFS_EIO;
    if (length == 6 && !memcmp(text, "stacks", 6))
        return surface_runtime_stacks() ? TIKU_VFS_OK : TIKU_VFS_EIO;
    if (length == 7 && !memcmp(text, "samples", 7))
        return surface_runtime_samples() ? TIKU_VFS_OK : TIKU_VFS_EIO;
    if (length == 4 && !memcmp(text, "heap", 4))
        return surface_runtime_heap_report() ? TIKU_VFS_OK : TIKU_VFS_EIO;
    /* finger=X,Y and lift: a finger placed and lifted through the touch path. */
    if (length == 4 && !memcmp(text, "lift", 4))
        return surface_runtime_inject('l', 0, 0) ? TIKU_VFS_OK : TIKU_VFS_EINVAL;
    /* down=X,Y move=X,Y up=X,Y tap=X,Y dtap=X,Y: pointer events as touch
     * posts them; dtap is two taps at once. */
    {
        const char *eq = memchr(text, '=', length), *comma;
        unsigned x, y;

        if (eq != NULL && (comma = memchr(eq, ',', length - (size_t)(eq - text))) != NULL &&
            number(eq + 1, (size_t)(comma - eq - 1), 65535u, &x) &&
            number(comma + 1, length - (size_t)(comma + 1 - text), 65535u, &y)) {
            size_t key = (size_t)(eq - text);

            if ((key == 4 && !memcmp(text, "down", 4)) || (key == 4 && !memcmp(text, "move", 4)) ||
                (key == 2 && !memcmp(text, "up", 2)) || (key == 3 && !memcmp(text, "tap", 3)))
                return surface_runtime_inject(text[0], x, y) ? TIKU_VFS_OK : TIKU_VFS_EINVAL;
            if (key == 4 && !memcmp(text, "dtap", 4))
                return surface_runtime_inject('D', x, y) ? TIKU_VFS_OK : TIKU_VFS_EINVAL;
            /* target=X,Y: the touch mark alone, as a point to aim a finger at. */
            if (key == 6 && !memcmp(text, "target", 6))
                return surface_runtime_inject('k', x, y) ? TIKU_VFS_OK : TIKU_VFS_EINVAL;
            if (key == 6 && !memcmp(text, "finger", 6))
                return surface_runtime_inject('f', x, y) ? TIKU_VFS_OK : TIKU_VFS_EINVAL;
        }
    }
    /* drag=X0,Y0,X1,Y1: down, eight moves along the line, up. */
    if (length > 5 && !memcmp(text, "drag=", 5)) {
        unsigned v[4];
        size_t at = 5;

        for (unsigned k = 0; k < 4; k++) {
            size_t end = at;

            while (end < length && text[end] != ',') end++;
            if (!number(text + at, end - at, 65535u, &v[k])) return TIKU_VFS_EINVAL;
            at = end + 1;
        }
        return surface_runtime_drag(v[0], v[1], v[2], v[3]) ? TIKU_VFS_OK : TIKU_VFS_EINVAL;
    }
#endif
    if (length > 5 && !memcmp(text, "fail=", 5) &&
        number(text + 5, length - 5, 32, &value)) {
        surface_runtime_fail_at(value);
        return TIKU_VFS_OK;
    }
    return TIKU_VFS_EINVAL;
}

static const tiku_vfs_node_t children[] = {
    {.name = "status", .type = TIKU_VFS_FILE, .read = status_read},
    {.name = "psram_free", .type = TIKU_VFS_FILE, .read = free_read},
    {.name = "touch", .type = TIKU_VFS_FILE, .read = input_read},
    {.name = "touchlog", .type = TIKU_VFS_FILE, .read = touchlog_read},
    {.name = "calibration", .type = TIKU_VFS_FILE, .read = calibration_read, .write = calibration_write, .req_cap = TIKU_VFS_CAP_SYS},
    {.name = "display", .type = TIKU_VFS_FILE, .read = display_read},
    {.name = "timing", .type = TIKU_VFS_FILE, .read = timing_read},
    {.name = "control", .type = TIKU_VFS_FILE, .write = control_write,
     .req_cap = TIKU_VFS_CAP_SYS}
};
static const tiku_vfs_node_t root = {
    .name = "surface", .type = TIKU_VFS_DIR,
    .children = children, .child_count = sizeof children / sizeof children[0]
};

const struct tiku_vfs_node *tiku_application_vfs_get(void)
{ return &root; }
