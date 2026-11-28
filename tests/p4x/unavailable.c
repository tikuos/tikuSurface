/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * unavailable.c - Optional device adapters in the volatile P4X desktop test.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "local_programs.h"
#include "terminal.h"
#include "bridge_os.h"
#include "camera.h"
#include "media_io.h"
#include <kernel/cpu/tiku_rtc.h>

/* Device adapters unavailable in this RAM session return their normal absence result. */
int tiku_media_card_mount(const uint32_t *cid, const uint8_t *uuid, uint32_t hz) { return -1; }
size_t surface_os_media_size(void) { return SIZE_MAX; }
void surface_os_media_init(void *s) {}
int surface_os_media_submit(void *s, surface_media_io_t *io) { return SURFACE_MEDIA_UNAVAILABLE; }
int surface_os_media_poll(void *s) { return 0; }
int surface_os_media_cancel(void *s) { return 1; }
int surface_os_media_idle(void *s) { return 0; }
unsigned surface_os_media_foreign(const void *s) { return 0; }
int surface_os_camera_open(void) { return SURFACE_CAMERA_ERROR; }
void surface_os_camera_close(void) {}
int surface_os_camera_capture(void *p) { return SURFACE_CAMERA_ERROR; }
int surface_os_camera_capture_poll(void) { return SURFACE_CAMERA_ERROR; }
int surface_os_camera_stream(void *p) { return SURFACE_CAMERA_ERROR; }
uint32_t surface_os_camera_newest(unsigned *half) { return 0; }
void surface_os_camera_drop(void *p, unsigned half) {}
int surface_os_camera_encode(void *p, unsigned q, uint8_t *o, uint32_t n) { return SURFACE_CAMERA_ERROR; }
int surface_os_camera_encode_poll(uint32_t *n) { return SURFACE_CAMERA_ERROR; }

int surface_os_form_access(const char *path) { return -1; }
int surface_os_form_write(const char *path, const void *p, size_t n) { return -1; }
int surface_terminal_claim(void *p) { return -1; }
int surface_terminal_read(int h, void *p, size_t n, unsigned *lost) { return -1; }
int surface_terminal_write(int h, const void *p, size_t n) { return -1; }
int surface_terminal_interrupt(int h) { return -1; }
void surface_terminal_release(int h) {}
int surface_program_ready(void *ctx) { return 0; }
int surface_program_attach(surface_program_buffers_t *b) { return -1; }
void surface_program_detach(int h) {}
int surface_program_read(int h, void *p, size_t n) { return -1; }
int surface_program_send(int h, uint32_t type, const void *p, uint32_t n) { return -1; }
void surface_program_pump(void) {}
void surface_basic_window_poll(void) {}
tiku_rtc_state_t tiku_rtc_read(uint32_t *v) { if (v) *v = 0; return TIKU_RTC_UNSET; }
tiku_rtc_state_t tiku_rtc_state(void) { return TIKU_RTC_UNSET; }
