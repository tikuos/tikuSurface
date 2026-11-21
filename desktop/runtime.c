/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * runtime.c - the kernel side of the TikuDesktop shell on the S31 panel.
 *
 * Start reserves the runtime's PSRAM span and divides it into the two RGB565
 * scanout buffers, the desktop thread's stack and the C-library heap; it
 * mounts the SD card without writing, seeds the desktop's random generator,
 * opens the panel and touch and starts the desktop thread.  Each owner turn
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
#include <stdio.h>
#include <string.h>

#define RGB_BYTES (SURFACE_DESKTOP_W * SURFACE_DESKTOP_H * 2u)
/* The desktop keeps its state, desktop_t (2,586,224 bytes on the S31 with
 * TikuSurface's bounds), in a local variable of tracker_run(); the rest
 * covers the deepest call chains, among them emptying the Trash from an
 * inline job (a 280,640-byte frame).  The stack is painted with
 * STACK_PAINT at start; the status line reports the deepest use seen. */
#define STACK_BYTES (3072u * 1024u)
#define STACK_PAINT 0xa5a5a5a5u
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
#define BURST_TURNS 32u         /* queued turns before the timer drives again */
#define BURST_US 20000u
#define STATUS_US 2000000u      /* console status line period */

int surface_desktop_main(int argc, char **argv);
void surface_desktop_heap(void *base, size_t bytes);
void surface_desktop_heap_use(size_t *used, size_t *total);
void surface_desktop_heap_refused(uint32_t *count, size_t *largest);
void surface_desktop_write_stats(uint32_t *console, uint32_t *files, uint32_t *calls,
                                 const char **last);
int surface_desktop_files(void);
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
    uint64_t last_report, frame_started;
    surface_input_stats_t input;
    surface_frame_stats_t frame;
    uint32_t turns, max_turn_us, last_turn_us, overruns;
    uint32_t burst;
    uint32_t *stack;          /* lowest word of the desktop thread's stack */
    uint32_t stack_scan;      /* next word the high-water scan examines */
    uint32_t stack_used;      /* deepest use found, in bytes */
    uint64_t burst_start, last_status;
    int capturing;
    unsigned capture_row;
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
    uint64_t input_at;        /* when a down event was posted, until a frame shows it */
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

/* The SD card's queued media bridge: one request runs at a time. */
static _Alignas(8) unsigned char card_bridge[SURFACE_MEDIA_BRIDGE_BYTES];
static unsigned card_submitted;   /* the token submitted to the bridge, or 0 */
static uint64_t card_began;       /* when the pending request was first seen */
static uint32_t card_requests, card_polls, card_last_us, card_max_us; /* for the status line */
static uint64_t card_total_us;    /* time all finished requests took */
#define CARD_TURN_US 2000u        /* time a card request may take per kernel
                                   * turn, after the presentation's */

/** @brief Whether a card request waits for the kernel. */
static int card_pending(void)
{
    surface_desktop_bridge_t *b = &surface_desktop_bridge;

    return b->card != NULL && b->card_token != b->card_done;
}

/** @brief Advance the desktop's card request by one bridge phase; nonzero
 *  when it finished.  A busy queue is retried on a later turn. */
static int card_step(void)
{
    surface_desktop_bridge_t *b = &surface_desktop_bridge;
    surface_media_io_t *io = b->card;
    unsigned token = b->card_token;

    if (!card_pending()) return 0;
    if (!b->card_mounted) {
        io->result = SURFACE_MEDIA_UNAVAILABLE;
        b->card_done = token;
        return 1;
    }
    if (card_submitted != token) {
        int r;

        if (card_began == 0) card_began = surface_os_time();
        r = surface_os_media_submit(card_bridge, io);

        if (r == SURFACE_MEDIA_BUSY) return 0;
        if (r != SURFACE_MEDIA_OK) {
            io->result = r;
            b->card_done = token;
            return 1;
        }
        card_submitted = token;
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
    b->card_done = token;
    return 1;
}

/* Program counters of the preempted desktop thread, one per kernel turn. */
static uint32_t samples[SAMPLE_SLOTS];

uint64_t surface_desktop_now_us(void) { return surface_os_time(); }

/** @brief Whether the kernel has finished the desktop's card request. */
static int card_finished(void *context)
{
    (void)context;
    return surface_desktop_bridge.card_done == surface_desktop_bridge.card_token;
}

void surface_desktop_card(surface_media_io_t *io)
{
    surface_desktop_bridge_t *b = &surface_desktop_bridge;

    b->card = io;
    b->card_token++;
    surface_os_desktop_kick();
    while (!card_finished(NULL)) surface_os_desktop_wait(card_finished, NULL, 1000000u);
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
    surface_desktop_bridge_t *b = &surface_desktop_bridge;

    if (!rt.running || length == 0 || length >= sizeof b->hash_path ||
        b->hash_token != b->hash_done) return 0;
    memcpy(b->hash_path, path, length);
    b->hash_path[length] = '\0';
    b->hash_token++;
    surface_os_desktop_wake();
    return 1;
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
 *  starts the measured delay until a frame taken after it is on the panel. */
static void post(tiku_event_type_t type, int x, int y, uint64_t now)
{
    surface_desktop_bridge_t *b = &surface_desktop_bridge;
    unsigned head = b->event_head;

    if (head - b->event_tail >= SURFACE_DESKTOP_EVENTS) return;
    if (type == TIKU_EVENT_POINTER_DOWN) rt.input_at = now;
    b->events[head % SURFACE_DESKTOP_EVENTS] = (tiku_input_event_t){
        .type = type, .button = 1, .x = x, .y = y,
        .time_us = now > INT64_MAX ? INT64_MAX : (int64_t)now };
    b->event_head = head + 1u;
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
    uint32_t px = rx < m->x_min ? 0u : rx > m->x_max ? m->x_max - m->x_min : rx - m->x_min;
    uint32_t py = ry < m->y_min ? 0u : ry > m->y_max ? m->y_max - m->y_min : ry - m->y_min;
    uint32_t dx = m->x_max - m->x_min, dy = m->y_max - m->y_min;

    if (m->swap) {
        uint32_t t = px; px = py; py = t;
        t = dx; dx = dy; dy = t;
    }
    *x = (int)((px * 799u + dx / 2u) / dx);
    *y = (int)((py * 479u + dy / 2u) / dy);
    if (m->invert_x) *x = 799 - *x;
    if (m->invert_y) *y = 479 - *y;
}

/** @brief One touch report as pointer down, move and up events. */
static int touch(const surface_touch_report_t *r, uint64_t now)
{
    unsigned i, primary;

    rt.input.reports++;
    if (r->cancel || r->count > SURFACE_TOUCH_CONTACTS) {
        rt.input.errors++;
        if (rt.touch_active) {
            post(TIKU_EVENT_POINTER_UP, rt.touch_x, rt.touch_y, now);
            rt.touch_active = 0;
            return 1;
        }
        return 0;
    }
    rt.input.contacts = r->count;
    rt.last_report = now;
    if (r->count == 0u) {
        if (!rt.touch_active) return 0;
        post(TIKU_EVENT_POINTER_UP, rt.touch_x, rt.touch_y, now);
        rt.touch_active = 0;
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
    {
        int x, y;

        map_point(r->points[primary].x, r->points[primary].y, &x, &y);
        if (!rt.touch_active) {
            rt.touch_active = 1;
            rt.touch_id = r->points[primary].id;
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
    }
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

static const surface_scanout_ops_t scanout = { scan_submit, scan_poll };

/** @brief The presentation lease ends: the frame is on the panel, or failed. */
int tiku_framebuffer_complete(tiku_framebuffer_t *owner, uint64_t token,
                              tiku_present_result_t result)
{
    surface_desktop_bridge_t *b = &surface_desktop_bridge;
    uint64_t now = surface_os_time();

    if (owner != &rt.owner || token != rt.presenting) return 0;
    rt.presenting = 0;
    b->frame_result = result == TIKU_PRESENT_OK ? 0 : -1;
    b->done_token = token;
    b->frames++;
    rt.frame.completed++;
    rt.frame.frame_us = (uint32_t)(now - rt.frame_started);
    if (rt.frame.frame_us > rt.frame.max_frame_us) rt.frame.max_frame_us = rt.frame.frame_us;
    if (rt.input_at != 0u && rt.frame_started >= rt.input_at) {
        rt.input_us = (uint32_t)(now - rt.input_at);
        rt.input_at = 0;
    }
    surface_os_desktop_wake();
    return 1;
}

/** @brief Print one row of the front buffer as runs of count and RGB565 colour. */
static void capture_row(void)
{
    const uint16_t *row = rt.rgb[rt.presentation.front] + rt.capture_row * SURFACE_DESKTOP_W;
    static char line[16 + SURFACE_DESKTOP_W * 7];
    int at = snprintf(line, sizeof line, "F%03u ", rt.capture_row);
    unsigned x = 0;

    while (x < SURFACE_DESKTOP_W) {
        unsigned run = 1;

        while (x + run < SURFACE_DESKTOP_W && run < 255u && row[x + run] == row[x]) run++;
        at += snprintf(line + at, sizeof line - (size_t)at, "%02x%04x", run, row[x]);
        x += run;
    }
    line[at++] = '\n';
    surface_os_desktop_print(line, (size_t)at);
    if (++rt.capture_row == SURFACE_DESKTOP_H) {
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
    rt.capture_row = 0;
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
    surface_desktop_bridge_t *b = &surface_desktop_bridge;
    const tiku_surface_t *s = b->frame;
    uint64_t token = b->frame_token;
    tiku_present_view_t view;

    if (rt.presenting || rt.capturing || s == NULL || token == b->done_token) return;
    view = (tiku_present_view_t){
        .pixels = s->px, .bytes = (size_t)s->w * (size_t)s->h * 4u,
        .stride = (size_t)s->w * 4u, .width = s->w, .height = s->h,
        .format = TIKU_PIXELS_XRGB8888, .damage = b->frame_damage,
        .token = token, .owner = &rt.owner };
    rt.frame.requested++;
    rt.frame_started = surface_os_time();
    rt.presenting = token;
    if (surface_presentation_submit(&rt.presentation, &view) != TIKU_PRESENT_OK) {
        rt.presenting = 0;
        b->frame_result = -1;
        b->done_token = token;
        surface_os_desktop_wake();
    }
}

static void desktop_thread(void *arg)
{
    static char *argv[] = { "TikuSurface", NULL };
    int code;

    (void)arg;
    surface_desktop_selftest();
    code = surface_desktop_main(1, argv);
    surface_desktop_bridge.exit_code = code;
    surface_desktop_bridge.exited = 1;
    surface_os_desktop_kick();
}

int surface_runtime_start(void)
{
    unsigned char *span, *stack, *heap;
    unsigned char record[SURFACE_SETTINGS_BYTES];
    size_t length = 0;

    clear_statics();
    if (rt.running) return 1;
    span = surface_os_reserve(SURFACE_RUNTIME_BYTES);
    if (span == NULL) return 0;
    memset(&surface_desktop_bridge, 0, sizeof surface_desktop_bridge);
    memset(&rt, 0, sizeof rt);
    rt.sample_dump = UINT32_MAX;
    rt.stacks_dump = UINT32_MAX;
    rt.span = span;
    rt.rgb[0] = (uint16_t *)span;
    rt.rgb[1] = (uint16_t *)(span + RGB_BYTES);
    stack = span + 2u * RGB_BYTES;
    heap = stack + STACK_BYTES;
    rt.stack = (uint32_t *)(void *)stack;
    for (uint32_t i = 0; i < STACK_BYTES / 4u; i++) rt.stack[i] = STACK_PAINT;
    memset(rt.rgb[0], 0, 2u * RGB_BYTES);
    surface_desktop_heap(heap, SURFACE_RUNTIME_BYTES - 2u * RGB_BYTES - STACK_BYTES);
    card_submitted = 0;
    if (surface_os_media_size() <= sizeof card_bridge) {
        surface_os_media_init(card_bridge);
        surface_desktop_bridge.card_mounted = surface_os_card_mount() == 0;
    }
    if (surface_desktop_files() != 0) return 0;
    if (surface_desktop_random_seed() != 0) {
        static const char line[] = "TikuSurface: the hardware random source gave no seed\n";

        surface_os_desktop_print(line, sizeof line - 1u);
    }
    rt.map = (surface_touch_map_t){0, 799, 0, 479, 0, 0, 0};
    rt.map_status = SURFACE_SETTINGS_DEFAULT;
    if (surface_os_settings_read(record, sizeof record, &length) &&
        surface_settings_decode(record, (unsigned)length, &rt.map) == SURFACE_SETTINGS_SAVED) {
        rt.map_status = SURFACE_SETTINGS_SAVED;
    }
    if (!surface_os_display_open(rt.rgb[0], rt.rgb[1]) ||
        !surface_presentation_init(&rt.presentation, rt.rgb[0], RGB_BYTES, rt.rgb[1],
                                   RGB_BYTES, 0, 4096, &rt.sequence, &scanout, NULL)) {
        rt.failed = 1;
        return 0;
    }
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

        if (surface_os_input_poll(&report)) {
            woke |= touch(&report, begin);
        } else {
            rt.input.errors++;
        }
        if (rt.touch_active && begin - rt.last_report >= TOUCH_EXPIRE_US) {
            post(TIKU_EVENT_POINTER_UP, rt.touch_x, rt.touch_y, begin);
            rt.touch_active = 0;
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
        if (polled != TIKU_PRESENT_BUSY) {
            uint64_t t = surface_os_time();

            if (rt.phase_seen != 0u && rt.phase_seen < 4u)
                rt.phase_us[rt.phase_seen] = (uint32_t)(t - rt.phase_at);
            rt.phase_seen = 0;
            break;
        }
    }
    while (card_pending() && surface_os_time() - begin < PRESENT_TURN_US + CARD_TURN_US) {
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
        capture_row();
    } else if (rt.sample_dump != UINT32_MAX) {
        sample_line();
    } else if (rt.stacks_dump != UINT32_MAX) {
        stack_line();
    } else {
        drain_log();
    }
    /* The stack grows down: the first word above the bottom that no longer
     * holds the paint marks the deepest use.  A pass covers the whole stack
     * over several turns and then starts again. */
    if (rt.stack != NULL) {
        uint32_t end = rt.stack_scan + STACK_SCAN_WORDS;

        if (end > STACK_BYTES / 4u) end = STACK_BYTES / 4u;
        /* Word 0 holds the thread's canary, not paint. */
        if (rt.stack_scan == 0u) rt.stack_scan = 1u;
        while (rt.stack_scan < end && rt.stack[rt.stack_scan] == STACK_PAINT) rt.stack_scan++;
        if (rt.stack_scan < end || end == STACK_BYTES / 4u) {
            uint32_t used = STACK_BYTES - rt.stack_scan * 4u;

            if (used > rt.stack_used) rt.stack_used = used;
            rt.stack_scan = 0;
        }
    }
    if (!rt.capturing && rt.sample_dump == UINT32_MAX && rt.stacks_dump == UINT32_MAX &&
        begin - rt.last_status >= STATUS_US) {
        char line[480];
        int n;

        size_t used = 0, total = 0, largest = 0;
        uint32_t refused = 0, console = 0, files = 0, calls = 0;
        uint64_t mutations = 0;
        const char *last = "";

        rt.last_status = begin;
        surface_desktop_heap_use(&used, &total);
        surface_desktop_heap_refused(&refused, &largest);
        surface_desktop_write_stats(&console, &files, &calls, &last);
        (void)tiku_posix_mutations(&mutations);
        n = snprintf(line, sizeof line,
                     "desktop: turns=%lu frames=%lu waits=%lu events=%u/%u present=%lu/%lu phase=%u max_us=%lu"
                     " heap=%lu/%lu refused=%lu/%lu stack=%lu/%lu writes=%lu/%lu/%lu:%.24s"
                     " up_ms=%lu frame_us=%lu/%lu input_us=%lu phases=%lu/%lu/%lu px=%lu convert_cpu=%lu"
                     " card=%lu/%lu/%lu/%lu/%lu mutations=%lu\n",
                     (unsigned long)rt.turns, (unsigned long)b->frames, (unsigned long)b->waits,
                     b->event_head, b->event_tail, (unsigned long)b->frame_token,
                     (unsigned long)b->done_token, rt.presentation.phase,
                     (unsigned long)rt.max_turn_us, (unsigned long)used, (unsigned long)total,
                     (unsigned long)refused, (unsigned long)largest,
                     (unsigned long)rt.stack_used, (unsigned long)STACK_BYTES,
                     (unsigned long)console, (unsigned long)files, (unsigned long)calls, last,
                     (unsigned long)(begin / 1000u), (unsigned long)rt.frame.frame_us,
                     (unsigned long)rt.frame.max_frame_us, (unsigned long)rt.input_us,
                     (unsigned long)rt.phase_us[1], (unsigned long)rt.phase_us[2],
                     (unsigned long)rt.phase_us[3], (unsigned long)rt.region_px,
                     (unsigned long)rt.convert_cpu_last, (unsigned long)card_requests,
                     (unsigned long)card_polls, (unsigned long)card_last_us, (unsigned long)card_max_us,
                     (unsigned long)(card_total_us / 1000u), (unsigned long)mutations);
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
    surface_desktop_bridge_t *b = &surface_desktop_bridge;
    uint64_t now = surface_os_time();
    /* Conversion and submission continue at once; a frame waiting on the
     * panel's DMA, or a burst past its bound, waits for the timer turn. */
    int work = rt.running && (rt.capturing || rt.sample_dump != UINT32_MAX ||
        rt.stacks_dump != UINT32_MAX || card_pending() ||
        (rt.presenting != 0 && rt.presentation.phase >= 1u && rt.presentation.phase <= 2u) ||
         (rt.presenting == 0 && b->frame != NULL && b->frame_token != b->done_token));

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
    rt.map_status = SURFACE_SETTINGS_PENDING;
    if (surface_settings_encode(map, record) &&
        surface_os_settings_write(record, sizeof record)) {
        rt.map_status = SURFACE_SETTINGS_SAVED;
    } else {
        rt.map_status = SURFACE_SETTINGS_FAILED;
    }
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
    surface_desktop_heap_use(&used, &total);
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
