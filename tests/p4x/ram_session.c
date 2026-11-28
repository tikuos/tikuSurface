/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * ram_session.c - Run and capture the native desktop with volatile board storage.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tiku.h>
#include "runtime.h"
#include "display.h"
#include "boot.h"
#include "bridge_os.h"
#include "settings_io.h"
#include "tiku_trk_splash.h"
#include <kernel/fs/tiku_tfs.h>
#include <kernel/vfs/tree/tiku_vfs_tree_random.h>
#include <arch/esp32p4/tiku_trng_arch.h>
#include <arch/esp32p4/tiku_boot_arch.h>
#include <arch/esp32p4/tiku_systimer_arch.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/process/tiku_process.h>
#include <kernel/vfs/tree/tiku_vfs_tree_watch.h>
#include <kernel/vfs/tree/tiku_vfs_tree_data.h>
#include <kernel/scheduler/tiku_sched.h>
#include <kernel/threads/tiku_thread.h>
#include <kernel/memory/tiku_mem.h>
#include <services/console/tiku_usb_serial_jtag.h>
#include <arch/esp32p4/tiku_psram_arch.h>
#include <hal/tiku_cpu.h>
#include <tikukits/crypto/sha256/tiku_kits_crypto_sha256.h>
#include <string.h>
#include <stdio.h>
#include <services/audio/tiku_mic_vfs.h>
#include <services/audio/tiku_mic.h>

extern uint8_t __assets_start[], __assets_end[], __surface_psram_bss_start[], __surface_psram_bss_end[];
extern const uint8_t __payload_digest[];
extern struct tiku_process surface_process;

#ifdef P4X_FLASH_BOOT
static const int standalone = 1;
#else
static int standalone;
#endif

static void write_usb(const void *bytes, size_t n)
{
    const uint8_t *p = bytes;
    while (n) {
        size_t sent = tiku_usb_serial_jtag_write(p, n);
        p += sent;
        n -= sent;
        tiku_usb_serial_jtag_poll();
    }
}
void tiku_console_write(const void *bytes, size_t n)
{
    if (!standalone) write_usb(bytes, n);
}
static void message(const char *s)
{
#ifdef P4X_FLASH_BOOT
    (void)tiku_usb_serial_jtag_write((const uint8_t *)s, strlen(s));
    tiku_usb_serial_jtag_poll();
#else
    write_usb(s, strlen(s));
#endif
}
static int receive_assets(void)
{
    size_t n = (uintptr_t)__assets_end - (uintptr_t)__assets_start;
    if ((uintptr_t)__assets_start != 0x49800000u || !n || n > 4u * 1024u * 1024u) return 0;
    uint8_t digest[32];
#ifdef P4X_FLASH_BOOT
    if (tiku_p4x_boot_read(0x50000u, __assets_start, n)) return 0;
#else
    message("P4X SEGMENT READY\r\n");
    uint64_t start = surface_os_time();
    for (size_t i = 0; i < n;) {
        tiku_usb_serial_jtag_poll();
        int ch = tiku_usb_serial_jtag_getc();
        if (ch < 0) {
            if (surface_os_time() - start > 30000000u) return 0;
            continue;
        }
        __assets_start[i] = (uint8_t)ch;
        ++i;
        if (!(i % 512u)) message("+");
    }
#endif
    tiku_cpu_dcache_clean(__assets_start, n);
    if (tiku_kits_crypto_sha256_hash(__assets_start, n, digest) ||
        memcmp(digest, __payload_digest, sizeof digest)) return 0;
    tiku_cpu_icache_invalidate();
    return 1;
}

/* The file store is a separate span of volatile PSRAM after the desktop image. */
static tiku_tfs_t files;
static int ram_write(tiku_nvm_backend_t *be, size_t offset, const void *data, size_t n)
{
    if (offset > be->size || n > be->size - offset) return -1;
    memmove(be->base + offset, data, n);
    return 0;
}
static tiku_nvm_backend_t ram_files = {
    .base = (uint8_t *)0x49d00000u, .size = 3u * 1024u * 1024u, .write = ram_write
};
tiku_tfs_t *tiku_vfs_tree_data_store(void) { return &files; }
int surface_os_settings_read(unsigned char *out, size_t cap, size_t *n)
{
    int r = tiku_tfs_read(&files, "tikusurface-cal-v1", out, cap, n);
    return r == TFS_ERR_NOTFOUND ? 1 : r;
}
int surface_os_settings_write(const unsigned char *data, size_t n)
{
    return tiku_tfs_write(&files, "tikusurface-cal-v1", data, n);
}
/* Sample the leaf menu state on the desktop worker when it paints the Deskbar. */
struct tiku_trk_deskbar;
extern int tiku_trk_deskbar_menu_open(const struct tiku_trk_deskbar *bar);
extern int tiku_trk_deskbar_menu_point(const struct tiku_trk_deskbar *bar,
                                       const char *label, int *x, int *y);
static unsigned settings_row, touch_row;

static unsigned row_point(const struct tiku_trk_deskbar *bar, const char *label)
{
    int x, y;
    if (!tiku_trk_deskbar_menu_point(bar, label, &x, &y)) return 0;
    return (unsigned)x << 16 | (unsigned)y;
}

void __real_tiku_trk_deskbar_draw(struct tiku_trk_deskbar *bar, tiku_surface_t *surface);
static volatile int drawn_menu_open;
static unsigned audio_row, recorder_row;
void __wrap_tiku_trk_deskbar_draw(struct tiku_trk_deskbar *bar, tiku_surface_t *surface)
{
    __real_tiku_trk_deskbar_draw(bar, surface);
    __atomic_store_n(&settings_row, row_point(bar, "Settings"), __ATOMIC_RELEASE);
    __atomic_store_n(&touch_row, row_point(bar, "Touch"), __ATOMIC_RELEASE);
    __atomic_store_n(&audio_row, row_point(bar, "Audio"), __ATOMIC_RELEASE);
    __atomic_store_n(&recorder_row, row_point(bar, "Sound Recorder"), __ATOMIC_RELEASE);
    drawn_menu_open = tiku_trk_deskbar_menu_open(bar);
}

static volatile int boot_finished;
void __real_tiku_trk_splash_done(tiku_trk_splash_t *splash, int64_t now);
void __wrap_tiku_trk_splash_done(tiku_trk_splash_t *splash, int64_t now)
{
    __real_tiku_trk_splash_done(splash, now);
    boot_finished = 1;
}
int __real_surface_desktop_main(int argc, char **argv);
int __wrap_surface_desktop_main(int argc, char **argv)
{
#ifdef P4X_FLASH_BOOT
    return __real_surface_desktop_main(argc, argv);
#else
    char *folder[] = { argv[0], "/devices/board/sys/clock", NULL };
    return __real_surface_desktop_main(2, folder);
#endif
}

static const tiku_vfs_node_t random_nodes[] = {
    TIKU_VFS_RANDOM_NODE("random"), TIKU_VFS_RANDOM_NODE("urandom")
};
static const tiku_vfs_node_t dev_node = {
    .name = "dev", .type = TIKU_VFS_DIR, .children = random_nodes, .child_count = 2
};
static int uptime_read(char *buf, size_t cap)
{
    return snprintf(buf, cap, "%lu\n", (unsigned long)tiku_clock_seconds());
}
static int mcu_read(char *buf, size_t cap)
{
    return snprintf(buf, cap, "%s\n", TIKU_DEVICE_NAME);
}
static int freq_read(char *buf, size_t cap)
{
    return snprintf(buf, cap, "%lu\n", tiku_cpu_mclk_hz());
}
static int version_read(char *buf, size_t cap)
{
    return snprintf(buf, cap, "%s\n", TIKU_VERSION);
}
static const tiku_vfs_node_t mcu = {
    .name = "mcu", .type = TIKU_VFS_FILE, .read = mcu_read
};
static const tiku_vfs_node_t freq = {
    .name = "freq", .type = TIKU_VFS_FILE, .read = freq_read
};
static const tiku_vfs_node_t sys_nodes[] = {
    {.name = "vfs", .type = TIKU_VFS_DIR, .children = tiku_vfs_tree_vfs_children,
     .child_count = TIKU_VFS_TREE_VFS_NCHILD},
    {.name = "uptime", .type = TIKU_VFS_FILE, .read = uptime_read},
    {.name = "device", .type = TIKU_VFS_DIR, .children = &mcu, .child_count = 1},
    {.name = "cpu", .type = TIKU_VFS_DIR, .children = &freq, .child_count = 1},
    {.name = "version", .type = TIKU_VFS_FILE, .read = version_read}
};
/** @brief Measure CPU0 cycles against a 10 ms interval on the crystal-derived timer. */
static unsigned measured_cpu_khz(void)
{
    uint64_t start, now;
    uint32_t before, after;
    if (tiku_p4_systimer_read(&start)) return 0;
    __asm__ volatile("rdcycle %0" : "=r"(before));
    for (unsigned i = 0; i < 100000u; ++i) {
        if (tiku_p4_systimer_read(&now)) return 0;
        uint32_t ticks = (uint32_t)(now - start);
        if (ticks >= 160000u) {
            __asm__ volatile("rdcycle %0" : "=r"(after));
            return (uint64_t)(after - before) * 16000u / ticks;
        }
    }
    return 0;
}

int __wrap_tiku_p4x_panel_pattern(void)
{
    unsigned khz = measured_cpu_khz();
    char report[64];
    snprintf(report, sizeof report, "CPU0 measured %u kHz\r\n", khz);
    message(report);
    if (khz < MAIN_CPU_FREQ * 995u || khz > MAIN_CPU_FREQ * 1005u) {
        message("FAIL CPU0 frequency\r\n");
        return -4;
    }
    if (tiku_p4_psram_size() != 32u * 1024u * 1024u || !receive_assets()) {
        message("FAIL segment\r\n"); return -4;
    }
    message("PASS linked segment\r\n");
    memset(__surface_psram_bss_start, 0, (uintptr_t)__surface_psram_bss_end - (uintptr_t)__surface_psram_bss_start);
    uint8_t *base = (uint8_t *)(TIKU_P4_PSRAM_BASE + 0x10000u);
    int tier = tiku_tier_init();
    int attach = tier ? -99 : tiku_tier_attach_psram(base, (uintptr_t)__assets_start - (uintptr_t)base);
    int fs = tiku_tfs_init(&files, &ram_files);
    int mount = 0;
    for (size_t i = 0; i < sizeof sys_nodes / sizeof sys_nodes[0] && !mount; ++i)
        mount = tiku_vfs_mount("/sys", &sys_nodes[i]);
    tiku_trng_arch_init();
    int dev = tiku_vfs_mount("/", &dev_node);
    if (!dev) dev = tiku_vfs_mount("/dev", tiku_mic_vfs_dev());
    if (!mount) mount = tiku_vfs_mount("/sys", tiku_mic_vfs_sys());
    if (tier || attach || fs || mount || dev) {
        char why[112];
        snprintf(why, sizeof why, "FAIL RAM services tier=%d attach=%d fs=%d sys=%d dev=%d\r\n",
                 tier, attach, fs, mount, dev);
        message(why); return -4;
    }
    message("PASS RAM services\r\n");
    if (!surface_runtime_start()) { message("FAIL native startup\r\n"); return -4; }
    tiku_process_start(&surface_process, NULL);
    tiku_process_poll(&surface_process);
    message("PASS native startup\r\n");
    uint64_t start = surface_os_time();
    unsigned frames = 0;
    uint64_t boot_time = 0;
    unsigned splash_frames = 0;
    uint8_t coords[4];
    unsigned received = sizeof coords;
    uint64_t tap_deadline = 0;
    for (;;) {
        tiku_sched_run_once();
        if (tiku_thread_worker_ready()) tiku_thread_kernel_block();
        surface_frame_stats_t stats;
        if (surface_runtime_frame_stats(&stats) && stats.completed != frames) {
            frames = stats.completed;
            if (frames == 1) message("PASS native desktop frame\r\n");
        }
        if (boot_finished && !boot_time) { boot_time = surface_os_time(); splash_frames = frames; }
        if (boot_time && frames > splash_frames && boot_finished == 1 && surface_os_time() - boot_time > 6000000u) {
            boot_finished = 2;
            message("PASS native desktop ready\r\n");
        }
        tiku_usb_serial_jtag_poll();
        int c = tiku_usb_serial_jtag_getc();
        if (received < sizeof coords && surface_os_time() >= tap_deadline) {
            received = sizeof coords;
            message("FAIL incomplete tap coordinates\r\n");
        }
        if (received < sizeof coords && c >= 0) {
            coords[received++] = (uint8_t)c;
            if (received == sizeof coords) {
                unsigned x = (unsigned)coords[0] << 8 | coords[1];
                unsigned y = (unsigned)coords[2] << 8 | coords[3];
                int posted = surface_runtime_inject('t', x, y);
                message(posted ? "Pointer tap queued\r\n" : "FAIL pointer tap\r\n");
            }
            continue;
        }
        if (c == 'v') {
#ifdef P4X_FLASH_BOOT
            /* One line: a second message would not fit the 64-byte FIFO. */
            snprintf(report, sizeof report, "TikuOS %s PSRAM %uMHz TikuSurface flash frames=%u ready=%d\r\n",
                     TIKU_VERSION, (unsigned)(tiku_p4_psram_hz() / 1000000u), frames, boot_finished == 2);
#else
            snprintf(report, sizeof report, "CPU0 %u kHz frames=%u standalone=%d\r\n",
                     measured_cpu_khz(), frames, standalone);
#endif
            message(report);
        }
        if (standalone) continue;
        if (c == 'A') {
            uint32_t jobs, failed, wakes, us;
            surface_os_display_convert_stats(&jobs, &failed, &wakes, &us);
            snprintf(report, sizeof report, "PPA %lu %lu %lu %lu\r\n",
                     (unsigned long)jobs, (unsigned long)failed,
                     (unsigned long)wakes, (unsigned long)us);
            message(report);
        }
#ifndef P4X_FLASH_BOOT
        if (c == 'r' && boot_finished == 2) {
            standalone = 1;
            message("Desktop running from RAM\r\n");
        }
#endif
        if (c == 'q' || surface_os_time() - start > 450000000u) break;
        if (c == 'T') {
            received = 0;
            tap_deadline = surface_os_time() + 1000000u;
        }
        if (c == 'W') {
            extern void p4x_check_recordings(void);
            p4x_check_recordings();
        }
        if (c == 'm') {
            tiku_mic_status_t mic;
            tiku_mic_status(&mic);
            char audio[100];
            snprintf(audio, sizeof audio, "MIC %u %u %lu %lu %lu %u\r\n",
                     mic.state, mic.starts, (unsigned long)mic.captured,
                     (unsigned long)mic.dropped, (unsigned long)mic.faults, mic.peak);
            message(audio);
        }
        if (c == 's' || c == 'a' || c == 'u' || c == 'R') {
            unsigned *row = c == 's' ? &settings_row : c == 'a' ? &touch_row :
                            c == 'u' ? &audio_row : &recorder_row;
            unsigned pos = __atomic_load_n(row, __ATOMIC_ACQUIRE);
            snprintf(report, sizeof report, "ROW %u %u\r\n", pos >> 16, pos & 0xffffu);
            message(report);
        }
        if (c == 'g') {
            char value;
            size_t n;
            int rc = tiku_tfs_read(&files, "ts/touch", &value, 1, &n);
            if (rc == TFS_ERR_NOTFOUND) message("MAGNIFIER unset\r\n");
            else if (rc || n != 1 || (value != '0' && value != '1')) message("FAIL touch preference\r\n");
            else message(value == '1' ? "MAGNIFIER on\r\n" : "MAGNIFIER off\r\n");
        }
        if (c == 'c') (void)surface_runtime_capture();
        if (c == '?') message(drawn_menu_open ? "MENU OPEN\r\n" : "MENU CLOSED\r\n");
        if (c == 'o' || c == 'p' || c == 'x') {
            unsigned x = 610, y = 510;
            if (c == 'o') { x = 940; y = 41; }
            if (c == 'p') { x = 928; y = 133; }
            int posted = surface_runtime_inject('t', x, y);
            message(posted ? "Pointer tap queued\r\n" : "FAIL pointer tap\r\n");
        }
    }
    static char touch_events[2048];
    message("GT911 touch history\r\n");
    if (surface_runtime_touch_log(touch_events, sizeof touch_events) > 0) message(touch_events);
    message("PASS native desktop session ended\r\n");
    for (;;) tiku_usb_serial_jtag_poll();
}
