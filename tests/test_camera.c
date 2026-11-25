/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_camera.c - Camera's window over a fake kernel bridge and job runner:
 * the requests it makes and when, the frame painted fitted and converted
 * from UYVY, the shutter's encode and save into ~/Pictures, Photos, and
 * closing while a request runs.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../desktop/camera.c"
#include <assert.h>
#include <unistd.h>

/* The kernel's side of the bridge: one request at a time, answered by the
 * test. */
static int pending, kind_asked, answer = SURFACE_CAMERA_BUSY;
static unsigned asks, quality_asked;
static void *frame_asked;
static uint8_t *out_asked;
static uint32_t cap_asked, answer_len;

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

/** @brief The JFIF conversion the painter uses, as doubles rounded down. */
static uint32_t expect(int y, int u, int v)
{
    int r = y + ((359 * (v - 128)) >> 8), g = y - ((88 * (u - 128) + 183 * (v - 128)) >> 8),
        b = y + ((454 * (u - 128)) >> 8);
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
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

int main(void)
{
    static tiku_rgb_t pixels[800 * 480];
    tiku_surface_t surface = {pixels, 800, 480, {0, 0, 800, 480}, 1, NULL, 0, {0, 0, 0, 0}, 0, 0};
    void *storage = calloc(1, tiku_workspace_size());
    tiku_workspace_t *ws = tiku_workspace_init(storage, tiku_workspace_size(), &surface, NULL);
    char home[] = "/tmp/surface-camera-XXXXXX", path[600];
    int dirty = 0;
    uint32_t len;

    assert(ws && mkdtemp(home) && !setenv("HOME", home, 1));
    /* Opening asks the kernel to start the sensor, then for a frame. */
    assert(surface_camera_open(ws, (tiku_rect_t){0, 20, 800, 460}, &dirty, review_photo));
    assert(cam.frame && dirty && !((uintptr_t)cam.frame & 63u));
    surface_camera_tick(0);
    assert(asks == 1 && kind_asked == SURFACE_CAM_OPEN);
    surface_camera_tick(0);
    assert(asks == 1);
    /* The shutter armed before any frame waits for one. */
    cam.shoot = 1;
    reply(SURFACE_CAMERA_OK, 0);
    surface_camera_tick(0);
    assert(cam.open && asks == 2 && kind_asked == SURFACE_CAM_FRAME && frame_asked == cam.frame);
    cam.shoot = 0;

    /* A whole frame: the left half grey, its even pixels darker than its
     * odd ones, the right half a colour; no next frame until it is
     * painted. */
    for (uint32_t i = 0; i < SURFACE_CAMERA_FRAME_BYTES; i += 4) {
        int right = (i / 2) % SURFACE_CAMERA_W >= SURFACE_CAMERA_W / 2;
        cam.frame[i] = right ? 85 : 128;
        cam.frame[i + 1] = right ? 76 : 100;
        cam.frame[i + 2] = right ? 255 : 128;
        cam.frame[i + 3] = right ? 76 : 180;
    }
    dirty = 0;
    reply(SURFACE_CAMERA_OK, 0);
    surface_camera_tick(0);
    assert(cam.shown && cam.unpainted && dirty && cam.frames == 1);
    surface_camera_tick(0);
    assert(asks == 2);
    tiku_rect_t content = tiku_window_content(cam.window);
    draw(cam.window, &surface, content, &cam);
    assert(!cam.unpainted);
    /* Fitted at 16:9 in the view, centred; the rest of the view is dark. */
    tiku_rect_t v = cam.viewport;
    int dw = v.w, dh = (int)((long)v.w * 720 / 1280);
    if (dh > v.h) { dh = v.h; dw = (int)((long)v.h * 1280 / 720); }
    int ox = v.x + (v.w - dw) / 2, oy = v.y + (v.h - dh) / 2;
    assert(dw < v.w || dh < v.h);
    for (int x = 0; x < dw / 2 - 2; x++) {
        unsigned sx = (unsigned)(x * 1280L / dw);
        assert((tiku_peek(&surface, ox + x, oy + dh / 2) & 0xFFFFFFu) ==
               expect(sx & 1u ? 180 : 100, 128, 128));
    }
    assert((tiku_peek(&surface, ox + 3 * dw / 4, oy + dh / 2) & 0xFFFFFFu) == expect(76, 85, 255));
    assert((tiku_peek(&surface, ox, oy) & 0xFFFFFFu) == expect(100, 128, 128));
    assert((tiku_peek(&surface, ox + dw - 1, oy + dh - 1) & 0xFFFFFFu) == expect(76, 85, 255));
    if (ox > v.x) assert((tiku_peek(&surface, ox - 1, oy + dh / 2) & 0xFFFFFFu) == 0x101010u &&
                         (tiku_peek(&surface, ox + dw, oy + dh / 2) & 0xFFFFFFu) == 0x101010u);
    if (oy > v.y) assert((tiku_peek(&surface, ox + dw / 2, oy - 1) & 0xFFFFFFu) == 0x101010u &&
                         (tiku_peek(&surface, ox + dw / 2, oy + dh) & 0xFFFFFFu) == 0x101010u);
    surface_camera_tick(0);
    assert(asks == 3 && kind_asked == SURFACE_CAM_FRAME);
    /* At 800 pixels across, odd columns of the frame are sampled too. */
    tiku_rect_t kept = cam.viewport;
    int odd = 0;
    cam.viewport = (tiku_rect_t){0, 0, 800, 450};
    paint_frame(&surface);
    for (int x = 0; x < 390; x++) {
        unsigned sx = (unsigned)(x * 1280L / 800);
        odd += sx & 1u;
        assert((tiku_peek(&surface, x, 200) & 0xFFFFFFu) == expect(sx & 1u ? 180 : 100, 128, 128));
    }
    assert(odd > 0);
    cam.viewport = kept;

    /* The shutter: the frame shown encoded at quality 80 into 512 KB, then
     * saved by a worker in ~/Pictures; the preview goes on meanwhile. */
    reply(SURFACE_CAMERA_OK, 0);
    surface_camera_tick(0);
    press(CAM_SHOT);
    assert(cam.shoot);
    surface_camera_tick(0);
    assert(asks == 4 && kind_asked == SURFACE_CAM_ENCODE && out_asked == cam.photo &&
           cap_asked == 524288u && quality_asked == 80u && frame_asked == cam.frame);
    memset(cam.photo, 0xA5, 1000);
    reply(SURFACE_CAMERA_OK, 1000);
    surface_camera_tick(0);
    assert(jobs == 1 && cam.saving && cam.photo == NULL && asks == 4);
    draw(cam.window, &surface, content, &cam);
    surface_camera_tick(0);
    assert(asks == 5 && kind_asked == SURFACE_CAM_FRAME);
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
    press(CAM_PHOTOS);
    assert(reviewed == cam.last);

    /* A photo larger than its buffer, and a home the photo cannot go to. */
    reply(SURFACE_CAMERA_OK, 0);
    surface_camera_tick(0);
    press(CAM_SHOT);
    surface_camera_tick(0);
    assert(kind_asked == SURFACE_CAM_ENCODE);
    reply(SURFACE_CAMERA_ROOM, 0);
    surface_camera_tick(0);
    assert(cam.photo == NULL && strstr(cam.status, "did not fit") && jobs == 1);
    draw(cam.window, &surface, content, &cam);
    surface_camera_tick(0);
    assert(!setenv("HOME", "/proc/surface-camera-none", 1));
    reply(SURFACE_CAMERA_OK, 0);
    surface_camera_tick(0);
    press(CAM_SHOT);
    surface_camera_tick(0);
    reply(SURFACE_CAMERA_OK, 10);
    surface_camera_tick(0);
    run_job();
    assert(jobs == 2 && strstr(cam.status, "could not be made"));
    draw(cam.window, &surface, content, &cam);
    surface_camera_tick(0);

    /* Closed while a frame is being taken: the sensor stops once it ends,
     * and the frame is freed then. */
    assert(pending && kind_asked == SURFACE_CAM_FRAME);
    unsigned before = asks;
    gone(cam.window, NULL);
    surface_camera_tick(0);
    assert(cam.frame && asks == before);
    reply(SURFACE_CAMERA_OK, 0);
    surface_camera_tick(0);
    assert(kind_asked == SURFACE_CAM_CLOSE && cam.frame);
    reply(SURFACE_CAMERA_OK, 0);
    surface_camera_tick(0);
    assert(cam.frame == NULL && !cam.open && !cam.closing);

    /* No camera answers: the window says so and asks nothing more. */
    assert(surface_camera_open(ws, (tiku_rect_t){0, 20, 800, 460}, &dirty, review_photo));
    surface_camera_tick(0);
    assert(kind_asked == SURFACE_CAM_OPEN);
    reply(SURFACE_CAMERA_ERROR, 0);
    surface_camera_tick(0);
    surface_camera_tick(0);
    assert(cam.failed && !pending && strstr(cam.status, "No camera"));
    (void)len;

    snprintf(path, sizeof path, "%s/Pictures", home);
    assert(!remove(reviewed) && !rmdir(path) && !rmdir(home));
    tiku_workspace_deinit(ws);
    free(storage);
    puts("Camera: requests, painting, the shutter and saving, Photos and closing passed");
    return 0;
}
