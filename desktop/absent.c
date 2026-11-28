/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * absent.c - shell services that TikuOS does not have, answered as absent.
 *
 * TikuSurface runs the desktop in one firmware image: there are no
 * application processes, sockets, signals or development drivers.  Each
 * function here gives the answer the shell already handles for a host where
 * the service failed to start: no listener, no sessions, no reports, no
 * resource endpoint, no sound device.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_conduct.h"
#include "tiku_remote.h"
#include "tiku_report.h"
#include "tiku_resource_channel.h"
#include "tiku_resource_endpoint.h"
#include "tiku_sound.h"
#include "touch_preferences.h"
#include "tiku_watch.h"
#include "tiku_trk_desktop_private.h"
#include <stdio.h>
#include <string.h>

int tiku_remote_process_handle(const tiku_remote_session_t *session, long *pid)
{ (void)session; if (pid != NULL) *pid = -1; return -1; }
int tiku_remote_process_kill(int handle) { (void)handle; return -1; }

/* The development conductor: no listening path or terminal exists. */
int tiku_conduct_listen(tiku_conduct_t *c, const char *path, void *ctx,
                        tiku_conduct_inject_fn inject, tiku_conduct_pixel_fn pixel,
                        tiku_conduct_text_fn text)
{ (void)c; (void)path; (void)ctx; (void)inject; (void)pixel; (void)text; return -1; }
int tiku_conduct_adopt(tiku_conduct_t *c, int fd, void *ctx, tiku_conduct_inject_fn inject,
                       tiku_conduct_pixel_fn pixel, tiku_conduct_text_fn text)
{ (void)c; (void)fd; (void)ctx; (void)inject; (void)pixel; (void)text; return -1; }
int tiku_conduct_open_tty(const char *path, int baud) { (void)path; (void)baud; return -1; }
int tiku_conduct_poll(tiku_conduct_t *c) { (void)c; return 0; }
void tiku_conduct_require(tiku_conduct_t *c, const char *full_token, const char *query_token)
{ (void)c; (void)full_token; (void)query_token; }
void tiku_conduct_shutdown(tiku_conduct_t *c) { (void)c; }

/* Crash and stall reports: TikuOS reports faults on its console. */
void tiku_report_install(const char *program, const char *dir) { (void)program; (void)dir; }
void tiku_report_ask_install(void) {}
/* The control file's "report" asks; the report goes to the console. */
int surface_desktop_report_asked(void);
int tiku_report_asked(void) { return surface_desktop_report_asked(); }
int tiku_report_write(const char *why, void (*snapshot)(FILE *, void *), void *ctx,
                      char *out, size_t max)
{
    fprintf(stderr, "REPORT BEGIN %s\n", why != NULL ? why : "");
    if (snapshot != NULL) snapshot(stderr, ctx);
    fprintf(stderr, "REPORT END\n");
    if (out != NULL && max > 0u) snprintf(out, max, "console");
    return 0;
}
int tiku_report_newest(char *out, size_t max) { if (out != NULL && max > 0u) out[0] = '\0'; return -1; }
int tiku_report_list(char out[][512], int max) { (void)out; (void)max; return 0; }
int tiku_report_child(const char *program, long pid, int sig, int status, char *out, size_t max)
{ (void)program; (void)pid; (void)sig; (void)status; if (out != NULL && max > 0u) out[0] = '\0'; return 0; }

/* The stall watchdog: the firmware watchdog covers the board. */
void tiku_watch_install(int after_ms) { (void)after_ms; }
void tiku_watch_beat(void) {}
long tiku_watch_longest_ms(void) { return 0; }
void tiku_watch_longest_reset(void) {}

/* The resource endpoint for other processes: none can connect. */
/* An endpoint no process can reach: it exists, and accepts nobody. */
static long unreachable_endpoint;

tiku_resource_endpoint_t *tiku_resource_endpoint_open(const char *directory)
{ (void)directory; return (tiku_resource_endpoint_t *)&unreachable_endpoint; }
int tiku_resource_endpoint_accept(tiku_resource_endpoint_t *endpoint) { (void)endpoint; return -1; }
void tiku_resource_endpoint_close(tiku_resource_endpoint_t *endpoint) { (void)endpoint; }
int tiku_resource_endpoint_ready(int fd, unsigned frame_max) { (void)fd; (void)frame_max; return 0; }
int tiku_resource_endpoint_recover(const char *directory) { (void)directory; return 0; }
tiku_resource_channel_t *tiku_resource_channel_server(int fd, const char *session, uint64_t features,
    const tiku_resource_channel_ops_t *ops, void *host)
{ (void)fd; (void)session; (void)features; (void)ops; (void)host; return NULL; }
int tiku_resource_channel_step(tiku_resource_channel_t *channel, int timeout_ms)
{ (void)channel; (void)timeout_ms; return -1; }
int tiku_resource_channel_send(tiku_resource_channel_t *channel, const tiku_msg_t *m, int timeout_ms)
{ (void)channel; (void)m; (void)timeout_ms; return -1; }
void tiku_resource_channel_free(tiku_resource_channel_t *channel) { (void)channel; }
uint64_t tiku_resource_channel_features(const tiku_resource_channel_t *channel) { (void)channel; return 0; }
const char *tiku_resource_channel_session(const tiku_resource_channel_t *channel)
{ (void)channel; return ""; }
int tiku_resource_channel_limits(const tiku_resource_channel_t *channel,
                                 tiku_resource_protocol_limits_t *limits)
{ (void)channel; if (limits != NULL) memset(limits, 0, sizeof *limits); return -1; }

/* Sound: the shell's sound kit plays nothing without a host backend. */
const tiku_sound_backend_t *tiku_sound_host_backend(void) { return NULL; }

/* The lifecycle VFS service exports desktop state to other processes. */
/* The shell supplies its instance before opening the display. Its lifetime
 * bounds the host's access to the touch lens on the desktop thread. */
static desktop_t *panel_desktop;

void surface_desktop_pointer_source(int mouse)
{
    desktop_t *app = panel_desktop;
    if (!app) return;
    app->lens.used = SURFACE_TOUCH_MODE && !mouse && surface_touch_magnifier();
    if (mouse && app->lens.shown) {
        desktop_damage(app, app->lens.box);
        app->lens.shown = app->lens.held = app->lens.picking = 0;
        app->lens.clicked_at = 0;
    }
}

void surface_desktop_calibrate_touch(void)
{
    if (panel_desktop) desktop_calibrate_begin(panel_desktop);
}

int desktop_lifecycle_vfs_open(desktop_t *app) { (void)app; return 0; }
void desktop_lifecycle_vfs_prepare(desktop_t *app)
{
    panel_desktop = app;
#if SURFACE_TOUCH_MODE
    surface_touch_preferences_load();
#endif
}
void desktop_lifecycle_vfs_poll(desktop_t *app, uint64_t now) { (void)app; (void)now; }
void desktop_lifecycle_vfs_close(desktop_t *app) { if (panel_desktop == app) panel_desktop = NULL; }
int desktop_lifecycle_vfs_restarting(desktop_t *app) { (void)app; return 0; }

/* Screen grabs are written as host image files. */
void desktop_grab(desktop_t *app, int session, const tiku_msg_t *msg)
{ (void)app; (void)session; (void)msg; }
void desktop_shot_desk(desktop_t *app) { (void)app; }
