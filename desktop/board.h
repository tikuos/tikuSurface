/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * board.h - the board's own namespace as a Tracker device (desktop/board.c).
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_BOARD_H
#define SURFACE_BOARD_H
#include <stddef.h>

struct tiku_backend;

/** @brief A backend over the kernel's VFS, and in @p name the board's name
 *  (/sys/device/name, else "board") for its mount.  NULL when the manifest
 *  cannot be read. */
struct tiku_backend *surface_board_open(char *name, size_t max);

/** @brief Print to stderr, one "board:" line each, whether a backend opens,
 *  reads /sys/uptime within the monotonic clock, lists /sys and the root
 *  with distinct identities, and refuses a write to /sys/uptime. */
void surface_board_selftest(void);
#endif
