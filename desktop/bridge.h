/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * bridge.h - the desktop thread, the kernel runtime and their firmware calls.
 *
 * The desktop runs TikuDesktop's shell on a TikuOS worker thread.  The
 * kernel-side runtime owns the panel, touch and presentation; the two meet
 * through kernel calls. The log ring carries output from the desktop.
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

#include "../system/product/surface_panel.h"
#define SURFACE_DESKTOP_EVENTS 64u
#define SURFACE_DESKTOP_LOG 4096u

/** @brief State shared by the desktop thread and the kernel runtime. */
typedef struct {
    char log[SURFACE_DESKTOP_LOG];
    volatile unsigned log_head, log_tail;       /* desktop writes head */
    volatile int exited, exit_code;
    volatile uint32_t frames, waits, turns;
    int card_mounted;                           /* fixed at desktop startup */
} surface_desktop_bridge_t;

/** @brief What the desktop asks of the touch map. */
enum {
    SURFACE_MAP_FIT = 1,    /* correct it from the edges it now reports */
    SURFACE_MAP_KEEP,       /* store the map in force */
    SURFACE_MAP_REVERT      /* put the stored map back in force */
};

/** @brief What the desktop asks of the camera. */
enum {
    SURFACE_CAM_OPEN = 1,   /* start the sensor and stream into cam_frame */
    SURFACE_CAM_FRAME,      /* capture one whole frame into cam_frame,
                             * ending the stream */
    SURFACE_CAM_ENCODE,     /* encode cam_frame into cam_out */
    SURFACE_CAM_STREAM,     /* stream into cam_frame again */
    SURFACE_CAM_CLOSE       /* stop the stream and the sensor */
};

/** @brief The one bridge, owned by desktop/runtime.c. */
extern surface_desktop_bridge_t surface_desktop_bridge;

/** @brief Present a damaged region and wait for the panel to release its pixels. */
int surface_desktop_present(const tiku_surface_t *surface, tiku_rect_t damage);
/** @brief Successful panel presentations since the desktop host opened, read on its thread. */
uint64_t surface_desktop_presented_frames(void);
/** @brief Input readiness predicate for the desktop wait queue. */
int surface_desktop_input_ready(void *unused);
/** @brief Set the shape requested by the shell's desktop host. */
void surface_desktop_cursor_shape(int shape);
/** @brief Select the shell's finger lens for touch events. Desktop thread only. */
void surface_desktop_pointer_source(int mouse);
/** @brief Take the next input event. Returns 2 for a mouse, 1 for another input,
 * zero when empty; consecutive moves from the same source share an event. */
int surface_desktop_input(tiku_input_event_t *event);
#define SURFACE_HASH_PATH_BYTES 64u

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
/** @brief On the desktop thread: hand the kernel camera request @p kind with
 *  its buffers.  @return 0 when handed, -1 while the last one runs. */
int surface_desktop_camera(int kind, void *frame, uint8_t *out, uint32_t cap,
                           unsigned quality);
/** @brief The last camera request's state: SURFACE_CAMERA_BUSY while it
 *  runs, otherwise its result and, for an encode, *@p len. */
int surface_desktop_camera_result(uint32_t *len);
/** @brief The newest streamed picture's number, from 1, and in *@p half the
 *  half of the frame that holds it, ready to read; 0 when none streams. */
uint32_t surface_desktop_camera_picture(unsigned *half);
/** @brief Count a picture drawn in @p us microseconds, for the status
 *  line. */
void surface_desktop_camera_drawn(uint32_t us);

/** @brief The desktop runtime's memory, in bytes: PSRAM, the part below the
 *  kernel's tier, the tier and its bytes not reserved (0 when unknown, the
 *  last as of the latest status line), the runtime's span in the tier, and
 *  the span's parts with the stacks' deepest use. */
typedef struct {
    size_t psram, below, tier, spare;
    size_t span, frames, stack, stack_used, jobs, jobs_used, stages, heap;
    size_t top_store;  /* file store reserved at the upper end of PSRAM */
} surface_desktop_memory_t;
/** @brief Fill @p m; any thread. */
void surface_desktop_memory(surface_desktop_memory_t *m);

#endif
