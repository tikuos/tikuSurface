/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * terminal.h - native Terminal session and its kernel shell lease.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_TERMINAL_H
#define SURFACE_TERMINAL_H
#include <stddef.h>

struct tiku_remote_listener;
struct tiku_process;
void surface_terminal_bind(struct tiku_remote_listener *listener);
int surface_terminal_start(void);
int surface_terminal_poll(void);
void surface_terminal_stop(void);
int surface_terminal_claim(void *storage);
int surface_terminal_read(int handle, void *bytes, size_t len, unsigned *lost);
int surface_terminal_write(int handle, const void *bytes, size_t len);
int surface_terminal_interrupt(int handle);
void surface_terminal_release(int handle);
void surface_terminal_exited(const struct tiku_process *owner);
#endif
