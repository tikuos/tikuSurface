/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * camera.c - Camera: the SC101IOT's picture live in a window of the
 * desktop's workspace; Take photo encodes the frame shown and a worker saves
 * it in ~/Pictures; Photos shows the last one in TikuImage's viewer.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "camera.h"
#include "bridge.h"
#include "../platform/tikuos/camera.h"
#include "tiku_dialog.h"
#include "tiku_font.h"
#include "tiku_model.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include "tiku_work.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* A photo at quality 80 of a 1280 x 720 frame is 60-200 KB. */
#define PHOTO_CAP 524288u
#define PHOTO_QUALITY 80u
enum { CAM_SHOT = 1, CAM_PHOTOS };

/** @brief Camera's state; only the desktop's loop touches it. */
typedef struct {
    tiku_workspace_t *workspace;
    tiku_window_t *window;
    int *dirty;
    void (*review)(const char *path);
    tiku_dialog_t dialog;
    tiku_rect_t viewport;
    uint8_t *frame;         /* the UYVY frame, while the sensor is open */
    uint8_t *photo;         /* the JPEG being encoded */
    int asked;              /* the request the kernel runs, or 0 */
    int open;               /* the sensor runs */
    int shown;              /* the frame holds a whole picture */
    int unpainted;          /* ... not yet drawn */
    int shoot, saving, closing, failed;
    unsigned frames;
    char status[160], last[TIKU_PATH_MAX];
} camera_t;

/** @brief A photo on its way to the card: written by a worker. */
typedef struct {
    uint8_t *jpeg;
    uint32_t len;
    char path[TIKU_PATH_MAX], error[96];
} save_t;

static camera_t cam;

static void note(const char *text)
{
    snprintf(cam.status, sizeof cam.status, "%s", text);
    if (cam.dirty) *cam.dirty = 1;
}

/** @brief Hand the kernel request @p kind; it is the one running after. */
static void ask(int kind)
{
    if (surface_desktop_camera(kind, cam.frame, cam.photo, PHOTO_CAP, PHOTO_QUALITY) == 0)
        cam.asked = kind;
}

/** @brief On a worker: make ~/Pictures if need be, name the photo by the
 *  time with a number after it when that name is taken, and write it. */
static void save_run(void *ctx)
{
    save_t *job = ctx;
    const char *home = getenv("HOME");
    char dir[TIKU_PATH_MAX];
    struct tm when;
    struct stat st;
    time_t now = time(NULL);
    FILE *f;

    snprintf(dir, sizeof dir, "%s/Pictures", home && home[0] ? home : "/home");
    (void)mkdir(dir, 0755);
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        snprintf(job->error, sizeof job->error, "~/Pictures could not be made");
        return;
    }
    (void)localtime_r(&now, &when);
    job->path[0] = 0;
    for (int k = 1; k < 100 && !job->path[0]; k++) {
        int n = snprintf(job->path, sizeof job->path, "%s/photo-%04d%02d%02d-%02d%02d%02d%s%.0d.jpg",
                         dir, when.tm_year + 1900, when.tm_mon + 1, when.tm_mday, when.tm_hour,
                         when.tm_min, when.tm_sec, k > 1 ? "-" : "", k > 1 ? k : 0);
        if (n < 0 || (size_t)n >= sizeof job->path || lstat(job->path, &st) == 0)
            job->path[0] = 0;
    }
    if (!job->path[0]) {
        snprintf(job->error, sizeof job->error, "No free name for the photo");
        return;
    }
    f = fopen(job->path, "wb");
    if (!f || fwrite(job->jpeg, 1, job->len, f) != job->len) {
        snprintf(job->error, sizeof job->error, "The photo was not saved (%s)", strerror(errno));
        if (f) (void)fclose(f);
        (void)remove(job->path);
        return;
    }
    if (fclose(f) != 0) {
        snprintf(job->error, sizeof job->error, "The photo was not saved (%s)", strerror(errno));
        (void)remove(job->path);
    }
}

/** @brief On the loop: say where the photo went, or why it did not. */
static void save_done(void *ctx)
{
    save_t *job = ctx;
    const char *name = strrchr(job->path, '/');
    char text[160];

    cam.saving = 0;
    if (job->error[0]) {
        note(job->error);
        return;
    }
    snprintf(cam.last, sizeof cam.last, "%s", job->path);
    snprintf(text, sizeof text, "Saved %.100s, %lu KB.", name ? name + 1 : job->path,
             (unsigned long)(job->len + 1023u) / 1024u);
    note(text);
}

static void save_free(void *ctx)
{
    save_t *job = ctx;

    free(job->jpeg);
    free(job);
    cam.saving = 0;
}

/** @brief Hand the encoded photo, @p len bytes, to a worker to save. */
static void save(uint32_t len)
{
    save_t *job = calloc(1, sizeof *job);
    tiku_work_job_t work = {save_run, save_done, save_free, job, "camera-save"};

    if (!job) {
        free(cam.photo);
        cam.photo = NULL;
        note("Not enough memory to save the photo.");
        return;
    }
    job->jpeg = cam.photo;
    job->len = len;
    cam.photo = NULL;
    if (tiku_work_post(&work) != 0) {
        free(job->jpeg);
        free(job);
        note("The photo could not be queued for saving.");
        return;
    }
    cam.saving = 1;
    note("Saving the photo...");
}

/** @brief Apply the result @p rc of the request that ran, @p len for an
 *  encode. */
static void answered(int rc, uint32_t len)
{
    int kind = cam.asked;

    cam.asked = 0;
    switch (kind) {
    case SURFACE_CAM_OPEN:
        cam.open = rc == SURFACE_CAMERA_OK;
        cam.failed = !cam.open;
        note(cam.open ? "" : "No camera answers on the board's camera port.");
        break;
    case SURFACE_CAM_FRAME:
        if (rc == SURFACE_CAMERA_OK) {
            cam.shown = cam.unpainted = 1;
            cam.frames++;
            if (cam.dirty) *cam.dirty = 1;
        } else {
            note("The camera sent no whole frame.");
        }
        break;
    case SURFACE_CAM_ENCODE:
        if (rc == SURFACE_CAMERA_OK) {
            save(len);
        } else {
            free(cam.photo);
            cam.photo = NULL;
            note(rc == SURFACE_CAMERA_ROOM ? "The photo did not fit in 512 KB." :
                 "The photo could not be encoded.");
        }
        break;
    case SURFACE_CAM_CLOSE:
        cam.open = 0;
        free(cam.frame);
        cam.frame = NULL;
        cam.shown = cam.unpainted = 0;
        cam.closing = 0;
        break;
    default:
        break;
    }
}

void surface_camera_tick(int64_t now)
{
    uint32_t len = 0;

    (void)now;
    if (cam.asked) {
        int rc = surface_desktop_camera_result(&len);

        if (rc == SURFACE_CAMERA_BUSY) return;
        answered(rc, len);
    }
    if (cam.closing) {
        if (cam.open) {
            ask(SURFACE_CAM_CLOSE);
        } else {
            free(cam.frame);
            cam.frame = NULL;
            cam.shown = cam.unpainted = 0;
            cam.closing = 0;
        }
        return;
    }
    if (!cam.window || !cam.frame || cam.failed) return;
    if (!cam.open) {
        ask(SURFACE_CAM_OPEN);
        return;
    }
    if (cam.shoot && cam.shown && !cam.saving) {
        cam.shoot = 0;
        cam.photo = aligned_alloc(64, PHOTO_CAP);
        if (!cam.photo) {
            note("Not enough memory for the photo (512 KB).");
            return;
        }
        note("Taking the photo...");
        ask(SURFACE_CAM_ENCODE);
        return;
    }
    if (!cam.unpainted && !cam.window->minimized) ask(SURFACE_CAM_FRAME);
}

/** @brief Lay out the buttons under the view and the status line under
 *  them; drawing and input both use it. */
static void layout(tiku_rect_t r)
{
    int bh = tiku_ui_button_h(), fh = tiku_font_plain()->height + 6;
    int y = r.y + r.h - fh - bh - 8, x = r.x + 6;
    int shot = tiku_ui_button_w("Take photo", 96), photos = tiku_ui_button_w("Photos", 72);

    tiku_dialog_begin(&cam.dialog, CAM_SHOT, -1);
    (void)tiku_dialog_add(&cam.dialog, CAM_SHOT, (tiku_rect_t){x, y, shot, bh},
                          TIKU_DIALOG_BUTTON, cam.open && !cam.photo && !cam.saving);
    (void)tiku_dialog_add(&cam.dialog, CAM_PHOTOS, (tiku_rect_t){x + shot + 6, y, photos, bh},
                          TIKU_DIALOG_BUTTON, cam.last[0] != 0);
    tiku_dialog_end(&cam.dialog);
    cam.viewport = (tiku_rect_t){r.x + 4, r.y + 4, r.w - 8, y - r.y - 10};
    if (cam.viewport.w < 1) cam.viewport.w = 1;
    if (cam.viewport.h < 1) cam.viewport.h = 1;
}

/** @brief Paint the frame fitted into the view, converted from UYVY (JFIF
 *  full range) row by row. */
static void paint_frame(tiku_surface_t *s)
{
    static uint32_t row[SURFACE_DESKTOP_W];
    tiku_rect_t v = cam.viewport;
    int dw = v.w, dh = (int)((long)v.w * SURFACE_CAMERA_H / SURFACE_CAMERA_W);

    if (dh > v.h) {
        dh = v.h;
        dw = (int)((long)v.h * SURFACE_CAMERA_W / SURFACE_CAMERA_H);
    }
    if (dw > SURFACE_DESKTOP_W) dw = SURFACE_DESKTOP_W;
    int ox = v.x + (v.w - dw) / 2, oy = v.y + (v.h - dh) / 2;
    for (int y = 0; y < dh; y++) {
        const uint8_t *line = cam.frame + (size_t)(y * (long)SURFACE_CAMERA_H / dh) *
                              SURFACE_CAMERA_W * 2u;
        for (int x = 0; x < dw; x++) {
            unsigned sx = (unsigned)(x * (long)SURFACE_CAMERA_W / dw);
            const uint8_t *pair = line + (sx & ~1u) * 2u;
            int luma = pair[(sx & 1u) ? 3 : 1], u = pair[0] - 128, v2 = pair[2] - 128;
            int r = luma + ((359 * v2) >> 8), g = luma - ((88 * u + 183 * v2) >> 8),
                b = luma + ((454 * u) >> 8);
            r = r < 0 ? 0 : r > 255 ? 255 : r;
            g = g < 0 ? 0 : g > 255 ? 255 : g;
            b = b < 0 ? 0 : b > 255 ? 255 : b;
            row[x] = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
        }
        tiku_picture(s, ox, oy + y, dw, 1, row);
    }
}

static void draw(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r, void *ctx)
{
    tiku_rect_t clip = s->clip;
    int fh = tiku_font_plain()->height + 6;
    char info[200];

    (void)w; (void)ctx;
    tiku_clip_set(s, r);
    tiku_ui_panel(s, r);
    layout(r);
    for (int i = 0; i < cam.dialog.count; i++) {
        const tiku_dialog_control_t *d = &cam.dialog.control[i];
        unsigned flags = d->enabled ? 0 : TIKU_S_DISABLED;

        if (tiku_dialog_pressed(&cam.dialog, d->id)) flags |= TIKU_S_PRESSED;
        tiku_ui_button(s, d->rect, d->id == CAM_SHOT ? "Take photo" : "Photos", flags);
    }
    tiku_fill(s, cam.viewport, 0xFF101010u);
    if (cam.shown && cam.frame) {
        paint_frame(s);
        cam.unpainted = 0;
    } else {
        tiku_text_line_draw(s, tiku_inset(cam.viewport, 12), 0,
            cam.failed ? "The camera is not available." :
            cam.frame ? "Starting the camera..." : "Not enough memory for the camera (1.8 MB).",
            0xFFD0D0D0u);
    }
    if (cam.status[0]) snprintf(info, sizeof info, "%s", cam.status);
    else snprintf(info, sizeof info, "1280 x 720, photos go to ~/Pictures.");
    tiku_text_line_draw(s, (tiku_rect_t){r.x + 8, r.y + r.h - fh - 2, r.w - 16, fh}, 0, info,
                        TIKU_C_TEXT);
    s->clip = clip;
}

static int event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    tiku_dialog_result_t res;

    (void)ctx;
    layout(tiku_window_content(w));
    res = tiku_dialog_event(&cam.dialog, ev);
    if (res.activated == CAM_SHOT && cam.open && !cam.photo && !cam.saving) cam.shoot = 1;
    if (res.activated == CAM_PHOTOS && cam.last[0] && cam.review) cam.review(cam.last);
    if (res.taken && cam.dirty) *cam.dirty = 1;
    return res.taken;
}

/** @brief The window went away: stop the sensor once its request ends, and
 *  free the frame then. */
static void gone(tiku_window_t *w, void *ctx)
{
    (void)w; (void)ctx;
    cam.window = NULL;
    cam.shoot = 0;
    cam.closing = 1;
}

tiku_window_t *surface_camera_open(tiku_workspace_t *workspace, tiku_rect_t area,
                                   int *dirty, void (*review)(const char *path))
{
    cam.workspace = workspace;
    cam.dirty = dirty;
    cam.review = review;
    if (!cam.window) {
        if (cam.closing) return NULL;
        cam.window = tiku_workspace_open(workspace, "Camera", tiku_inset(area, 2), draw,
                                         &cam, &cam);
        if (!cam.window) return NULL;
        tiku_window_set_handlers(cam.window, event, NULL, gone);
        tiku_dialog_init(&cam.dialog);
        cam.failed = 0;
        cam.status[0] = 0;
        if (!cam.frame) cam.frame = aligned_alloc(64, SURFACE_CAMERA_FRAME_BYTES);
    }
    tiku_workspace_activate(workspace, cam.window);
    if (dirty) *dirty = 1;
    return cam.window;
}
