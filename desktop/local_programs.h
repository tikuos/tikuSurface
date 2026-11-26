/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * local_programs.h - local program transport across the desktop thread.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_LOCAL_PROGRAMS_H
#define SURFACE_LOCAL_PROGRAMS_H
#include <stdint.h>
#include <stddef.h>
#define SURFACE_PROGRAM_MTU 1560
#define SURFACE_PROGRAM_SLOTS 8
typedef struct {
    unsigned char up[SURFACE_PROGRAM_SLOTS][SURFACE_PROGRAM_MTU + 2];
    unsigned char down[SURFACE_PROGRAM_SLOTS][82];
} surface_program_buffers_t;

int surface_program_attach(surface_program_buffers_t *buffers);
void surface_program_detach(int generation);
int surface_program_read(int generation, void *buf, size_t size);
int surface_program_send(int generation, uint32_t type, const void *data, uint32_t size);
int surface_program_ready(void *ctx);
void surface_program_pump(void);
int surface_program_queue_status(char *out, size_t size);
void surface_program_reconnect(void);
int surface_basic_window_start(void);
void surface_basic_window_poll(void);
void surface_basic_window_stop(void);
int surface_basic_window_status(char *out, size_t max);
#endif
