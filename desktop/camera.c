/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * camera.c - Camera: the SC101IOT's stream of pictures in a window of the
 * desktop's workspace, each converted into the desktop's pixels as it
 * arrives; Take photo captures a whole frame, encodes it and a worker saves
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
    void (*damage)(tiku_window_t *window, tiku_rect_t rect);
    tiku_dialog_t dialog;
    tiku_rect_t viewport;
    tiku_rect_t picture;    /* where a picture is drawn, inside the view */
    unsigned step;          /* frame pixels across, and picture lines down,
                             * to one pixel drawn: 2, 4, 8 or 16 */
    uint8_t *frame;         /* the stream's two halves, or a whole frame */
    uint8_t *photo;         /* the JPEG being encoded */
    int asked;              /* the request the kernel runs, or 0 */
    int open;               /* the sensor runs */
    int streaming;          /* pictures arrive in the frame's halves */
    uint32_t seen;          /* the picture number last drawn for */
    int shoot, saving, closing, failed;
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
 *  encode: a whole frame is encoded, and an encode saved, before the
 *  stream starts again. */
static void answered(int rc, uint32_t len)
{
    int kind = cam.asked;

    cam.asked = 0;
    switch (kind) {
    case SURFACE_CAM_OPEN:
        cam.open = rc == SURFACE_CAMERA_OK;
        cam.streaming = cam.open;
        cam.failed = !cam.open;
        cam.seen = 0;
        note(cam.open ? "" : "No camera answers on the board's camera port.");
        break;
    case SURFACE_CAM_FRAME:
        if (rc == SURFACE_CAMERA_OK) {
            ask(SURFACE_CAM_ENCODE);
            return;
        }
        free(cam.photo);
        cam.photo = NULL;
        note("The camera sent no whole frame.");
        ask(SURFACE_CAM_STREAM);
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
        ask(SURFACE_CAM_STREAM);
        break;
    case SURFACE_CAM_STREAM:
        cam.streaming = rc == SURFACE_CAMERA_OK;
        cam.seen = 0;
        if (!cam.streaming) note("The camera did not start again.");
        break;
    case SURFACE_CAM_CLOSE:
        cam.open = cam.streaming = 0;
        free(cam.frame);
        cam.frame = NULL;
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
        if (cam.asked) return;
    }
    if (cam.closing) {
        if (cam.open) {
            ask(SURFACE_CAM_CLOSE);
        } else {
            free(cam.frame);
            cam.frame = NULL;
            cam.closing = 0;
        }
        return;
    }
    if (!cam.window || !cam.frame || cam.failed) return;
    if (!cam.open) {
        ask(SURFACE_CAM_OPEN);
        return;
    }
    if (cam.shoot && cam.streaming && !cam.saving) {
        cam.shoot = 0;
        cam.photo = aligned_alloc(64, PHOTO_CAP);
        if (!cam.photo) {
            note("Not enough memory for the photo (512 KB).");
            return;
        }
        note("Taking the photo...");
        cam.streaming = 0;
        ask(SURFACE_CAM_FRAME);
        return;
    }
    if (cam.streaming && !cam.window->minimized) {
        uint32_t n = surface_desktop_camera_picture(NULL);

        if (n != cam.seen) {
            cam.seen = n;
            if (cam.damage) cam.damage(cam.window, cam.picture);
            else if (cam.dirty) *cam.dirty = 1;
        }
    }
}

/** @brief Lay out the buttons under the view, the status line beside
 *  them, and the picture in the view: the largest of 640 x 360, 320 x 180,
 *  160 x 90 and 80 x 45 that fits, centred; drawing and input both use
 *  it. */
static void layout(tiku_rect_t r)
{
    int bh = tiku_ui_button_h();
    int y = r.y + r.h - bh - 6, x = r.x + 6;
    int shot = tiku_ui_button_w("Take photo", 96), photos = tiku_ui_button_w("Photos", 72);
    tiku_rect_t v;

    tiku_dialog_begin(&cam.dialog, CAM_SHOT, -1);
    (void)tiku_dialog_add(&cam.dialog, CAM_SHOT, (tiku_rect_t){x, y, shot, bh},
                          TIKU_DIALOG_BUTTON, cam.streaming && !cam.photo && !cam.saving);
    (void)tiku_dialog_add(&cam.dialog, CAM_PHOTOS, (tiku_rect_t){x + shot + 6, y, photos, bh},
                          TIKU_DIALOG_BUTTON, cam.last[0] != 0);
    tiku_dialog_end(&cam.dialog);
    v = (tiku_rect_t){r.x + 4, r.y + 4, r.w - 8, y - r.y - 8};
    if (v.w < 1) v.w = 1;
    if (v.h < 1) v.h = 1;
    cam.viewport = v;
    cam.step = 2;
    while (cam.step < 16 && ((int)(SURFACE_CAMERA_W / cam.step) > v.w ||
                             (int)(SURFACE_CAMERA_H / cam.step) > v.h))
        cam.step *= 2;
    int pw = (int)(SURFACE_CAMERA_W / cam.step), ph = (int)(SURFACE_CAMERA_H / cam.step);
    cam.picture = (tiku_rect_t){v.x + (v.w - pw) / 2, v.y + (v.h - ph) / 2, pw, ph};
}

/** @brief The overlap of @p a and @p b; empty when none. */
static tiku_rect_t overlap(tiku_rect_t a, tiku_rect_t b)
{
    int x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
    int x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;

    return x1 > x0 && y1 > y0 ? (tiku_rect_t){x0, y0, x1 - x0, y1 - y0} : (tiku_rect_t){0, 0, 0, 0};
}

/** @brief Whether @p in lies inside @p out. */
static int inside(tiku_rect_t in, tiku_rect_t out)
{
    return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w &&
           in.y + in.h <= out.y + out.h;
}

/* Full-range BT.601 (JFIF): each chroma value's term in red, green and
 * blue, and a clamp to 0..255 for sums from -256 to 511. */
static int16_t red_v[256], green_u[256], green_v[256], blue_u[256];
static uint8_t clamped[768];

static void tables(void)
{
    if (clamped[767]) return;
    for (int i = 0; i < 256; i++) {
        int d = i - 128;

        red_v[i] = (int16_t)((359 * d) >> 8);
        green_u[i] = (int16_t)(88 * d);
        green_v[i] = (int16_t)(183 * d);
        blue_u[i] = (int16_t)((454 * d) >> 8);
    }
    for (int i = 0; i < 768; i++) clamped[i] = (uint8_t)(i < 256 ? 0 : i > 511 ? 255 : i - 256);
}

/** @brief Paint the part @p c of the picture rectangle from the newest
 *  picture: a picture line is every other frame line, and each pixel drawn
 *  takes one U Y V Y pair, its two lumas averaged, at 640 across; smaller
 *  pictures take every second, fourth or eighth pair and line.  Dark with a
 *  line of text while there is none.  @return 1 when the picture was
 *  painted */
static int paint_picture(tiku_surface_t *s, tiku_rect_t c)
{
    tiku_rect_t p = cam.picture;
    unsigned half = 0, pairs = cam.step / 2u, lines = cam.step / 2u;
    uint32_t n = cam.streaming ? surface_desktop_camera_picture(&half) : 0u;
    int stride = tiku_surface_stride(s);

    if (c.w <= 0 || c.h <= 0) return 0;
    if (n && cam.frame && !s->record && s->scale <= 1) {
        uint64_t t0 = surface_desktop_now_us();
        const uint8_t *picture = cam.frame + (size_t)half * SURFACE_CAMERA_W * SURFACE_CAMERA_PICTURE_H * 2u;

        tables();
        for (int y = c.y; y < c.y + c.h; y++) {
            const uint8_t *line = picture + (size_t)(y - p.y) * lines * SURFACE_CAMERA_W * 2u;
            const uint8_t *pair = line + (size_t)(c.x - p.x) * pairs * 4u;
            uint32_t *out = s->px + (size_t)y * (size_t)stride + c.x;

            for (int x = 0; x < c.w; x++, pair += pairs * 4u) {
                int luma = (pair[1] + pair[3] + 1) >> 1, u = pair[0], v = pair[2];

                out[x] = 0xFF000000u | (uint32_t)clamped[luma + red_v[v] + 256] << 16 |
                         (uint32_t)clamped[luma - ((green_u[u] + green_v[v]) >> 8) + 256] << 8 |
                         clamped[luma + blue_u[u] + 256];
            }
        }
        surface_desktop_camera_drawn((uint32_t)(surface_desktop_now_us() - t0));
        return 1;
    }
    tiku_fill(s, c, 0xFF101010u);
    tiku_text_line_draw(s, tiku_inset(cam.viewport, 12), 0,
        cam.failed ? "The camera is not available." :
        !cam.frame ? "Not enough memory for the camera (1.8 MB)." :
        cam.photo ? "Taking the photo..." : "Starting the camera...",
        0xFFD0D0D0u);
    return 0;
}

/** @brief Draw what of the window the clip holds: the buttons and the
 *  status line, the dark bands around the picture, and the picture.  A new
 *  picture's frame clips to the picture and draws it alone. */
static void draw(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r, void *ctx)
{
    tiku_rect_t clip = s->clip, p, v, bands[4];
    char info[200];

    (void)w; (void)ctx;
    layout(r);
    tiku_clip_set(s, r);
    p = cam.picture;
    v = cam.viewport;
    if (!inside(s->clip, v)) {
        tiku_ui_panel(s, r);
        for (int i = 0; i < cam.dialog.count; i++) {
            const tiku_dialog_control_t *d = &cam.dialog.control[i];
            unsigned flags = d->enabled ? 0 : TIKU_S_DISABLED;

            if (tiku_dialog_pressed(&cam.dialog, d->id)) flags |= TIKU_S_PRESSED;
            tiku_ui_button(s, d->rect, d->id == CAM_SHOT ? "Take photo" : "Photos", flags);
        }
        if (cam.status[0]) snprintf(info, sizeof info, "%s", cam.status);
        else snprintf(info, sizeof info, "1280 x 720, photos go to ~/Pictures.");
        if (cam.dialog.count > 1) {
            tiku_rect_t last = cam.dialog.control[cam.dialog.count - 1].rect;
            int tx = last.x + last.w + 12;

            tiku_text_line_draw(s, (tiku_rect_t){tx, last.y, r.x + r.w - 8 - tx, last.h}, 0,
                                info, TIKU_C_TEXT);
        }
    }
    bands[0] = (tiku_rect_t){v.x, v.y, v.w, p.y - v.y};
    bands[1] = (tiku_rect_t){v.x, p.y + p.h, v.w, v.y + v.h - p.y - p.h};
    bands[2] = (tiku_rect_t){v.x, p.y, p.x - v.x, p.h};
    bands[3] = (tiku_rect_t){p.x + p.w, p.y, v.x + v.w - p.x - p.w, p.h};
    for (int i = 0; i < 4; i++) {
        tiku_rect_t b = overlap(bands[i], s->clip);

        if (b.w > 0) tiku_fill(s, b, 0xFF101010u);
    }
    (void)paint_picture(s, overlap(p, s->clip));
    s->clip = clip;
}

static int event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    tiku_dialog_result_t res;

    (void)ctx;
    layout(tiku_window_content(w));
    res = tiku_dialog_event(&cam.dialog, ev);
    if (res.activated == CAM_SHOT && cam.streaming && !cam.photo && !cam.saving) cam.shoot = 1;
    if (res.activated == CAM_PHOTOS && cam.last[0] && cam.review) cam.review(cam.last);
    if (res.taken && cam.dirty) *cam.dirty = 1;
    return res.taken;
}

/** @brief The window went away: stop the stream and the sensor once the
 *  running request ends, and free the frame then. */
static void gone(tiku_window_t *w, void *ctx)
{
    (void)w; (void)ctx;
    cam.window = NULL;
    cam.shoot = 0;
    cam.closing = 1;
}

tiku_window_t *surface_camera_open(tiku_workspace_t *workspace, tiku_rect_t area, int *dirty,
                                   void (*damage)(tiku_window_t *window, tiku_rect_t rect),
                                   void (*review)(const char *path))
{
    cam.workspace = workspace;
    cam.dirty = dirty;
    cam.damage = damage;
    cam.review = review;
    if (!cam.window) {
        if (cam.closing) return NULL;
        cam.window = tiku_workspace_open(workspace, "Camera", tiku_inset(area, 2), draw,
                                         &cam, &cam);
        if (!cam.window) return NULL;
        tiku_window_set_handlers(cam.window, event, NULL, gone);
        tiku_window_set_opaque(cam.window, 1);
        tiku_dialog_init(&cam.dialog);
        cam.failed = 0;
        cam.status[0] = 0;
        if (!cam.frame) cam.frame = aligned_alloc(64, SURFACE_CAMERA_FRAME_BYTES);
    }
    tiku_workspace_activate(workspace, cam.window);
    if (dirty) *dirty = 1;
    return cam.window;
}
