/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_recorder.c - Sound Recorder over a fake microphone behind the POSIX
 * calls and a job runner the test drives: a take read each turn into the
 * scope and the heap, saved as a WAV in ~/Recordings, the list of
 * recordings, a recording's waveform, a refused start, a failing
 * microphone, and a window closed while recording.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../desktop/recorder.c"
#include <assert.h>
#include <unistd.h>

/* The kernel's microphone: enable, dropped and /dev/mic, whose reads hand
 * out the next samples of a running count. */
surface_desktop_bridge_t surface_desktop_bridge;
enum { FD_ENABLE = 3, FD_MIC, FD_DROPPED };
static int enabled, enable_err, mic_open, mic_fail, opened_path;
static unsigned waiting, starts_seen, stops_seen, dropped;
static int16_t next_sample;
int tiku_posix_open(const char *path, int flags)
{
    if (!strcmp(path, "/sys/audio/mic/enable") && flags == O_WRONLY) return FD_ENABLE;
    if (!strcmp(path, "/sys/audio/mic/dropped") && flags == O_RDONLY) return FD_DROPPED;
    if (!strcmp(path, "/dev/mic") && flags == O_RDONLY && enabled) { mic_open = 1; return FD_MIC; }
    errno = ENOENT;
    opened_path++;
    return -1;
}
ssize_t tiku_posix_write(int fd, const void *buf, size_t n)
{
    assert(fd == FD_ENABLE && n == 1);
    if (enable_err) { errno = enable_err; return -1; }
    if (((const char *)buf)[0] == '1') { enabled = 1; starts_seen++; }
    else { if (enabled) stops_seen++; enabled = 0; }
    return 1;
}
ssize_t tiku_posix_read(int fd, void *buf, size_t n)
{
    if (fd == FD_DROPPED) return snprintf(buf, n, "%u\n", dropped);
    assert(fd == FD_MIC && mic_open && enabled);
    if (mic_fail) { errno = EIO; return -1; }
    size_t samples = n / 2u < waiting ? n / 2u : waiting;
    for (size_t i = 0; i < samples; i++) ((int16_t *)buf)[i] = next_sample++;
    waiting -= (unsigned)samples;
    return (ssize_t)(samples * 2u);
}
int tiku_posix_close(int fd) { if (fd == FD_MIC) mic_open = 0; return 0; }
uint64_t surface_desktop_now_us(void) { return 0; }

/* The job runner: posted jobs run when the test says. */
static tiku_work_job_t queue[8];
static int queued;
int tiku_work_post(const tiku_work_job_t *j)
{
    assert(queued < 8);
    queue[queued++] = *j;
    return 0;
}
static void run_jobs(void)
{
    while (queued) {
        tiku_work_job_t j = queue[0];
        memmove(queue, queue + 1, sizeof queue[0] * (size_t)--queued);
        j.run(j.ctx);
        j.done(j.ctx);
        j.free(j.ctx);
    }
}

void tiku_text_line_draw(tiku_surface_t *s, tiku_rect_t r, int face, const char *text,
                         tiku_rgb_t ink)
{
    (void)s; (void)r; (void)face; (void)text; (void)ink;
}

static unsigned damages;
static void damage(tiku_window_t *w, tiku_rect_t r) { assert(w == rec.window); (void)r; damages++; }

static void press(int button)
{
    for (int i = 0; i < rec.dialog.count; i++) {
        tiku_rect_t r = rec.dialog.control[i].rect;
        if (rec.dialog.control[i].id != button) continue;
        tiku_event_t down = {.type = TIKU_EVENT_POINTER_DOWN, .x = r.x + r.w / 2,
                             .y = r.y + r.h / 2, .button = 1};
        tiku_event_t up = down;
        up.type = TIKU_EVENT_POINTER_UP;
        (void)event(rec.window, &down, NULL);
        (void)event(rec.window, &up, NULL);
        return;
    }
    assert(0);
}

static int enabled_button(int id)
{
    for (int i = 0; i < rec.dialog.count; i++)
        if (rec.dialog.control[i].id == id) return rec.dialog.control[i].enabled;
    return -1;
}

static tiku_rgb_t pixels[800 * 480];
static tiku_surface_t surface;
/** @brief Whether the scope's column @p x shows the trace anywhere. */
static int traced(int x)
{
    for (int y = rec.scope.y; y < rec.scope.y + rec.scope.h; y++)
        if ((tiku_peek(&surface, x, y) & 0xFFFFFFu) == 0x6CE07Cu) return 1;
    return 0;
}
static tiku_surface_t surface = {pixels, 800, 480, {0, 0, 800, 480}, 1, NULL, 0, {0, 0, 0, 0}, 0, 0};
static void draw_all(void)
{
    tiku_rect_t c = tiku_window_content(rec.window);
    tiku_clip_set(&surface, c);
    draw(rec.window, &surface, c, &rec);
    tiku_clip_reset(&surface);
}

/** @brief Read back a saved WAV: its header and its samples, which must be
 *  the running count from @p first. */
static uint32_t check_wav(const char *path, int16_t first)
{
    FILE *f = fopen(path, "rb");
    uint8_t head[44], want[44];
    int16_t v;
    uint32_t n = 0;

    assert(f && fread(head, 1, 44, f) == 44);
    while (fread(&v, 2, 1, f) == 1) assert(v == (int16_t)(first + (int16_t)n++));
    fclose(f);
    surface_recorder_wav_header(want, n);
    assert(!memcmp(head, want, 44));
    return n;
}

int main(void)
{
    void *storage = calloc(1, tiku_workspace_size());
    tiku_workspace_t *ws = tiku_workspace_init(storage, tiku_workspace_size(), &surface, NULL);
    char home[] = "/tmp/surface-recorder-XXXXXX", path[700], dir[600];
    uint8_t head[44];
    int dirty = 0;

    assert(ws && mkdtemp(home) && !setenv("HOME", home, 1));
    snprintf(dir, sizeof dir, "%s/Recordings", home);

    /* The header: a mono 16 kHz PCM16 WAV of the sample count. */
    surface_recorder_wav_header(head, 1000);
    assert(!memcmp(head, "RIFF", 4) && !memcmp(head + 8, "WAVEfmt ", 8) && !memcmp(head + 36, "data", 4));
    assert(head[4] == 0xF4 && head[5] == 0x07 && head[16] == 16 && head[20] == 1 && head[22] == 1);
    assert(head[24] == 0x80 && head[25] == 0x3e && head[28] == 0x00 && head[29] == 0x7d);
    assert(head[32] == 2 && head[34] == 16 && head[40] == 0xd0 && head[41] == 0x07);

    /* Opening lists ~/Recordings, which does not exist yet; Stop waits. */
    assert(surface_recorder_open(ws, (tiku_rect_t){0, 20, 800, 460}, &dirty, damage));
    assert(rec.window->opaque && queued == 1);
    run_jobs();
    draw_all();
    assert(rec.nfiles == 0 && enabled_button(REC_RECORD) == 1 && enabled_button(REC_STOP) == 0);
    assert((tiku_peek(&surface, rec.scope.x + 3, rec.scope.y + 3) & 0xFFFFFFu) == 0x0A120Au);

    /* A refused start says why and holds nothing. */
    enable_err = EPERM;
    press(REC_RECORD);
    assert(!rec.recording && !mic_open && strstr(rec.status, "did not start"));
    enable_err = 0;

    /* Record: the microphone starts and each turn reads all that waits. */
    press(REC_RECORD);
    assert(rec.recording && enabled && mic_open && starts_seen == 1);
    draw_all();
    assert(enabled_button(REC_RECORD) == 0 && enabled_button(REC_STOP) == 1);
    waiting = 5000;
    damages = 0;
    surface_recorder_tick(0);
    assert(rec.samples == 5000 && waiting == 0 && damages == 3);
    waiting = 70000;                /* more than one turn's 16 reads hold */
    surface_recorder_tick(0);
    assert(rec.samples == 5000 + 16u * (READ_BYTES / 2u) && waiting > 0);
    surface_recorder_tick(0);
    assert(rec.samples == 75000 && rec.chunks == 5 && waiting == 0);
    /* The scope's newest column holds the count's last samples, and is
     * drawn at its right edge. */
    draw_all();
    column_t last = rec.live[(rec.live_head + COLS - 1u) % COLS];
    assert(last.hi == (int16_t)74999 && last.lo == (int16_t)74875);
    assert(traced(rec.scope.x + rec.scope.w - 1) && !traced(rec.scope.x + rec.scope.w - 1 - COLS));

    /* A turn's damage of the scope, the meter and the clock as one
     * rectangle repaints the buttons inside it. */
    {
        static tiku_rgb_t full[800 * 480];
        tiku_rect_t u = tiku_rect_union(tiku_rect_union(rec.scope, rec.meter), rec.clock);
        tiku_rect_t row = rec.dialog.control[0].rect;
        int differ = 0;

        draw_all();
        memcpy(full, pixels, sizeof full);
        for (int i = 0; i < 800 * 480; i++) pixels[i] = 0xFF123456u;
        tiku_clip_set(&surface, u);
        tiku_clip_damage(&surface, &u);
        draw(rec.window, &surface, tiku_window_content(rec.window), &rec);
        tiku_clip_damage(&surface, NULL);
        tiku_clip_reset(&surface);
        for (int y = row.y; y < row.y + row.h; y++)
            for (int x = row.x; x < rec.clock.x; x++)
                differ += pixels[y * 800 + x] != full[y * 800 + x];
        assert(differ == 0);
    }

    /* Stop: what still waits in the ring joins the take, the microphone
     * stops, and the take is saved by a worker. */
    dropped = 0;
    waiting = 3000;
    press(REC_STOP);
    assert(waiting == 0 && rec.view_n == COLS);
    assert(!rec.recording && !enabled && !mic_open && stops_seen == 1 && rec.saving);
    assert(rec.chunks == 0 && rec.view_n == COLS);
    draw_all();
    assert(enabled_button(REC_RECORD) == 0);
    run_jobs();
    assert(!rec.saving && strstr(rec.status, "Saved recording-") && rec.nfiles == 1);
    assert(!strstr(rec.status, "lost") && strstr(rec.status, "in RAM until reset"));
    surface_desktop_bridge.card_mounted = 1;
    snprintf(path, sizeof path, "%s/%s", dir, rec.files[0].name);
    assert(check_wav(path, 0) == 78000 && rec.files[0].bytes == 44u + 156000u);
    assert(rec.selected == 0);

    /* A second take is listed first, before the first take, and the list
     * leaves out what is not a WAV. */
    FILE *junk = fopen(strcat(strcpy(path, dir), "/notes.txt"), "w");
    assert(junk && fputs("x", junk) >= 0 && !fclose(junk));
    int16_t first = next_sample;
    press(REC_RECORD);
    waiting = 300;
    surface_recorder_tick(0);
    press(REC_STOP);
    run_jobs();
    assert(rec.nfiles == 2);
    snprintf(path, sizeof path, "%s/%s", dir, rec.files[0].name);
    assert(check_wav(path, first) == 300);
    snprintf(path, sizeof path, "%s/%s", dir, rec.files[1].name);
    assert(check_wav(path, 0) == 78000);

    /* Tapping the older recording reads its waveform into the scope. */
    tiku_event_t tap = {.type = TIKU_EVENT_POINTER_DOWN, .x = rec.list.x + 20,
                        .y = rec.list.y + 2 + ROW_H + ROW_H / 2, .button = 1};
    (void)event(rec.window, &tap, NULL);
    assert(rec.selected == 1 && rec.reading && queued == 1);
    run_jobs();
    assert(!rec.reading && rec.view_n == COLS && !strcmp(rec.view_name, rec.files[1].name));
    assert(rec.view[COLS - 1].hi == (int16_t)77999 && rec.view[0].lo == 0);

    /* A failing microphone ends the take, which is saved. */
    press(REC_RECORD);
    waiting = 1000;
    surface_recorder_tick(0);
    mic_fail = 1;
    surface_recorder_tick(0);
    assert(!rec.recording && !enabled && rec.saving && strstr(rec.status, "The microphone failed."));
    mic_fail = 0;
    run_jobs();
    assert(rec.nfiles == 3);

    /* Lost audio is reported; nothing recorded is not saved. */
    press(REC_RECORD);
    dropped = 7;
    waiting = 100;
    surface_recorder_tick(0);
    press(REC_STOP);
    assert(strstr(rec.status, "some audio was lost"));
    run_jobs();
    assert(strstr(rec.status, "Saved recording-") && strstr(rec.status, "audio was lost"));
    assert(!strstr(rec.status, "until reset"));
    dropped = 0;
    press(REC_RECORD);
    press(REC_STOP);
    assert(!rec.saving && strstr(rec.status, "Nothing was recorded.") && queued == 0);

    /* Closing the window while recording stops and saves the take. */
    press(REC_RECORD);
    waiting = 2000;
    surface_recorder_tick(0);
    tiku_workspace_close(ws, rec.window);
    assert(rec.window == NULL && rec.closing && rec.recording);
    surface_recorder_tick(0);
    assert(!rec.recording && !enabled && rec.saving);
    run_jobs();
    assert(rec.nfiles == 5);
    surface_recorder_tick(0);
    assert(!rec.closing);
    assert(surface_recorder_open(ws, (tiku_rect_t){0, 20, 800, 460}, &dirty, damage));
    run_jobs();
    assert(rec.nfiles == 5);
    /* Newest first, a same-second "-2" ahead of its first take. */
    const char *order[] = {"recording-20991231-235959-2.wav", "recording-20991231-235959.wav",
                           "recording-20991231-235958.wav"};
    for (int i = 2; i >= 0; i--) {
        snprintf(path, sizeof path, "%s/%s", dir, order[i]);
        FILE *f = fopen(path, "wb");
        assert(f && fwrite(head, 1, 44, f) == 44 && !fclose(f));
    }
    refresh();
    run_jobs();
    assert(rec.nfiles == 8);
    for (int i = 0; i < 3; i++) assert(!strcmp(rec.files[i].name, order[i]));
    puts("recorder: take, save, list, waveform, refusal, failure, close OK");
    return 0;
}
