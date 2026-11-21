/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * console_io.h - caller-owned local shell queues without SDK types.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_CONSOLE_IO_H
#define SURFACE_CONSOLE_IO_H
#include <stddef.h>
/** @brief Caller reservation for console output and owned BASIC state; aligned to sixteen bytes. */
#define SURFACE_CONSOLE_BRIDGE_BYTES 4352u
/** @brief Return storage required for the aligned console bridge. */
size_t surface_os_console_size(void);
/** @brief Initialize caller storage before opening the console. */
void surface_os_console_init(void *);
/** @brief Claim the UART-backed shell and mirror its output; owner context only. */
int surface_os_console_open(void *);
/** @brief Cancel this console input and restore its previous output backend. */
int surface_os_console_close(void *);
/** @brief Copy at most 256 input bytes into the console injection queue. */
int surface_os_console_write(void *,const void *,size_t);
/** @brief Copy at most 256 output bytes and acknowledge accumulated loss. */
int surface_os_console_read(void *,void *,size_t,unsigned *);
/** @brief Discard queued injected input and submit Ctrl-C to the shell. */
int surface_os_console_interrupt(void *);
/** @brief Consume a bounded BASIC source snapshot; return 1, busy 0 or error -1. */
int surface_os_program_start(void *,const char *,size_t);
/** @brief Read the result belonging to this console, independent of other jobs. */
int surface_os_program_state(void *);
/** @brief Stop this console's BASIC job before retiring its stream storage. */
int surface_os_program_stop(void *);
#endif
