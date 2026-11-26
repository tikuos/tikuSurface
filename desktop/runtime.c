/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * runtime.c - the kernel side of the TikuDesktop shell on the S31 panel.
 *
 * Start reserves the two RGB565 frame buffers and opens the panel on them,
 * then reserves the rest of the runtime's PSRAM: the desktop and job
 * threads' stacks, the settings stages and the C-library heap; it mounts
 * the SD card without writing, seeds the desktop's random generator, opens
 * touch and starts the desktop thread.  Each owner turn
 * turns touch reports into pointer events for the desktop, converts and
 * submits a frame the desktop handed over for a bounded time, advances the
 * desktop's card request on the media bridge, and prints what the desktop
 * wrote to its standard output and error.  The desktop's surface is read
 * only while the desktop waits for the frame.  The control file's
 * diagnostics (capture, samples, stacks, heap, report, hash, synthetic
 * touch) are served here.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bridge.h"
#include "../platform/tikuos/runtime.h"
#include "../platform/tikuos/presentation.h"
#include "../platform/tikuos/display.h"
#include "../platform/tikuos/input.h"
#include "../platform/tikuos/settings_io.h"
#include "../platform/tikuos/camera.h"
#include "board.h"
#include "gesture.h"
#include "tiku_event.h"
#include "tiku_trk_deskbar.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define RGB_BYTES (SURFACE_DESKTOP_W * SURFACE_DESKTOP_H * 2u)
/* The desktop keeps its state, desktop_t, in a local variable of
 * tracker_run(), whose frame is 2,609,920 bytes on the S31 with
 * TikuSurface's bounds; the rest covers the call chains on the desktop
 * thread, where no other frame passes 83,120 bytes (template_fill()).  Jobs,
 * emptying the Trash among them, run on the job thread.  The deepest use
 * seen on the panel is 2,739,456 bytes.  The stack is painted with
 * STACK_PAINT at start; the status line reports the deepest use seen and
 * the kernel's stack canary faults. */
#define STACK_BYTES (2816u * 1024u)
#define STACK_PAINT 0xa5a5a5a5u
/* The stack of the desktop's job thread (TIKU_WORK_WORKERS in
 * desktop/desktop.mk), from compat/posix.  The deepest frame a job reaches
 * is emptying the Trash, fop_fs_empty_trash()'s 280,640 bytes, which leaves
 * 112,576 bytes of this stack for the rest of its chain.  The job stack uses desktop heap memory. The status
 * line reports its peak use from its stack paint. */
#define JOBS_BYTES (384u * 1024u)
/* Stages for the settings files the store group writes: three, so a save's
 * temporary, the session file's and a direct writer each have one. */
#define STAGES_BYTES (3u * 16384u)
#define STACK_SCAN_WORDS 4096u   /* words examined per turn for the high-water mark */
#define SAMPLE_SLOTS 4096u       /* program counters kept, one per kernel turn */
#define SAMPLE_LINE 48u          /* samples printed per turn by the dump */
#define STACK_SAMPLES 1024u      /* stack samples one "stacks" request takes */
#define STACK_DEPTH 16u          /* words per stack sample: pc, ra, then code addresses */
#define STACK_SCAN 4096u         /* stack words examined per stack sample */
#define PRESENT_TURN_US 3000u   /* presentation work per kernel turn: about three
                                 * 4096-pixel conversions, or many 1,600-byte
                                 * rows of the cache write-back */
#define TOUCH_EXPIRE_US 100000u
#ifndef SURFACE_TOUCH_MODE
/* 1: a mark under the finger from the first report, a slop before the
 * pointer moves and double taps landing on the first tap; 0: raw contacts. */
#define SURFACE_TOUCH_MODE 1
#endif
#define BURST_TURNS 32u         /* queued turns before the timer drives again */
#define BURST_US 20000u
#define STATUS_US 2000000u      /* console status line period */

int surface_desktop_main(int argc, char **argv);
void surface_desktop_write_stats(uint32_t *console, uint32_t *files, uint32_t *calls,
                                 const char **last);
int surface_desktop_files(void);
int surface_desktop_views_load(void);
int surface_desktop_views_save(void);
int surface_desktop_random_seed(void);
int tiku_posix_mutations(uint64_t *count);
void surface_desktop_selftest(void);

surface_desktop_bridge_t surface_desktop_bridge;

/* Whether the PSRAM statics section has been cleared.  It lives in SRAM
 * small data, which the C start-up clears; the section itself is not. */
static uint8_t statics_cleared;

/** @brief Clear the desktop's PSRAM statics once, before any of them is read. */
static void clear_statics(void)
{
    void *statics;
    size_t bytes;

    if (statics_cleared) return;
    surface_os_desktop_statics(&statics, &bytes);
    if (bytes) memset(statics, 0, bytes);
    statics_cleared = 1;
}

struct tiku_framebuffer { int unused; };

static struct {
    int running, failed;
    void *span;
    uint16_t *rgb[2];
    surface_presentation_t presentation;
    uint64_t sequence, presenting;
    struct tiku_framebuffer owner;
    unsigned input_owned;
    int input_open;
    surface_touch_map_t map;
    unsigned map_status;
    int touch_active, touch_x, touch_y;
    unsigned touch_id;
    surface_gesture_t gesture;
    int touch_held;           /* a finger placed by the control file: kept until lift */
    uint64_t last_report, frame_started;
    surface_input_stats_t input;
    surface_frame_stats_t frame;
    uint32_t turns, max_turn_us, last_turn_us, overruns;
    uint32_t burst;
    uint32_t *stack;          /* lowest word of the desktop thread's stack */
    uint32_t stack_scan;      /* next word the high-water scan examines */
    uint32_t stack_used;      /* deepest use found, in bytes */
    uint32_t *jobs;           /* lowest word of the job thread's stack */
    uint32_t jobs_scan;       /* next word its high-water scan examines */
    uint32_t jobs_used;       /* its deepest use found, in bytes */
    size_t psram, psram_below, psram_tier;  /* PSRAM, below the tier, the tier */
    size_t psram_spare;       /* the tier's bytes not reserved */
    uint64_t burst_start, last_status;
    int capturing;
    unsigned capture_row, capture_x;
    uint32_t sample_head;     /* samples taken, the ring's next slot modulo SAMPLE_SLOTS */
    uint32_t sample_idle;     /* turns that found the desktop thread blocked */
    uint32_t sample_dump;     /* next sample the dump prints, or UINT32_MAX */
    uint32_t sample_end;      /* sample_head when the dump was asked for */
    uint64_t phase_at;        /* when the presentation entered its current phase */
    unsigned phase_seen;      /* that phase: 1 convert, 2 submit, 3 wait */
    uint32_t phase_us[4];     /* the last frame's time in each phase */
    uint32_t region_px;       /* the last frame's converted pixels */
    uint32_t convert_cpu;     /* time inside conversion calls for this frame */
    uint32_t convert_cpu_last; /* the same for the last frame */
    uint64_t input_at;        /* when a down event was posted, until a frame
                               * the desktop drew after taking it is shown */
    uint32_t input_us;        /* that delay for the last down event */
    int stacks_on;            /* stack samples are being taken */
    uint32_t stacks_taken;    /* stack samples taken by this request */
    uint32_t stacks_dump;     /* next stack sample the dump prints, or UINT32_MAX */
} rt;

/* Each stack sample: the program counter, the return address, then words
 * from the interrupted stack that fall in a code range, nearest first.
 * Words that are not return addresses are filtered on the host. */
static uint32_t stack_samples[STACK_SAMPLES][STACK_DEPTH];

/** @brief Whether @p w lies in SRAM or flash code. */
static int code_word(uint32_t w)
{
    return (w >= 0x2f000000u && w < 0x2f040000u) || (w >= 0x40020000u && w < 0x40400000u);
}

/** @brief Record one stack sample of the preempted desktop thread. */
static void stack_sample(void)
{
    uintptr_t pc, ra;
    const uint32_t *sp, *top = rt.stack + STACK_BYTES / 4u;
    uint32_t *out = stack_samples[rt.stacks_taken];
    unsigned n = 2;

    if (!surface_os_desktop_context(&pc, &ra, &sp)) return;
    out[0] = (uint32_t)pc;
    out[1] = (uint32_t)ra;
    if (sp >= rt.stack && sp < top) {
        const uint32_t *end = top - sp > STACK_SCAN ? sp + STACK_SCAN : top;

        for (; sp < end && n < STACK_DEPTH; sp++)
            if (code_word(*sp)) out[n++] = *sp;
    }
    while (n < STACK_DEPTH) out[n++] = 0;
    if (++rt.stacks_taken == STACK_SAMPLES) {
        static const char head[] = "STACKS BEGIN\n";

        rt.stacks_on = 0;
        rt.stacks_dump = 0;
        surface_os_desktop_print(head, sizeof head - 1u);
    }
}

/** @brief Print the next stack sample of the dump. */
static void stack_line(void)
{
    char line[8 + STACK_DEPTH * 9];
    int at = snprintf(line, sizeof line, "K");

    for (unsigned i = 0; i < STACK_DEPTH; i++)
        at += snprintf(line + at, sizeof line - (size_t)at, " %08lx",
                       (unsigned long)stack_samples[rt.stacks_dump][i]);
    line[at++] = '\n';
    surface_os_desktop_print(line, (size_t)at);
    if (++rt.stacks_dump == STACK_SAMPLES) {
        static const char end[] = "STACKS END\n";

        surface_os_desktop_print(end, sizeof end - 1u);
        rt.stacks_dump = UINT32_MAX;
    }
}

int surface_runtime_stacks(void)
{
    if (!rt.running || rt.stacks_on || rt.stacks_dump != UINT32_MAX) return 0;
    rt.stacks_taken = 0;
    rt.stacks_on = 1;
    return 1;
}

typedef struct {
    const tiku_surface_t *surface;
    tiku_rect_t damage;
    int result, done;
} frame_request_t;

typedef struct {
    surface_media_io_t *io;
    int foreground, done;
} card_request_t;

typedef struct {
    int kind;
    void *frame;
    uint8_t *out;
    uint32_t cap, quality;
} camera_request_t;

typedef struct {
    int kind;
    const int *edge;
    int result, done;
} map_request_t;

/* The kernel admits requests and advances drivers on the surface process turn.
 * A waiting caller retains its request and buffers through completion. */
static struct {
    frame_request_t *frame;
    uint64_t serial, down_serial;
    int down_taken;
    tiku_input_event_t events[SURFACE_DESKTOP_EVENTS];
    unsigned head, tail;
    card_request_t *card;
    map_request_t *map;
    camera_request_t camera;
    void *stream_buffer;
    int cam_started, cam_opened, cam_result;
    uint32_t cam_len, picture, drawn, draw_us;
    char hash_path[SURFACE_HASH_PATH_BYTES];
    int hash_ready;
} devices;

static int request_done(void *arg)
{
    return __atomic_load_n((int *)arg, __ATOMIC_ACQUIRE);
}

static void wait_request(int *done)
{
    while (!request_done(done)) surface_os_desktop_wait(request_done, done, 0);
}

static int submit_frame(void *arg)
{
    if (devices.frame) return -1;
    devices.frame = arg;
    if (++devices.serial == 0) devices.serial++;
    surface_os_desktop_kick();
    return 0;
}

int surface_desktop_present(const tiku_surface_t *surface, tiku_rect_t damage)
{
    frame_request_t request = {.surface = surface, .damage = damage};
    if (surface_os_desktop_call(submit_frame, &request) < 0) return -1;
    wait_request(&request.done);
    return request.result;
}

static void release_frame(int result)
{
    frame_request_t *request = devices.frame;
    devices.frame = NULL;
    request->result = result;
    __atomic_store_n(&request->done, 1, __ATOMIC_RELEASE);
    surface_os_desktop_wake();
}

/* The SD card's queued media bridge: one request runs at a time. */
static _Alignas(8) unsigned char card_bridge[SURFACE_MEDIA_BRIDGE_BYTES];
static int card_submitted;
static uint64_t card_began;       /* when the pending request was first seen */
static uint32_t card_requests, card_polls, card_last_us, card_max_us; /* for the status line */
static uint64_t card_total_us;    /* time all finished requests took */
#define CARD_TURN_US 2000u        /* time a card request may take per kernel
                                   * turn, after the presentation's */

/** @brief Whether a card request waits for the kernel. */
static int card_pending(void)
{
    return devices.card != NULL;
}

/** @brief Whether the pending card request may have the processor: the
 *  driver moves its bytes by polling on the kernel thread, which preempts
 *  the desktop thread.  The desktop thread's own request may, since that
 *  thread waits for it.  The job thread's may only while the desktop thread
 *  is blocked and no frame of its waits for the panel, so a press and the
 *  frames it brings come first. */
static int card_urgent(void)
{
    if (devices.card->foreground) return 1;
    return surface_os_desktop_blocked() && rt.presenting == 0u &&
           devices.frame == NULL;
}

/** @brief Advance the desktop's card request by one bridge phase; nonzero
 *  when it finished.  A busy queue is retried on a later turn. */
static int card_step(void)
{
    card_request_t *request = devices.card;
    if (!request) return 0;
    surface_media_io_t *io = request->io;
    if (!card_submitted) {
        int r;

        if (card_began == 0) card_began = surface_os_time();
        r = surface_os_media_submit(card_bridge, io);

        if (r == SURFACE_MEDIA_BUSY) return 0;
        if (r != SURFACE_MEDIA_OK) {
            io->result = r;
            devices.card = NULL;
            card_began = 0;
            __atomic_store_n(&request->done, 1, __ATOMIC_RELEASE);
            return 1;
        }
        card_submitted = 1;
    }
    card_polls++;
    if (!surface_os_media_poll(card_bridge)) return 0;
    if (io->result == SURFACE_MEDIA_BUSY) {
        card_submitted = 0;
        return 0;
    }
    card_submitted = 0;
    card_requests++;
    card_last_us = (uint32_t)(surface_os_time() - card_began);
    card_total_us += card_last_us;
    if (card_last_us > card_max_us) card_max_us = card_last_us;
    card_began = 0;
    devices.card = NULL;
    __atomic_store_n(&request->done, 1, __ATOMIC_RELEASE);
    return 1;
}

/* Program counters of the preempted desktop thread, one per kernel turn. */
static uint32_t samples[SAMPLE_SLOTS];

uint64_t surface_desktop_now_us(void) { return surface_os_time(); }

static int submit_card(void *arg)
{
    if (!surface_desktop_bridge.card_mounted || devices.card) return -1;
    devices.card = arg;
    surface_os_desktop_kick();
    return 0;
}

/* The desktop thread and its job thread both ask for card requests; the
 * bridge carries one at a time. */
static pthread_mutex_t card_turn = PTHREAD_MUTEX_INITIALIZER;

#define CARD_NOW_US 5000000u    /* a start-up card request's limit */

/** @brief Run card request @p io to its end on the kernel thread, which
 *  asks only while the desktop starts, before its threads run: the bridge
 *  is polled here until the request ends, or cancelled past CARD_NOW_US. */
static void card_now(surface_media_io_t *io)
{
    uint64_t began = surface_os_time();
    int r;

    while ((r = surface_os_media_submit(card_bridge, io)) == SURFACE_MEDIA_BUSY) {
        (void)surface_os_media_poll(card_bridge);
        if (surface_os_time() - began > CARD_NOW_US) {
            io->result = SURFACE_MEDIA_UNAVAILABLE;
            return;
        }
    }
    if (r != SURFACE_MEDIA_OK) {
        io->result = r;
        return;
    }
    while (!surface_os_media_poll(card_bridge)) {
        if (surface_os_time() - began > CARD_NOW_US) {
            began = surface_os_time();
            while (!surface_os_media_cancel(card_bridge) && surface_os_time() - began < CARD_NOW_US) {
            }
            io->result = SURFACE_MEDIA_UNAVAILABLE;
            return;
        }
    }
}

unsigned surface_desktop_card_foreign(void) { return surface_os_media_foreign(card_bridge); }

void surface_desktop_card(surface_media_io_t *io)
{
    surface_desktop_bridge_t *b = &surface_desktop_bridge;

    if (surface_os_in_owner()) {
        if (!b->card_mounted) io->result = SURFACE_MEDIA_UNAVAILABLE;
        else card_now(io);
        return;
    }
    (void)pthread_mutex_lock(&card_turn);
    card_request_t request = {.io = io, .foreground = surface_os_in_desktop()};
    if (surface_os_desktop_call(submit_card, &request) == 0) wait_request(&request.done);
    else io->result = SURFACE_MEDIA_UNAVAILABLE;
    (void)pthread_mutex_unlock(&card_turn);
}

static int submit_map(void *arg)
{
    if (devices.map) return -1;
    devices.map = arg;
    surface_os_desktop_kick();
    return 0;
}

static int map_request(int kind, const int *edge)
{
    map_request_t request = {.kind = kind, .edge = edge};
    if (surface_os_desktop_call(submit_map, &request) < 0) return -1;
    wait_request(&request.done);
    return request.result;
}

/* The Deskbar clock's standing (TIKU_SHELL_CLOCK_STATE): the kernel's
 * wall-clock state, which tiku_rtc_state() reads safely on this thread. */
int tiku_shell_clock_state(void)
{
    switch (surface_os_clock_state()) {
    case SURFACE_CLOCK_SET:
        return TIKU_TRK_CLOCK_CURRENT;
    case SURFACE_CLOCK_RESUMED:
        return TIKU_TRK_CLOCK_RESUMED;
    default:
        return TIKU_TRK_CLOCK_UNSET;
    }
}

/* The shell's pointer map (TIKU_SHELL_TOUCH), which its calibration page
 * corrects. */
int tiku_shell_touch_fit(int x0, int x1, int y0, int y1)
{
    int edge[4] = { x0, x1, y0, y1 };

    return map_request(SURFACE_MAP_FIT, edge);
}

int tiku_shell_touch_keep(void) { return map_request(SURFACE_MAP_KEEP, NULL); }

void tiku_shell_touch_revert(void) { (void)map_request(SURFACE_MAP_REVERT, NULL); }

static int submit_camera(void *arg)
{
    if (devices.camera.kind) return -1;
    devices.camera = *(camera_request_t *)arg;
    devices.cam_len = 0;
    surface_os_desktop_kick();
    return 0;
}

int surface_desktop_camera(int kind, void *frame, uint8_t *out, uint32_t cap,
                           unsigned quality)
{
    if (kind < SURFACE_CAM_OPEN || kind > SURFACE_CAM_CLOSE) return -1;
    camera_request_t request = {kind, frame, out, cap, quality};
    return surface_os_desktop_call(submit_camera, &request);
}

static int camera_result(void *arg)
{
    if (devices.camera.kind) return SURFACE_CAMERA_BUSY;
    if (arg) *(uint32_t *)arg = devices.cam_len;
    return devices.cam_result;
}

int surface_desktop_camera_result(uint32_t *len)
{
    return surface_os_desktop_call(camera_result, len);
}

uint32_t surface_desktop_camera_picture(unsigned *half)
{
    uint32_t picture = __atomic_load_n(&devices.picture, __ATOMIC_ACQUIRE);
    if (half) *half = picture & 1u;
    return picture >> 1;
}

void surface_desktop_camera_drawn(uint32_t us)
{
    __atomic_store_n(&devices.draw_us, us, __ATOMIC_RELAXED);
    __atomic_add_fetch(&devices.drawn, 1, __ATOMIC_RELAXED);
}

/** @brief Advance the desktop's camera request by one turn; nonzero when
 *  it finished.  A frame or an encode starts on one turn and is polled on
 *  the later ones; open and close finish in the turn that starts them. */
static int camera_step(void)
{
    int rc = SURFACE_CAMERA_ERROR;
    uint32_t len = 0;

    if (!devices.camera.kind) return 0;
    if (!devices.cam_started) {
        switch (devices.camera.kind) {
        case SURFACE_CAM_OPEN:
            rc = devices.cam_opened ? SURFACE_CAMERA_OK : surface_os_camera_open();
            devices.cam_opened = rc == SURFACE_CAMERA_OK;
            if (devices.cam_opened) rc = surface_os_camera_stream(devices.camera.frame);
            break;
        case SURFACE_CAM_STREAM:
            if (devices.cam_opened) rc = surface_os_camera_stream(devices.camera.frame);
            break;
        case SURFACE_CAM_CLOSE:
            if (devices.cam_opened) {
                (void)surface_os_camera_stream(NULL);
                surface_os_camera_close();
            }
            devices.cam_opened = 0;
            rc = SURFACE_CAMERA_OK;
            break;
        case SURFACE_CAM_FRAME:
            if (devices.cam_opened) rc = surface_os_camera_capture(devices.camera.frame);
            break;
        case SURFACE_CAM_ENCODE:
            rc = surface_os_camera_encode(devices.camera.frame, devices.camera.quality, devices.camera.out, devices.camera.cap);
            break;
        default:
            break;
        }
        if (rc == SURFACE_CAMERA_BUSY) {
            devices.cam_started = 1;
            return 0;
        }
    } else {
        rc = devices.camera.kind == SURFACE_CAM_FRAME ? surface_os_camera_capture_poll() :
             surface_os_camera_encode_poll(&len);
        if (rc == SURFACE_CAMERA_BUSY) return 0;
        devices.cam_started = 0;
    }
    devices.cam_len = len;
    if (rc == SURFACE_CAMERA_OK && (devices.camera.kind == SURFACE_CAM_OPEN ||
                                    devices.camera.kind == SURFACE_CAM_STREAM))
        devices.stream_buffer = devices.camera.frame;
    if (devices.camera.kind == SURFACE_CAM_CLOSE || devices.camera.kind == SURFACE_CAM_FRAME)
        devices.stream_buffer = NULL;
    devices.cam_result = rc;
    devices.camera.kind = 0;
    return 1;
}

/** @brief Keep the newest streamed picture on the bridge, the cache's copy
 *  of its half dropped first; nonzero when it changed, so the desktop wakes
 *  to draw it. */
static int camera_watch(void)
{
    unsigned half = 0;
    uint32_t n = devices.stream_buffer ? surface_os_camera_newest(&half) : 0u;
    uint32_t picture = n ? n << 1 | half : 0u;

    if (picture == __atomic_load_n(&devices.picture, __ATOMIC_RELAXED)) return 0;
    if (n) surface_os_camera_drop(devices.stream_buffer, half);
    __atomic_store_n(&devices.picture, picture, __ATOMIC_RELEASE);
    return 1;
}

static void load_touch_map(void)
{
    unsigned char record[SURFACE_SETTINGS_BYTES];
    size_t length = 0;
    int result = surface_os_settings_read(record, sizeof record, &length);
    rt.map = (surface_touch_map_t){0, 799, 0, 479, 0, 0, 0};
    rt.map_status = surface_settings_load(result, record, (unsigned)length, &rt.map);
}

/** @brief Answer the desktop's touch map request, if one waits: a fit
 *  corrects the map in force and stores nothing, keep stores it, and revert
 *  reads the stored map again, or takes the panel's default without one. */
static int apply_map(const map_request_t *request)
{
    surface_touch_map_t next;
    switch (request->kind) {
    case SURFACE_MAP_FIT:
        if (surface_touch_map_refit(&rt.map, request->edge, &next)) {
            rt.map = next;
            rt.map_status = SURFACE_SETTINGS_PENDING;
            return 0;
        }
        return -1;
    case SURFACE_MAP_KEEP:
        return surface_runtime_calibrate(&rt.map) &&
               rt.map_status == SURFACE_SETTINGS_SAVED ? 0 : -1;
    case SURFACE_MAP_REVERT:
        load_touch_map();
        return 0;
    default:
        return -1;
    }
}

static int map_step(void)
{
    map_request_t *request = devices.map;
    if (!request) return 0;
    devices.map = NULL;
    request->result = apply_map(request);
    __atomic_store_n(&request->done, 1, __ATOMIC_RELEASE);
    return 1;
}

/** @brief Whether the log ring has room for another byte. */
static int log_room(void *context)
{
    (void)context;
    return surface_desktop_bridge.log_head - surface_desktop_bridge.log_tail < SURFACE_DESKTOP_LOG;
}

void surface_desktop_log(const void *bytes, size_t length)
{
    surface_desktop_bridge_t *b = &surface_desktop_bridge;
    const unsigned char *p = bytes;
    int waits = surface_os_in_desktop();

    while (length-- > 0u) {
        unsigned head = b->log_head;

        if (head - b->log_tail >= SURFACE_DESKTOP_LOG) {
            /* The desktop thread waits for the kernel to print; any other
             * caller drops the rest. */
            if (!waits) break;
            surface_os_desktop_kick();
            surface_os_desktop_wait(log_room, NULL, 100000u);
            if (!log_room(NULL)) break;
            head = b->log_head;
        }
        b->log[head % SURFACE_DESKTOP_LOG] = (char)*p++;
        b->log_head = head + 1u;
    }
    surface_os_desktop_kick();
}

/* Set by the control file's "report"; tiku_report_asked() takes it. */
static volatile int report_asked;

int surface_runtime_hash(const char *path, size_t length)
{
    if (!rt.running || !path || !length || !path[0] || length >= sizeof devices.hash_path ||
        devices.hash_path[0]) return 0;
    memcpy(devices.hash_path, path, length);
    devices.hash_path[length] = '\0';
    __atomic_store_n(&devices.hash_ready, 1, __ATOMIC_RELEASE);
    surface_os_desktop_wake();
    return 1;
}

static int take_hash(void *path)
{
    if (!__atomic_exchange_n(&devices.hash_ready, 0, __ATOMIC_ACQ_REL)) return 0;
    memcpy(path, devices.hash_path, sizeof devices.hash_path);
    return 1;
}

static int finish_hash(void *unused)
{
    (void)unused;
    devices.hash_path[0] = '\0';
    return 0;
}

void surface_desktop_hash_poll(void)
{
    char path[SURFACE_HASH_PATH_BYTES];
    if (!__atomic_load_n(&devices.hash_ready, __ATOMIC_ACQUIRE) || surface_os_desktop_call(take_hash, path) != 1) return;
    surface_desktop_hash_file(path);
    (void)surface_os_desktop_call(finish_hash, NULL);
}

int surface_runtime_report(void)
{
    if (!rt.running) return 0;
    report_asked = 1;
    surface_os_desktop_wake();
    return 1;
}

int surface_desktop_report_asked(void)
{
    int asked = report_asked;

    report_asked = 0;
    return asked;
}

/** @brief Print the log ring in contiguous pieces. */
static void drain_log(void)
{
    surface_desktop_bridge_t *b = &surface_desktop_bridge;

    while (b->log_tail != b->log_head) {
        unsigned tail = b->log_tail, head = b->log_head;
        unsigned at = tail % SURFACE_DESKTOP_LOG;
        unsigned n = head - tail;

        if (n > SURFACE_DESKTOP_LOG - at) n = SURFACE_DESKTOP_LOG - at;
        surface_os_desktop_print(&b->log[at], n);
        b->log_tail = tail + n;
    }
}

/** @brief Queue one pointer event; a full ring drops it.  A down event
 *  starts the measured delay until a frame the desktop drew after taking
 *  it is on the panel. */
static void post(tiku_event_type_t type, int x, int y, uint64_t now)
{
    unsigned head = devices.head;

    if (head - devices.tail >= SURFACE_DESKTOP_EVENTS) return;
    if (type == TIKU_EVENT_POINTER_DOWN) {
        rt.input_at = now;
        devices.down_taken = 0;
    }
    devices.events[head % SURFACE_DESKTOP_EVENTS] = (tiku_input_event_t){
        .type = type, .button = 1, .x = x, .y = y,
        .time_us = now > INT64_MAX ? INT64_MAX : (int64_t)now };
    __atomic_store_n(&devices.head, head + 1u, __ATOMIC_RELEASE);
}

int surface_desktop_input_ready(void *unused)
{
    (void)unused;
    return __atomic_load_n(&devices.head, __ATOMIC_ACQUIRE) !=
           __atomic_load_n(&devices.tail, __ATOMIC_ACQUIRE);
}

static int take_input(void *arg)
{
    tiku_input_event_t *event = arg;
    unsigned tail = devices.tail;

    if (tail == devices.head) return 0;
    *event = devices.events[tail % SURFACE_DESKTOP_EVENTS];
    tail++;
    /* A move with another move queued behind it is dropped for the later
     * one: the desktop sees where the contact is now, not where it was
     * while a frame was drawn. */
    while (event->type == TIKU_EVENT_POINTER_MOVE && tail != devices.head &&
           devices.events[tail % SURFACE_DESKTOP_EVENTS].type == TIKU_EVENT_POINTER_MOVE) {
        *event = devices.events[tail % SURFACE_DESKTOP_EVENTS];
        tail++;
    }
    __atomic_store_n(&devices.tail, tail, __ATOMIC_RELEASE);
    if (event->type == TIKU_EVENT_POINTER_DOWN) {
        devices.down_serial = devices.serial;
        devices.down_taken = 1;
    }
    return 1;
}

int surface_desktop_input(tiku_input_event_t *event)
{
    return surface_desktop_input_ready(NULL) && surface_os_desktop_call(take_input, event) == 1;
}

int surface_runtime_keys(const char *text, size_t length)
{
    unsigned head = devices.head;
    if (!rt.running || !length || length > SURFACE_DESKTOP_EVENTS ||
        head - devices.tail > SURFACE_DESKTOP_EVENTS - length) return 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c != 3 && c != '\r' && (c < 32 || c > 126)) return 0;
    }
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        tiku_input_event_t key = {.type = TIKU_EVENT_KEY_DOWN, .key = c,
                                  .time_us = (int64_t)surface_os_time()};
        if (c == '\r') key.key = TIKU_KEY_RETURN;
        else if (c == 3) {
            key.key = 'c';
            key.modifiers = TIKU_MOD_CMD;
        } else key.text[0] = (char)c;
        devices.events[(head + i) % SURFACE_DESKTOP_EVENTS] = key;
    }
    __atomic_store_n(&devices.head, head + (unsigned)length, __ATOMIC_RELEASE);
    surface_os_desktop_wake();
    return 1;
}

static void release(uint64_t now);
static int contact(int x, int y, unsigned id, uint64_t now);

/* The touch path's last pointer events, for /surface/touchlog. */
#define TOUCH_LOG 80u
#define TOUCH_QUICK_US 60000u   /* a press this soon after a release is no second touch */
typedef struct { char kind; uint16_t x, y, moves; uint32_t ms; } touch_log_t;
static touch_log_t touch_log[TOUCH_LOG];
static unsigned touch_log_head;
static uint64_t touch_lift_at;  /* when the touch path last released, 0 before */

/** @brief Keep one event of the touch path; moves in a row share a line. */
static void touch_note(char kind, int x, int y, uint64_t now)
{
    touch_log_t *last = touch_log_head ? &touch_log[(touch_log_head - 1u) % TOUCH_LOG] : NULL;

    if (kind == 'm' && last != NULL && last->kind == 'm') {
        last->x = (uint16_t)x;
        last->y = (uint16_t)y;
        last->ms = (uint32_t)(now / 1000u);
        if (last->moves < UINT16_MAX) last->moves++;
        return;
    }
    touch_log[touch_log_head % TOUCH_LOG] =
        (touch_log_t){ kind, (uint16_t)x, (uint16_t)y, 1u, (uint32_t)(now / 1000u) };
    touch_log_head++;
}

int surface_runtime_touch_log(char *buffer, size_t capacity)
{
    unsigned count = touch_log_head < TOUCH_LOG ? touch_log_head : TOUCH_LOG;
    unsigned first = touch_log_head - count;
    uint32_t before = 0;
    size_t used = 0;

    if (buffer == NULL || capacity == 0u) return 0;
    buffer[0] = 0;
    for (unsigned i = first; i != touch_log_head; i++) {
        const touch_log_t *e = &touch_log[i % TOUCH_LOG];
        int n = snprintf(buffer + used, capacity - used, "%c %u,%u +%lu n=%u\n", e->kind,
                         (unsigned)e->x, (unsigned)e->y,
                         (unsigned long)(i == first ? 0u : e->ms - before), (unsigned)e->moves);

        if (n < 0 || (size_t)n >= capacity - used) break;
        used += (size_t)n;
        before = e->ms;
    }
    return (int)used;
}

int surface_runtime_inject(char kind, unsigned x, unsigned y)
{
    uint64_t now = surface_os_time();

    if (!rt.running || x >= SURFACE_DESKTOP_W || y >= SURFACE_DESKTOP_H) return 0;
    switch (kind) {
    case 'd': post(TIKU_EVENT_POINTER_DOWN, (int)x, (int)y, now); break;
    case 'm': post(TIKU_EVENT_POINTER_MOVE, (int)x, (int)y, now); break;
    case 'u': post(TIKU_EVENT_POINTER_UP, (int)x, (int)y, now); break;
    case 't':
        post(TIKU_EVENT_POINTER_DOWN, (int)x, (int)y, now);
        post(TIKU_EVENT_POINTER_UP, (int)x, (int)y, now);
        break;
    case 'D':
        post(TIKU_EVENT_POINTER_DOWN, (int)x, (int)y, now);
        post(TIKU_EVENT_POINTER_UP, (int)x, (int)y, now);
        post(TIKU_EVENT_POINTER_DOWN, (int)x, (int)y, now);
        post(TIKU_EVENT_POINTER_UP, (int)x, (int)y, now);
        break;
    case 'f':
        (void)contact((int)x, (int)y, 0u, now);
        rt.touch_held = 1;
        break;
    case 'l':
        if (rt.touch_active) release(now);
        rt.touch_held = 0;
        break;
#if SURFACE_TOUCH_MODE
    case 'k':
        /* The mark with no finger: it stays until a finger moves it. */
        surface_presentation_mark(&rt.presentation, 1, (int)x, (int)y);
        break;
#endif
    default: return 0;
    }
    surface_os_desktop_wake();
    return 1;
}

int surface_runtime_drag(unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
    uint64_t now = surface_os_time();

    if (!rt.running || x0 >= SURFACE_DESKTOP_W || x1 >= SURFACE_DESKTOP_W ||
        y0 >= SURFACE_DESKTOP_H || y1 >= SURFACE_DESKTOP_H) return 0;
    post(TIKU_EVENT_POINTER_DOWN, (int)x0, (int)y0, now);
    for (int k = 1; k <= 8; k++)
        post(TIKU_EVENT_POINTER_MOVE, (int)x0 + ((int)x1 - (int)x0) * k / 8,
             (int)y0 + ((int)y1 - (int)y0) * k / 8, now);
    post(TIKU_EVENT_POINTER_UP, (int)x1, (int)y1, now);
    surface_os_desktop_wake();
    return 1;
}

/** @brief The primary contact as panel coordinates, by the calibration. */
static void map_point(unsigned rx, unsigned ry, int *x, int *y)
{
    const surface_touch_map_t *m = &rt.map;
    uint32_t dx = m->x_max - m->x_min, dy = m->y_max - m->y_min;
    uint32_t px = rx < m->x_min ? 0u : rx > m->x_max ? dx : rx - m->x_min;
    uint32_t py = ry < m->y_min ? 0u : ry > m->y_max ? dy : ry - m->y_min;

    if (m->swap) {
        uint32_t t = px; px = py; py = t;
        t = dx; dx = dy; dy = t;
    }
    *x = (int)((px * 799u + dx / 2u) / dx);
    *y = (int)((py * 479u + dy / 2u) / dy);
    if (m->invert_x) *x = 799 - *x;
    if (m->invert_y) *y = 479 - *y;
}

/** @brief The finger left the panel: release the pointer where it is. */
static void release(uint64_t now)
{
    int x = rt.touch_x, y = rt.touch_y;

#if SURFACE_TOUCH_MODE
    (void)surface_gesture_lift(&rt.gesture, now, &x, &y);
    surface_presentation_mark(&rt.presentation, 0, 0, 0);
#endif
    post(TIKU_EVENT_POINTER_UP, x, y, now);
    touch_note('u', x, y, now);
    rt.input.ups++;
    touch_lift_at = now ? now : 1u;
    rt.touch_active = 0;
}

/** @brief One touch report as pointer down, move and up events. */
static int touch(const surface_touch_report_t *r, uint64_t now)
{
    unsigned i, primary;

    rt.input.reports++;
    if (r->cancel || r->count > SURFACE_TOUCH_CONTACTS) {
        rt.input.errors++;
        if (rt.touch_active) {
            release(now);
            return 1;
        }
        return 0;
    }
    rt.input.contacts = r->count;
    rt.last_report = now;
    if (r->count == 0u) {
        /* A finger placed by the control file stays until "lift". */
        if (!rt.touch_active || rt.touch_held) return 0;
        release(now);
        return 1;
    }
    primary = 0;
    for (i = 0; i < r->count; i++) {
        if (rt.touch_active && r->points[i].id == rt.touch_id) {
            primary = i;
            break;
        }
        if (r->points[i].id < r->points[primary].id) primary = i;
    }
    rt.input.id = r->points[primary].id;
    rt.input.x = r->points[primary].x;
    rt.input.y = r->points[primary].y;
    rt.touch_held = 0;
    {
        int x, y;

        map_point(r->points[primary].x, r->points[primary].y, &x, &y);
        return contact(x, y, r->points[primary].id, now);
    }
}

/** @brief The primary contact at panel point (@p x, @p y) as pointer events. */
static int contact(int x, int y, unsigned id, uint64_t now)
{
#if SURFACE_TOUCH_MODE
    int px = rt.touch_x, py = rt.touch_y, event;

    /* The mark follows the finger itself; the pointer follows the gesture,
     * which holds a press still inside the slop. */
    surface_presentation_mark(&rt.presentation, 1, x, y);
    event = surface_gesture_contact(&rt.gesture, x, y, now, &px, &py);
    if (!rt.touch_active) {
        rt.touch_active = 1;
        rt.touch_id = id;
    }
    rt.touch_x = px;
    rt.touch_y = py;
    if (event == SURFACE_GESTURE_NONE) return 0;
    if (event == SURFACE_GESTURE_DOWN) {
        if (px != x || py != y) touch_note('r', x, y, now);
        rt.input.downs++;
        if (touch_lift_at != 0u) {
            uint32_t gap = (uint32_t)((now - touch_lift_at) / 1000u);

            if (now - touch_lift_at < TOUCH_QUICK_US) rt.input.quick++;
            if (rt.input.gap_ms == 0u || gap < rt.input.gap_ms) rt.input.gap_ms = gap ? gap : 1u;
        }
    }
    touch_note(event == SURFACE_GESTURE_DOWN ? 'd' : 'm', px, py, now);
    post(event == SURFACE_GESTURE_DOWN ? TIKU_EVENT_POINTER_DOWN : TIKU_EVENT_POINTER_MOVE,
         px, py, now);
    return 1;
#else
    if (!rt.touch_active) {
        rt.touch_active = 1;
        rt.touch_id = id;
        rt.touch_x = x;
        rt.touch_y = y;
        post(TIKU_EVENT_POINTER_DOWN, x, y, now);
        return 1;
    }
    if (x == rt.touch_x && y == rt.touch_y) return 0;
    rt.touch_x = x;
    rt.touch_y = y;
    post(TIKU_EVENT_POINTER_MOVE, x, y, now);
    return 1;
#endif
}

int surface_touch_map_valid(const surface_touch_map_t *m)
{
    return m != NULL && m->x_min < m->x_max && m->y_min < m->y_max &&
           m->swap <= 1 && m->invert_x <= 1 && m->invert_y <= 1;
}

static tiku_present_result_t scan_submit(void *context, unsigned back, tiku_rect_t r,
                                         uint64_t ticket)
{
    (void)context;
    int result = surface_os_display_submit(back, (unsigned)r.x, (unsigned)r.y,
                                           (unsigned)r.w, (unsigned)r.h, ticket);
    return result == SURFACE_SCANOUT_OK ? TIKU_PRESENT_OK : TIKU_PRESENT_ERROR;
}

static tiku_present_result_t scan_poll(void *context, uint64_t ticket)
{
    (void)context;
    int result = surface_os_display_poll(ticket);

    return result == SURFACE_SCANOUT_OK ? TIKU_PRESENT_OK :
           result == SURFACE_SCANOUT_BUSY ? TIKU_PRESENT_BUSY : TIKU_PRESENT_ERROR;
}

/** @brief Start the PPA on a part of the view: 0 running, -1 refused. */
static int scan_convert(void *context, unsigned back, const tiku_present_view_t *v, tiku_rect_t r)
{
    (void)context;
    return surface_os_display_convert(back, v->pixels, (unsigned)v->stride, (unsigned)r.x,
        (unsigned)r.y, (unsigned)r.w, (unsigned)r.h) == SURFACE_SCANOUT_OK ? 0 : -1;
}

/** @brief The PPA's job: 1 running, 0 done, -1 failed. */
static int scan_convert_poll(void *context)
{
    (void)context;
    int result = surface_os_display_convert_poll();

    return result == SURFACE_SCANOUT_BUSY ? 1 : result == SURFACE_SCANOUT_OK ? 0 : -1;
}

#if SURFACE_TOUCH_MODE
/** @brief Make the touch mark drawn into the buffer on show visible. */
static int scan_amend(void *context, unsigned buffer, tiku_rect_t r)
{
    (void)context;
    return surface_os_display_amend(buffer, (unsigned)r.x, (unsigned)r.y,
                                    (unsigned)r.w, (unsigned)r.h);
}

static const surface_scanout_ops_t scanout = { scan_submit, scan_poll, scan_amend, scan_convert,
                                                scan_convert_poll };
#else
static const surface_scanout_ops_t scanout = { scan_submit, scan_poll, NULL, scan_convert,
                                                scan_convert_poll };
#endif

/** @brief The presentation lease ends: the frame is on the panel, or failed. */
int tiku_framebuffer_complete(tiku_framebuffer_t *owner, uint64_t token,
                              tiku_present_result_t result)
{
    surface_desktop_bridge_t *b = &surface_desktop_bridge;
    uint64_t now = surface_os_time();

    if (owner != &rt.owner || !devices.frame || !token || token != rt.presenting) return 0;
    rt.presenting = 0;
    b->frames++;
    rt.frame.completed++;
    rt.frame.frame_us = (uint32_t)(now - rt.frame_started);
    if (rt.frame.frame_us > rt.frame.max_frame_us) rt.frame.max_frame_us = rt.frame.frame_us;
    if (rt.input_at != 0u && devices.down_taken && token > devices.down_serial) {
        rt.input_us = (uint32_t)(now - rt.input_at);
        rt.input_at = 0;
    }
    release_frame(result == TIKU_PRESENT_OK ? 0 : -1);
    return 1;
}

/** @brief Send up to 32 RGB565 runs from the frozen front buffer per kernel turn. */
static void capture_chunk(void)
{
    const uint16_t *row = rt.rgb[rt.presentation.front] + rt.capture_row * SURFACE_DESKTOP_W;
    static const char hex[] = "0123456789abcdef";
    char chunk[8 + 32 * 6];
    size_t n = 0;
    unsigned x = rt.capture_x;

    if (x == 0) n = (size_t)snprintf(chunk, sizeof chunk, "F%03u ", rt.capture_row);
    for (unsigned i = 0; i < 32 && x < SURFACE_DESKTOP_W; i++) {
        unsigned run = 1, color = row[x];
        while (run < 255 && x + run < SURFACE_DESKTOP_W && row[x + run] == color) run++;
        chunk[n++] = hex[run >> 4];
        chunk[n++] = hex[run & 15];
        for (int shift = 12; shift >= 0; shift -= 4) chunk[n++] = hex[(color >> shift) & 15];
        x += run;
    }
    if (x == SURFACE_DESKTOP_W) {
        chunk[n++] = '\n';
        x = 0;
        rt.capture_row++;
    }
    rt.capture_x = x;
    surface_os_desktop_print(chunk, n);
    if (rt.capture_row == SURFACE_DESKTOP_H) {
        static const char end[] = "CAPTURE END\n";
        surface_os_desktop_print(end, sizeof end - 1u);
        rt.capturing = 0;
    }
}

/* Present in a profile build only; see heap_profile.c. */
extern void surface_desktop_heap_report(void (*print)(const void *, size_t)) __attribute__((weak));

int surface_runtime_heap_report(void)
{
    if ((uintptr_t)surface_desktop_heap_report == 0u) return 0;
    surface_desktop_heap_report(surface_os_desktop_print);
    return 1;
}

int surface_runtime_samples(void)
{
    char head[80];
    int n;

    if (!rt.running || rt.capturing || rt.sample_dump != UINT32_MAX) return 0;
    rt.sample_end = rt.sample_head;
    rt.sample_dump = rt.sample_end > SAMPLE_SLOTS ? rt.sample_end - SAMPLE_SLOTS : 0u;
    n = snprintf(head, sizeof head, "SAMPLES BEGIN taken=%lu idle=%lu\n",
                 (unsigned long)rt.sample_end, (unsigned long)rt.sample_idle);
    surface_os_desktop_print(head, (size_t)n);
    return 1;
}

/** @brief Print the next line of the sample dump. */
static void sample_line(void)
{
    char line[16 + SAMPLE_LINE * 9];
    int at = snprintf(line, sizeof line, "S");

    for (unsigned i = 0; i < SAMPLE_LINE && rt.sample_dump < rt.sample_end; i++, rt.sample_dump++)
        at += snprintf(line + at, sizeof line - (size_t)at, " %08lx",
                       (unsigned long)samples[rt.sample_dump % SAMPLE_SLOTS]);
    line[at++] = '\n';
    surface_os_desktop_print(line, (size_t)at);
    if (rt.sample_dump == rt.sample_end) {
        static const char end[] = "SAMPLES END\n";

        surface_os_desktop_print(end, sizeof end - 1u);
        rt.sample_dump = UINT32_MAX;
    }
}

int surface_runtime_capture(void)
{
    if (!rt.running || rt.capturing || rt.presenting || rt.sample_dump != UINT32_MAX) return 0;
    rt.capturing = 1;
    rt.capture_row = rt.capture_x = 0;
    {
        char head[64];
        int n = snprintf(head, sizeof head, "CAPTURE BEGIN %d %d front=%u\n",
                         SURFACE_DESKTOP_W, SURFACE_DESKTOP_H, rt.presentation.front);

        surface_os_desktop_print(head, (size_t)n);
    }
    return 1;
}

/** @brief Hand the desktop's waiting frame to the presentation. */
static void take_frame(void)
{
    frame_request_t *request = devices.frame;
    uint64_t token = devices.serial;
    tiku_present_view_t view;

    if (rt.presenting || rt.capturing || !request) return;
    const tiku_surface_t *s = request->surface;
    view = (tiku_present_view_t){
        .pixels = s->px, .bytes = (size_t)s->w * (size_t)s->h * 4u,
        .stride = (size_t)s->w * 4u, .width = s->w, .height = s->h,
        .format = TIKU_PIXELS_XRGB8888, .damage = request->damage,
        .token = token, .owner = &rt.owner };
    rt.frame.requested++;
    rt.frame_started = surface_os_time();
    rt.presenting = token;
    if (surface_presentation_submit(&rt.presentation, &view) != TIKU_PRESENT_OK) {
        rt.presenting = 0;
        release_frame(-1);
    }
}

static void desktop_thread(void *arg)
{
    static char *argv[] = { "TikuSurface", NULL };
    int code;

    (void)arg;
    surface_desktop_selftest();
    surface_board_selftest();
    code = surface_desktop_main(1, argv);
    (void)surface_desktop_views_save();
    surface_desktop_bridge.exit_code = code;
    surface_desktop_bridge.exited = 1;
    surface_os_desktop_kick();
}

void surface_desktop_memory(surface_desktop_memory_t *m)
{
    m->psram = rt.psram;
    m->below = rt.psram_below;
    m->tier = rt.psram_tier;
    m->spare = rt.psram_spare;
    m->span = SURFACE_FRAMES_BYTES + SURFACE_RUNTIME_BYTES;
    m->frames = SURFACE_FRAMES_BYTES;
    m->stack = STACK_BYTES;
    m->stack_used = rt.stack_used;
    m->jobs = JOBS_BYTES;
    m->jobs_used = rt.jobs_used;
    m->stages = STAGES_BYTES;
    m->heap = SURFACE_RUNTIME_BYTES - STACK_BYTES - JOBS_BYTES - STAGES_BYTES;
}

int surface_runtime_start(void)
{
    unsigned char *frames, *span, *stack, *jobs, *stages, *heap;

    clear_statics();
    if (rt.running) return 1;
    frames = surface_os_reserve_frames(SURFACE_FRAMES_BYTES);
    if (frames == NULL) return 0;
    memset(&devices, 0, sizeof devices);
    memset(&surface_desktop_bridge, 0, sizeof surface_desktop_bridge);
    memset(&rt, 0, sizeof rt);
    rt.sample_dump = UINT32_MAX;
    rt.stacks_dump = UINT32_MAX;
    rt.rgb[0] = (uint16_t *)frames;
    rt.rgb[1] = (uint16_t *)(frames + RGB_BYTES);
    memset(rt.rgb[0], 0, 2u * RGB_BYTES);
    /* The panel opens on the frames before the rest is reserved: its
     * driver returns its own scan-out buffer to the PSRAM tier when it is
     * handed them, and the rest takes that room. */
    if (!surface_os_display_open(rt.rgb[0], rt.rgb[1]) ||
        !surface_presentation_init(&rt.presentation, rt.rgb[0], RGB_BYTES, rt.rgb[1],
                                   RGB_BYTES, 0, 4096, &rt.sequence, &scanout, NULL)) {
        rt.failed = 1;
        return 0;
    }
    span = surface_os_reserve(SURFACE_RUNTIME_BYTES);
    if (span == NULL) {
        rt.failed = 1;
        return 0;
    }
    /* PSRAM's extent, for the About box: figures the kernel gives only its
     * owner, the tier's free bytes again with each status line. */
    if (surface_os_psram_extent(&rt.psram, &rt.psram_below, &rt.psram_tier, &rt.psram_spare) != 0)
        rt.psram = rt.psram_below = rt.psram_tier = rt.psram_spare = 0;
    rt.span = span;
    stack = span;
    jobs = stack + STACK_BYTES;
    stages = jobs + JOBS_BYTES;
    heap = stages + STAGES_BYTES;
    rt.stack = (uint32_t *)(void *)stack;
    for (uint32_t i = 0; i < STACK_BYTES / 4u; i++) rt.stack[i] = STACK_PAINT;
    rt.jobs = (uint32_t *)(void *)jobs;
    for (uint32_t i = 0; i < JOBS_BYTES / 4u; i++) rt.jobs[i] = STACK_PAINT;
    if (surface_os_desktop_heap_memory(heap, SURFACE_RUNTIME_BYTES - STACK_BYTES -
                                       JOBS_BYTES - STAGES_BYTES) != 0 ||
        surface_os_desktop_jobs_memory(jobs, JOBS_BYTES) != 0 ||
        surface_os_desktop_store_memory(stages, STAGES_BYTES) != 0 ||
        surface_os_desktop_authority() != 0) {
        rt.failed = 1;
        return 0;
    }
    card_submitted = 0;
    if (surface_os_media_size() <= sizeof card_bridge) {
        surface_os_media_init(card_bridge);
        surface_desktop_bridge.card_mounted = surface_os_card_mount() == 0;
    }
    if (surface_desktop_files() != 0) return 0;
    if (surface_desktop_views_load() != 0) {
        static const char line[] = "TikuSurface: the folder views in the store did not load\n";

        surface_os_desktop_print(line, sizeof line - 1u);
    }
    if (surface_desktop_random_seed() != 0) {
        static const char line[] = "TikuSurface: the hardware random source gave no seed\n";

        surface_os_desktop_print(line, sizeof line - 1u);
    }
    load_touch_map();
    rt.input_open = surface_os_input_open(&rt.input_owned);
    if (surface_os_desktop_start(desktop_thread, NULL, stack, STACK_BYTES) != 0) {
        rt.failed = 1;
        return 0;
    }
    rt.running = 1;
    return 1;
}

void surface_runtime_stop(void)
{
    /* The desktop thread runs until the board resets. */
}

/** @brief One step of a stack's high-water scan.  The stack grows down:
 *  the first word above the bottom that no longer holds the paint marks the
 *  deepest use.  A pass covers the whole stack over several turns and then
 *  starts again; word 0 holds the thread's canary, not paint. */
static void paint_scan(const uint32_t *words, uint32_t bytes, uint32_t *scan, uint32_t *used)
{
    uint32_t end = *scan + STACK_SCAN_WORDS;

    if (end > bytes / 4u) end = bytes / 4u;
    if (*scan == 0u) *scan = 1u;
    while (*scan < end && words[*scan] == STACK_PAINT) (*scan)++;
    if (*scan < end || end == bytes / 4u) {
        uint32_t deepest = bytes - *scan * 4u;

        if (deepest > *used) *used = deepest;
        *scan = 0;
    }
}

int surface_runtime_poll(void)
{
    surface_desktop_bridge_t *b = &surface_desktop_bridge;
    uint64_t begin, now;
    int woke = 0;

    if (!rt.running) return 0;
    begin = surface_os_time();
    rt.turns++;
    {
        uintptr_t pc = surface_os_desktop_sample();

        if (pc != 0u) {
            samples[rt.sample_head % SAMPLE_SLOTS] = (uint32_t)pc;
            rt.sample_head++;
        } else {
            rt.sample_idle++;
        }
        if (rt.stacks_on) stack_sample();
    }
    b->turns++;
    if (rt.input_open) {
        surface_touch_report_t report;

        /* A poll with no report leaves the finger where the last report
         * put it; TOUCH_EXPIRE_US without one releases it. */
        switch (surface_os_input_poll(&report)) {
        case SURFACE_OS_INPUT_REPORT: woke |= touch(&report, begin); break;
        case SURFACE_OS_INPUT_NONE: break;
        default: rt.input.errors++; break;
        }
        if (rt.touch_active && !rt.touch_held && begin - rt.last_report >= TOUCH_EXPIRE_US) {
            touch_note('x', rt.touch_x, rt.touch_y, begin);
            release(begin);
            woke = 1;
        }
    }
    take_frame();
    while (rt.presenting && surface_os_time() - begin < PRESENT_TURN_US) {
        unsigned before = rt.presentation.phase;

        if (before != rt.phase_seen) {
            uint64_t t = surface_os_time();

            if (rt.phase_seen != 0u && rt.phase_seen < 4u)
                rt.phase_us[rt.phase_seen] = (uint32_t)(t - rt.phase_at);
            if (before == 1u) {
                rt.region_px = rt.presentation.convert_px;
                rt.convert_cpu = 0;
            }
            if (rt.phase_seen == 1u) rt.convert_cpu_last = rt.convert_cpu;
            rt.phase_seen = before;
            rt.phase_at = t;
        }
        uint64_t call = surface_os_time();
        tiku_present_result_t polled = surface_presentation_poll(&rt.presentation);

        if (before == 1u) rt.convert_cpu += (uint32_t)(surface_os_time() - call);
        /* A running PPA job queues the next turn from its interrupt. */
        if (polled == TIKU_PRESENT_BUSY && rt.presentation.hw && surface_os_display_convert_wakes())
            break;
        if (polled != TIKU_PRESENT_BUSY) {
            uint64_t t = surface_os_time();

            if (rt.phase_seen != 0u && rt.phase_seen < 4u)
                rt.phase_us[rt.phase_seen] = (uint32_t)(t - rt.phase_at);
            rt.phase_seen = 0;
            break;
        }
    }
    if (map_step()) woke = 1;
    if (camera_step()) woke = 1;
    if (camera_watch()) woke = 1;
    /* A request that may not have the processor is polled once a turn. */
    if (card_pending() && !card_urgent() && card_step()) woke = 1;
    while (card_pending() && card_urgent() &&
           surface_os_time() - begin < PRESENT_TURN_US + CARD_TURN_US) {
        if (card_step()) {
            woke = 1;
            break;
        }
    }
    /* A card session left open for the next request is closed once it
     * has lingered past the build's limit. */
    if (!card_pending() && surface_desktop_bridge.card_mounted) (void)surface_os_media_idle(card_bridge);
    if (woke) surface_os_desktop_wake();
    if (rt.capturing) {
        capture_chunk();
    } else if (rt.sample_dump != UINT32_MAX) {
        sample_line();
    } else if (rt.stacks_dump != UINT32_MAX) {
        stack_line();
    } else {
        drain_log();
    }
    if (rt.stack != NULL) paint_scan(rt.stack, STACK_BYTES, &rt.stack_scan, &rt.stack_used);
    if (rt.jobs != NULL) paint_scan(rt.jobs, JOBS_BYTES, &rt.jobs_scan, &rt.jobs_used);
    if (!rt.capturing && rt.sample_dump == UINT32_MAX && rt.stacks_dump == UINT32_MAX &&
        begin - rt.last_status >= STATUS_US) {
        char line[560];
        int n;

        size_t used = 0, total = 0, largest = 0;
        uint32_t refused = 0, console = 0, files = 0, calls = 0;
        uint64_t mutations = 0;
        uint32_t ppa_jobs = 0, ppa_failed = 0, ppa_wakes = 0, ppa_us = 0;
        const char *last = "";
        unsigned lock_mutexes = 0, lock_conds = 0, lock_refused = 0;

        rt.last_status = begin;
        if (rt.psram > 0u) {
            size_t size, below, tier;

            (void)surface_os_psram_extent(&size, &below, &tier, &rt.psram_spare);
        }
        surface_os_display_convert_stats(&ppa_jobs, &ppa_failed, &ppa_wakes, &ppa_us);
        surface_os_desktop_heap_use(&used, &total, &refused, &largest);
        surface_desktop_write_stats(&console, &files, &calls, &last);
        (void)tiku_posix_mutations(&mutations);
        surface_os_desktop_lock_use(&lock_mutexes, &lock_conds, &lock_refused);
        n = snprintf(line, sizeof line,
                     "desktop: turns=%lu frames=%lu waits=%lu events=%u/%u present=%lu/%lu phase=%u max_us=%lu"
                     " heap=%lu/%lu refused=%lu/%lu stack=%lu/%lu jobs=%lu/%lu canary=%u locks=%u/%u/%u"
                     " writes=%lu/%lu/%lu:%.24s"
                     " up_ms=%lu frame_us=%lu/%lu input_us=%lu phases=%lu/%lu/%lu px=%lu convert_cpu=%lu"
                     " card=%lu/%lu/%lu/%lu/%lu mutations=%lu ppa=%lu/%lu/%lu/%lu ring_lost=%lu"
                     " camera=%lu/%lu/%lu\n",
                     (unsigned long)rt.turns, (unsigned long)b->frames, (unsigned long)b->waits,
                     devices.head, devices.tail, (unsigned long)devices.serial,
                     (unsigned long)(devices.serial - (devices.frame != NULL)), rt.presentation.phase,
                     (unsigned long)rt.max_turn_us, (unsigned long)used, (unsigned long)total,
                     (unsigned long)refused, (unsigned long)largest,
                     (unsigned long)rt.stack_used, (unsigned long)STACK_BYTES,
                     (unsigned long)rt.jobs_used, (unsigned long)JOBS_BYTES,
                     surface_os_desktop_canary_faults(),
                     lock_mutexes, lock_conds, lock_refused,
                     (unsigned long)console, (unsigned long)files, (unsigned long)calls, last,
                     (unsigned long)(begin / 1000u), (unsigned long)rt.frame.frame_us,
                     (unsigned long)rt.frame.max_frame_us, (unsigned long)rt.input_us,
                     (unsigned long)rt.phase_us[1], (unsigned long)rt.phase_us[2],
                     (unsigned long)rt.phase_us[3], (unsigned long)rt.region_px,
                     (unsigned long)rt.convert_cpu_last, (unsigned long)card_requests,
                     (unsigned long)card_polls, (unsigned long)card_last_us, (unsigned long)card_max_us,
                     (unsigned long)(card_total_us / 1000u), (unsigned long)mutations,
                     (unsigned long)ppa_jobs, (unsigned long)ppa_failed,
                     (unsigned long)ppa_wakes, (unsigned long)ppa_us, surface_board_lost(),
                     (unsigned long)(devices.picture >> 1), (unsigned long)__atomic_load_n(&devices.drawn, __ATOMIC_RELAXED),
                     (unsigned long)__atomic_load_n(&devices.draw_us, __ATOMIC_RELAXED));
        if (n > 0) surface_os_desktop_print(line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1u);
    }
    if (b->exited == 1) {
        static const char said[] = "TikuSurface: the desktop ended\n";

        surface_os_desktop_print(said, sizeof said - 1u);
        b->exited = 2;
    }
    now = surface_os_time();
    rt.last_turn_us = (uint32_t)(now - begin);
    if (rt.last_turn_us > rt.max_turn_us) rt.max_turn_us = rt.last_turn_us;
    if (rt.last_turn_us > 5000u) rt.overruns++;
    return 1;
}

int surface_runtime_continue(void)
{
    uint64_t now = surface_os_time();
    /* Conversion and submission continue at once; a frame waiting on the
     * panel's DMA, or a burst past its bound, waits for the timer turn, and
     * one waiting on the PPA for its interrupt or the timer turn; the desktop
     * has the processor meanwhile. */
    int work = rt.running && (rt.capturing || rt.sample_dump != UINT32_MAX ||
        rt.stacks_dump != UINT32_MAX || (card_pending() && card_urgent()) ||
        (rt.presenting != 0 && ((rt.presentation.phase == 1u && !rt.presentation.hw) ||
                                rt.presentation.phase == 2u)) ||
         (rt.presenting == 0 && devices.frame != NULL));

    if (!work) {
        rt.burst = 0;
        return 0;
    }
    if (rt.burst == 0) rt.burst_start = now;
    if (++rt.burst > BURST_TURNS || now - rt.burst_start > BURST_US) {
        rt.burst = 0;
        return 0;
    }
    return 1;
}

int surface_runtime_launch(unsigned id) { (void)id; return 0; }

int surface_runtime_calibrate(const surface_touch_map_t *map)
{
    unsigned char record[SURFACE_SETTINGS_BYTES];

    if (!rt.running || !surface_touch_map_valid(map)) return 0;
    rt.map = *map;
    rt.map_status = surface_settings_encode(map, record)
        ? surface_settings_stored(surface_os_settings_write(record, sizeof record))
        : SURFACE_SETTINGS_FAILED;
    return 1;
}

int surface_runtime_calibration(surface_touch_map_t *map, unsigned *status)
{
    if (!rt.running) return 0;
    if (map != NULL) *map = rt.map;
    if (status != NULL) *status = rt.map_status;
    return 1;
}

int surface_runtime_preferences(surface_preferences_t *p, unsigned *status)
{ (void)p; (void)status; return 0; }
int surface_runtime_set_preferences(const surface_preferences_t *p) { (void)p; return 0; }

void surface_runtime_stats(surface_runtime_stats_t *out)
{
    size_t used = 0, total = 0;

    if (out == NULL) return;
    memset(out, 0, sizeof *out);
    surface_os_desktop_heap_use(&used, &total, NULL, NULL);
    out->running = (uint32_t)rt.running;
    out->turns = rt.turns;
    out->frames = surface_desktop_bridge.frames;
    out->live_bytes = (uint32_t)used;
    out->peak_bytes = (uint32_t)used;
    out->last_dispatch_us = rt.last_turn_us;
    out->max_dispatch_us = rt.max_turn_us;
    out->overruns = rt.overruns;
    out->lcd = 1;
    out->completed = rt.frame.completed;
}

int surface_runtime_frame_stats(surface_frame_stats_t *out)
{
    if (out == NULL || !rt.running) return 0;
    *out = rt.frame;
    out->pending = (uint32_t)(rt.presenting != 0);
    return 1;
}

int surface_runtime_input_stats(surface_input_stats_t *out)
{
    if (out == NULL || !rt.running) return 0;
    *out = rt.input;
    return 1;
}

void surface_runtime_fail_at(unsigned attempt) { (void)attempt; }
