/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_camera.c - Camera's window over a fake kernel bridge and job runner:
 * the stream started, a picture drawn for each that arrives, converted from
 * UYVY into the picture's rectangle alone, the shutter's capture, encode,
 * save in ~/Pictures and stream again, the failures, Photos, and closing
 * while a request runs.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../desktop/camera.c"
#include <assert.h>
#include <unistd.h>

/* The kernel's side of the bridge: one request at a time, answered by the
 * test; the newest picture's number and half. */
static int pending, kind_asked, answer = SURFACE_CAMERA_BUSY;
static unsigned asks, quality_asked, drawn;
static void *frame_asked;
static uint8_t *out_asked;
static uint32_t cap_asked, answer_len, picture_n;
static unsigned picture_half;
static uint64_t clock_us;

int surface_desktop_camera(int kind, void *frame, uint8_t *out, uint32_t cap, unsigned quality)
{
    if (pending) return -1;
    pending = 1;
    asks++;
    kind_asked = kind; frame_asked = frame; out_asked = out;
    cap_asked = cap; quality_asked = quality;
    answer = SURFACE_CAMERA_BUSY;
    return 0;
}

int surface_desktop_camera_result(uint32_t *len)
{
    if (answer != SURFACE_CAMERA_BUSY && len) *len = answer_len;
    return answer;
}

uint32_t surface_desktop_camera_picture(unsigned *half)
{
    if (half) *half = picture_half;
    return picture_n;
}

void surface_desktop_camera_drawn(uint32_t us) { (void)us; drawn++; }
uint64_t surface_desktop_now_us(void) { return clock_us; }

/** @brief The kernel finishes the request with @p rc, @p len for an encode. */
static void reply(int rc, uint32_t len)
{
    assert(pending);
    pending = 0;
    answer = rc;
    answer_len = len;
}

/* The job runner: the posted job, run when the test says. */
static tiku_work_job_t job;
static int jobs;
int tiku_work_post(const tiku_work_job_t *j)
{
    job = *j;
    jobs++;
    return 0;
}
static void run_job(void)
{
    job.run(job.ctx);
    job.done(job.ctx);
    job.free(job.ctx);
}

void tiku_text_line_draw(tiku_surface_t *s, tiku_rect_t r, int face, const char *text,
                         tiku_rgb_t ink)
{
    (void)s; (void)r; (void)face; (void)text; (void)ink;
}

static const char *reviewed;
static void review_photo(const char *path) { reviewed = path; }

/* The damage the window asked for. */
static unsigned damages;
static tiku_rect_t damaged;
static void damage(tiku_window_t *w, tiku_rect_t r)
{
    assert(w == cam.window);
    damages++;
    damaged = r;
}

/** @brief Full-range BT.601 as the painter computes it, 0x00RRGGBB. */
static uint32_t expect(int y, int u, int v)
{
    int r = y + ((359 * (v - 128)) >> 8), g = y - ((88 * (u - 128) + 183 * (v - 128)) >> 8),
        b = y + ((454 * (u - 128)) >> 8);
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

/** @brief The grey's first luma on picture line @p line; the second is 80
 *  more, so the pair's mean is 40 more. */
static int grey(uint32_t line) { return 60 + (int)(line % 32u) * 2; }

/** @brief Fill half @p half: each picture line's left half grey, its lumas
 *  changing with the line, its right half a colour of luma @p colour_y. */
static void fill_half(unsigned half, int colour_y)
{
    uint8_t *p = cam.frame + (size_t)half * SURFACE_CAMERA_W * SURFACE_CAMERA_PICTURE_H * 2u;

    for (uint32_t line = 0; line < SURFACE_CAMERA_PICTURE_H; line++)
        for (uint32_t x = 0; x < SURFACE_CAMERA_W; x += 2) {
            uint8_t *q = p + (line * SURFACE_CAMERA_W + x) * 2u;
            int right = x >= SURFACE_CAMERA_W / 2;

            q[0] = right ? 85 : 128;
            q[1] = right ? (uint8_t)colour_y : (uint8_t)grey(line);
            q[2] = right ? 255 : 128;
            q[3] = right ? (uint8_t)colour_y : (uint8_t)(grey(line) + 80);
        }
}

/** @brief Press and release @p button of the window. */
static void press(int button)
{
    for (int i = 0; i < cam.dialog.count; i++) {
        tiku_rect_t r = cam.dialog.control[i].rect;
        if (cam.dialog.control[i].id != button) continue;
        tiku_event_t down = {.type = TIKU_EVENT_POINTER_DOWN, .x = r.x + r.w / 2,
                             .y = r.y + r.h / 2, .button = 1};
        tiku_event_t up = down;
        up.type = TIKU_EVENT_POINTER_UP;
        (void)event(cam.window, &down, NULL);
        (void)event(cam.window, &up, NULL);
        return;
    }
    assert(0);
}

static tiku_rgb_t pixels[800 * 480];
static tiku_surface_t surface = {pixels, 800, 480, {0, 0, 800, 480}, 1, NULL, 0, {0, 0, 0, 0}, 0, 0};

/** @brief Draw the window's content with @p clip the damage in force, as
 *  the workspace draws a frame's damage. */
static void draw_clipped(tiku_rect_t clip)
{
    tiku_clip_set(&surface, clip);
    tiku_clip_damage(&surface, &clip);
    draw(cam.window, &surface, tiku_window_content(cam.window), &cam);
    tiku_clip_damage(&surface, NULL);
    tiku_clip_reset(&surface);
}

/** @brief Whether every third pixel of the picture shows its pair and line
 *  of the picture converted: the grey's mean luma on the left, luma
 *  @p colour_y in colour on the right. */
static int picture_shows(int colour_y)
{
    tiku_rect_t p = cam.picture;

    for (int y = p.y; y < p.y + p.h; y += 3)
        for (int x = p.x; x < p.x + p.w; x += 3) {
            unsigned pair = (unsigned)(x - p.x) * (cam.step / 2u);
            uint32_t line = (uint32_t)(y - p.y) * (cam.step / 2u);
            int right = pair * 2u >= SURFACE_CAMERA_W / 2u;
            uint32_t want = right ? expect(colour_y, 85, 255) : expect(grey(line) + 40, 128, 128);

            if ((tiku_peek(&surface, x, y) & 0xFFFFFFu) != want) return 0;
        }
    return 1;
}

int main(void)
{
    void *storage = calloc(1, tiku_workspace_size());
    tiku_workspace_t *ws = tiku_workspace_init(storage, tiku_workspace_size(), &surface, NULL);
    char home[] = "/tmp/surface-camera-XXXXXX", path[600];
    int dirty = 0;

    assert(ws && mkdtemp(home) && !setenv("HOME", home, 1));
    /* Opening asks the kernel to start the sensor and the stream; the
     * window paints all of its content. */
    assert(surface_camera_open(ws, (tiku_rect_t){0, 20, 800, 460}, &dirty, damage, review_photo));
    assert(cam.frame && dirty && !((uintptr_t)cam.frame & 63u) && cam.window->opaque);
    surface_camera_tick(0);
    assert(asks == 1 && kind_asked == SURFACE_CAM_OPEN && frame_asked == cam.frame);
    surface_camera_tick(0);
    assert(asks == 1);
    draw_clipped(tiku_window_content(cam.window));
    assert(cam.picture.w == 640 && cam.picture.h == 360 && cam.step == 2);
    tiku_rect_t v = cam.viewport, p = cam.picture;
    assert(p.x - v.x == v.x + v.w - p.x - p.w || p.x - v.x + 1 == v.x + v.w - p.x - p.w);
    assert(drawn == 0 && (tiku_peek(&surface, p.x + 5, p.y + 5) & 0xFFFFFFu) == 0x101010u);
    /* The view around the picture is dark too. */
    assert(p.x - v.x > 4 && (tiku_peek(&surface, v.x + 2, p.y + 5) & 0xFFFFFFu) == 0x101010u &&
           (tiku_peek(&surface, p.x + p.w + 2, p.y + p.h - 2) & 0xFFFFFFu) == 0x101010u);
    reply(SURFACE_CAMERA_OK, 0);
    surface_camera_tick(0);
    assert(cam.open && cam.streaming && damages == 0 && asks == 1);

    /* A picture arrives: its rectangle alone is drawn again, once. */
    fill_half(0, 76);
    fill_half(1, 200);
    picture_n = 1;
    picture_half = 0;
    surface_camera_tick(0);
    assert(damages == 1 && damaged.x == p.x && damaged.y == p.y && damaged.w == 640 &&
           damaged.h == 360);
    surface_camera_tick(0);
    assert(damages == 1);
    for (int i = 0; i < 800 * 480; i++) pixels[i] = 0xFF123456u;
    draw_clipped(p);
    assert(drawn == 1 && picture_shows(76));
    assert(tiku_peek(&surface, p.x - 1, p.y) == 0xFF123456u &&
           tiku_peek(&surface, p.x + p.w, p.y + p.h - 1) == 0xFF123456u &&
           tiku_peek(&surface, p.x, p.y + p.h) == 0xFF123456u &&
           tiku_peek(&surface, v.x, v.y + v.h + 2) == 0xFF123456u);
    /* The next picture, in the other half. */
    picture_n = 2;
    picture_half = 1;
    surface_camera_tick(0);
    assert(damages == 2);
    draw_clipped(p);
    assert(drawn == 2 && picture_shows(200));
    /* A part of the picture: that part converted, from the right place. */
    for (int i = 0; i < 800 * 480; i++) pixels[i] = 0xFF123456u;
    draw_clipped((tiku_rect_t){p.x + 300, p.y + 100, 40, 20});
    assert(tiku_peek(&surface, p.x + 300, p.y + 100) == (0xFF000000u | expect(grey(100) + 40, 128, 128)) &&
           tiku_peek(&surface, p.x + 339, p.y + 119) == (0xFF000000u | expect(200, 85, 255)) &&
           tiku_peek(&surface, p.x + 299, p.y + 100) == 0xFF123456u &&
           tiku_peek(&surface, p.x + 300, p.y + 120) == 0xFF123456u);
    /* Minimized, a picture draws nothing; without the damage call the
     * whole screen is asked for. */
    cam.window->minimized = 1;
    picture_n = 3;
    surface_camera_tick(0);
    assert(damages == 2);
    cam.window->minimized = 0;
    cam.damage = NULL;
    dirty = 0;
    surface_camera_tick(0);
    assert(dirty == 1 && damages == 2);
    cam.damage = damage;

    /* A smaller window: a quarter-size picture, every other pair and line. */
    cam.window->frame = (tiku_rect_t){20, 40, 420, 300};
    draw_clipped(tiku_window_content(cam.window));
    assert(cam.picture.w == 320 && cam.picture.h == 180 && cam.step == 4 && picture_shows(200));
    cam.window->frame = (tiku_rect_t){2, 22, 796, 456};
    draw_clipped(tiku_window_content(cam.window));
    p = cam.picture;
    assert(p.w == 640);

    /* The shutter: a whole frame, encoded at quality 80 into 512 KB, the
     * stream again, and a worker saves the photo in ~/Pictures. */
    press(CAM_SHOT);
    assert(cam.shoot);
    surface_camera_tick(0);
    assert(asks == 2 && kind_asked == SURFACE_CAM_FRAME && frame_asked == cam.frame &&
           cam.photo && !cam.streaming);
    /* The kernel reports no picture while it captures, and counts from 1
     * again when the stream restarts. */
    picture_n = 0;
    unsigned before = drawn;
    draw_clipped(p);
    assert(drawn == before && strstr(cam.status, "Taking the photo"));
    reply(SURFACE_CAMERA_OK, 0);
    surface_camera_tick(0);
    assert(asks == 3 && kind_asked == SURFACE_CAM_ENCODE && out_asked == cam.photo &&
           cap_asked == 524288u && quality_asked == 80u && frame_asked == cam.frame);
    memset(cam.photo, 0xA5, 1000);
    reply(SURFACE_CAMERA_OK, 1000);
    surface_camera_tick(0);
    assert(jobs == 1 && cam.saving && cam.photo == NULL && asks == 4 &&
           kind_asked == SURFACE_CAM_STREAM);
    run_job();
    assert(!cam.saving && cam.last[0] && strstr(cam.status, "Saved photo-") &&
           strstr(cam.status, "1 KB"));
    snprintf(path, sizeof path, "%s/Pictures/", home);
    assert(!strncmp(cam.last, path, strlen(path)) && strstr(cam.last, ".jpg") &&
           !strchr(cam.last + strlen(path), '/') && !strncmp(cam.last + strlen(path), "photo-", 6));
    FILE *f = fopen(cam.last, "rb");
    unsigned char back[1100];
    assert(f && fread(back, 1, sizeof back, f) == 1000 && back[0] == 0xA5 && back[999] == 0xA5);
    fclose(f);
    unsigned seen_damages = damages;
    reply(SURFACE_CAMERA_OK, 0);
    surface_camera_tick(0);
    assert(cam.streaming && cam.seen == 0 && damages == seen_damages);
    picture_n = 1;
    surface_camera_tick(0);
    assert(cam.seen == 1 && damages == seen_damages + 1u);
    press(CAM_PHOTOS);
    assert(reviewed == cam.last);

    /* A frame that did not come, a photo too large, a stream that did not
     * start again, and a home the photo cannot go to. */
    press(CAM_SHOT);
    surface_camera_tick(0);
    assert(kind_asked == SURFACE_CAM_FRAME);
    reply(SURFACE_CAMERA_ERROR, 0);
    surface_camera_tick(0);
    assert(kind_asked == SURFACE_CAM_STREAM && cam.photo == NULL &&
           strstr(cam.status, "no whole frame"));
    reply(SURFACE_CAMERA_OK, 0);
    surface_camera_tick(0);
    press(CAM_SHOT);
    surface_camera_tick(0);
    reply(SURFACE_CAMERA_OK, 0);
    surface_camera_tick(0);
    assert(kind_asked == SURFACE_CAM_ENCODE);
    reply(SURFACE_CAMERA_ROOM, 0);
    surface_camera_tick(0);
    assert(kind_asked == SURFACE_CAM_STREAM && cam.photo == NULL && jobs == 1 &&
           strstr(cam.status, "did not fit"));
    reply(SURFACE_CAMERA_ERROR, 0);
    surface_camera_tick(0);
    assert(!cam.streaming && strstr(cam.status, "did not start again"));
    cam.streaming = 1;
    assert(!setenv("HOME", "/proc/surface-camera-none", 1));
    press(CAM_SHOT);
    surface_camera_tick(0);
    reply(SURFACE_CAMERA_OK, 0);
    surface_camera_tick(0);
    reply(SURFACE_CAMERA_OK, 10);
    surface_camera_tick(0);
    run_job();
    assert(jobs == 2 && strstr(cam.status, "could not be made"));

    /* Closed while the stream restarts: the sensor stops once that ends,
     * and the frame is freed then. */
    assert(pending && kind_asked == SURFACE_CAM_STREAM);
    unsigned now_asks = asks;
    gone(cam.window, NULL);
    surface_camera_tick(0);
    assert(cam.frame && asks == now_asks);
    reply(SURFACE_CAMERA_OK, 0);
    surface_camera_tick(0);
    assert(kind_asked == SURFACE_CAM_CLOSE && cam.frame);
    reply(SURFACE_CAMERA_OK, 0);
    surface_camera_tick(0);
    assert(cam.frame == NULL && !cam.open && !cam.streaming && !cam.closing);

    /* No camera answers: the window says so and asks nothing more. */
    assert(surface_camera_open(ws, (tiku_rect_t){0, 20, 800, 460}, &dirty, damage, review_photo));
    surface_camera_tick(0);
    assert(kind_asked == SURFACE_CAM_OPEN);
    reply(SURFACE_CAMERA_ERROR, 0);
    surface_camera_tick(0);
    surface_camera_tick(0);
    assert(cam.failed && !pending && strstr(cam.status, "No camera"));

    snprintf(path, sizeof path, "%s/Pictures", home);
    assert(!remove(reviewed) && !rmdir(path) && !rmdir(home));
    tiku_workspace_deinit(ws);
    free(storage);
    puts("Camera: the stream, pictures drawn in place, the shutter and saving, Photos and closing passed");
    return 0;
}
