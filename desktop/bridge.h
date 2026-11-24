/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * bridge.h - the desktop thread, the kernel runtime and their firmware calls.
 *
 * The desktop runs TikuDesktop's shell on a TikuOS worker thread.  The
 * kernel-side runtime owns the panel, touch and presentation; the two meet
 * through the input ring, the frame handoff and the log ring declared here.
 * The firmware calls both sides use are declared in bridge_os.h.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_DESKTOP_BRIDGE_H
#define SURFACE_DESKTOP_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#include "tiku_gfx.h"
#include "tiku_input.h"
#include "bridge_os.h"
#include "../platform/tikuos/media_io.h"

#define SURFACE_DESKTOP_W 800
#define SURFACE_DESKTOP_H 480
#define SURFACE_DESKTOP_EVENTS 64u
#define SURFACE_DESKTOP_LOG 4096u

/** @brief State shared by the desktop thread and the kernel runtime. */
typedef struct {
    tiku_input_event_t events[SURFACE_DESKTOP_EVENTS];
    volatile unsigned event_head, event_tail;   /* kernel writes head */
    const tiku_surface_t *volatile frame;       /* desktop hands a frame */
    tiku_rect_t frame_damage;
    volatile uint64_t frame_token, done_token;  /* done follows the panel */
    /* frame_token when the desktop took the last down event, set before
     * down_taken: a frame with a later token was drawn after the press. */
    volatile uint64_t down_token;
    volatile int down_taken;
    volatile int frame_result;
    char log[SURFACE_DESKTOP_LOG];
    volatile unsigned log_head, log_tail;       /* desktop writes head */
    volatile int exited, exit_code;
    volatile uint32_t frames, waits, turns;
    /* One SD card request at a time: the desktop points card at it and
     * bumps card_token; the kernel submits and polls it, then sets
     * card_done to that token. */
    surface_media_io_t *volatile card;
    volatile unsigned card_token, card_done;
    volatile int card_foreground;               /* the desktop thread asked */
    volatile int card_mounted;                  /* the kernel mounted it */
    char hash_path[64];                          /* a file to hash, or "" */
    volatile unsigned hash_token, hash_done;
    /* One touch map request at a time: the desktop sets map_kind, and for
     * a fit map_edge, and bumps map_token; the kernel answers in map_result
     * and sets map_done to that token. */
    volatile int map_kind;                      /* SURFACE_MAP_* */
    int map_edge[4];                            /* left, right, top, bottom */
    volatile int map_result;                    /* 0, or -1 when refused */
    volatile unsigned map_token, map_done;
} surface_desktop_bridge_t;

/** @brief What the desktop asks of the touch map. */
enum {
    SURFACE_MAP_FIT = 1,    /* correct it from the edges it now reports */
    SURFACE_MAP_KEEP,       /* store the map in force */
    SURFACE_MAP_REVERT      /* put the stored map back in force */
};

/** @brief The one bridge, owned by desktop/runtime.c. */
extern surface_desktop_bridge_t surface_desktop_bridge;

/* Desktop-thread side, desktop/host.c and desktop/posix.c. */
/** @brief Append bytes to the log ring; the desktop thread waits for room,
 *  any other caller drops what does not fit. */
void surface_desktop_log(const void *bytes, size_t length);
/** @brief On the desktop thread: hash the file the control file named, if
 *  any, and print its size and SHA-256 on the log. */
void surface_desktop_hash_poll(void);
/** @brief Run one SD card request on the kernel's media bridge and wait for
 *  it; @p io's result field holds the outcome. */
void surface_desktop_card(surface_media_io_t *io);
/** @brief Changes made to the card's names by another client since the
 *  desktop started, as the kernel's turns count them; any thread. */
unsigned surface_desktop_card_foreign(void);
/** @brief Monotonic microseconds since boot. */
uint64_t surface_desktop_now_us(void);

/** @brief The desktop runtime's memory, in bytes: PSRAM, the part below the
 *  kernel's tier, the tier and its bytes not reserved (0 when unknown, the
 *  last as of the latest status line), the runtime's span in the tier, and
 *  the span's parts with the stacks' deepest use. */
typedef struct {
    size_t psram, below, tier, spare;
    size_t span, frames, stack, stack_used, jobs, jobs_used, stages, heap;
} surface_desktop_memory_t;
/** @brief Fill @p m; any thread. */
void surface_desktop_memory(surface_desktop_memory_t *m);

#endif
