/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * recorder.c - Sound Recorder: Record starts the kernel's microphone
 * (/sys/audio/mic/enable) and each desktop turn reads /dev/mic into a take
 * held in the heap, its waveform scrolling in the scope; Stop hands the take
 * to a worker that writes it as a WAV in ~/Recordings.  The list shows the
 * recordings there, and a selected one's waveform fills the scope.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "recorder.h"
#include "bridge.h"
#include "tiku_posix.h"
#include "tiku_dialog.h"
#include "tiku_font.h"
#include "tiku_model.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include "tiku_work.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define RATE 16000u
#define CHUNK RATE              /* samples in one heap chunk: a second */
#define MAX_CHUNKS 1800u        /* half an hour; the heap ends a take sooner */
#define READ_BYTES 8192u
#define COLS 512u               /* scope columns: the last four seconds */
#define COL_SAMPLES (4u * RATE / COLS)
#define MAX_FILES 48
#define ROW_H 22
#define HOLD_US 1500000
enum { REC_RECORD = 1, REC_STOP };

/** @brief The smallest and largest sample under one column of a waveform. */
typedef struct { int16_t lo, hi; } column_t;

/** @brief One recording in ~/Recordings. */
typedef struct { char name[64]; uint32_t bytes; } entry_t;

/** @brief Sound Recorder's state; only the desktop's loop touches it. */
typedef struct {
    tiku_workspace_t *workspace;
    tiku_window_t *window;
    int *dirty;
    void (*damage)(tiku_window_t *window, tiku_rect_t rect);
    tiku_dialog_t dialog;
    tiku_rect_t scope, meter, clock, list, status_rect;
    int fd;                     /* /dev/mic while recording, or -1 */
    int recording, saving, listing, reading, closing;
    int16_t *chunk[MAX_CHUNKS]; /* the take, a second to a chunk */
    unsigned chunks;
    uint32_t samples;
    column_t live[COLS];        /* the scope while recording: a ring */
    unsigned live_head;
    int32_t acc_lo, acc_hi;
    unsigned acc_n;
    column_t view[COLS];        /* a whole recording squeezed to the scope */
    unsigned view_n;
    char view_name[64];
    unsigned peak, hold;
    int64_t hold_until;
    entry_t files[MAX_FILES];
    int nfiles, selected, first;
    char status[160];
} recorder_t;

static recorder_t rec = {.fd = -1, .selected = -1};

static void note(const char *text)
{
    snprintf(rec.status, sizeof rec.status, "%s", text);
    if (rec.dirty) *rec.dirty = 1;
}

/** @brief Redraw @p r of the window, or all of it. */
static void redraw(tiku_rect_t r)
{
    if (rec.window && rec.damage && r.w > 0) rec.damage(rec.window, r);
    else if (rec.dirty) *rec.dirty = 1;
}

/** @brief Write @p text to the kernel node @p path; 0, or an errno. */
static int node_write(const char *path, const char *text)
{
    int fd = tiku_posix_open(path, O_WRONLY), err = 0;

    if (fd < 0) return errno ? errno : EIO;
    if (tiku_posix_write(fd, text, strlen(text)) != (ssize_t)strlen(text)) err = errno ? errno : EIO;
    tiku_posix_close(fd);
    return err;
}

/** @brief The number the kernel node @p path holds, or 0. */
static unsigned long node_number(const char *path)
{
    char text[24] = {0};
    int fd = tiku_posix_open(path, O_RDONLY);

    if (fd < 0) return 0;
    (void)tiku_posix_read(fd, text, sizeof text - 1);
    tiku_posix_close(fd);
    return strtoul(text, NULL, 10);
}

void surface_recorder_wav_header(uint8_t out[44], uint32_t samples)
{
    uint32_t data = samples * 2u;
    const uint32_t fields[] = {36u + data, 16u, 1u | (1u << 16), RATE, RATE * 2u,
                               2u | (16u << 16), data};
    static const char tags[][4] = {"RIFF", "WAVE", "fmt ", "data"};

    memcpy(out, tags[0], 4);
    memcpy(out + 8, tags[1], 4);
    memcpy(out + 12, tags[2], 4);
    memcpy(out + 36, tags[3], 4);
    for (unsigned i = 0; i < 7; i++) {
        static const uint8_t at[] = {4, 16, 20, 24, 28, 32, 40};
        for (unsigned b = 0; b < 4; b++) out[at[i] + b] = (uint8_t)(fields[i] >> (8u * b));
    }
}

/** @brief The folder recordings go in: $HOME/Recordings. */
static void folder(char *out, size_t max)
{
    const char *home = getenv("HOME");

    snprintf(out, max, "%s/Recordings", home && home[0] ? home : "/home");
}

/** @brief Squeeze @p n samples, read through @p get, into @p out's COLS
 *  columns; @return the columns filled. */
static unsigned squeeze(column_t *out, uint32_t n, int16_t (*get)(void *, uint32_t), void *ctx)
{
    unsigned cols = n < COLS ? (unsigned)n : COLS;

    for (unsigned c = 0; c < cols; c++) {
        uint32_t from = (uint32_t)((uint64_t)n * c / cols), to = (uint32_t)((uint64_t)n * (c + 1u) / cols);
        int16_t lo = 32767, hi = -32768;

        for (uint32_t i = from; i < to; i++) {
            int16_t v = get(ctx, i);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        out[c] = (column_t){lo, hi};
    }
    return cols;
}

/* ---- the list of recordings, read on a worker ---- */

typedef struct { entry_t files[MAX_FILES]; int n; } listing_t;

/** @brief Newest first: names compared without ".wav", so a take named
 *  for the same second with "-2" after it comes before the first. */
static int newest_first(const void *a, const void *b)
{
    const char *x = ((const entry_t *)a)->name, *y = ((const entry_t *)b)->name;
    size_t lx = strlen(x) - 4u, ly = strlen(y) - 4u;
    int c = memcmp(y, x, lx < ly ? lx : ly);

    return c ? c : (int)ly - (int)lx;
}

static void list_run(void *ctx)
{
    listing_t *job = ctx;
    char dir[TIKU_PATH_MAX], path[TIKU_PATH_MAX + 64];
    struct dirent *d;
    struct stat st;
    DIR *dp;

    folder(dir, sizeof dir);
    if ((dp = opendir(dir)) == NULL) return;
    while ((d = readdir(dp)) != NULL && job->n < MAX_FILES) {
        size_t len = strlen(d->d_name);
        if (len < 5 || len >= sizeof job->files[0].name || strcmp(d->d_name + len - 4, ".wav")) continue;
        snprintf(path, sizeof path, "%s/%.63s", dir, d->d_name);
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        memcpy(job->files[job->n].name, d->d_name, len + 1u);
        job->files[job->n++].bytes = (uint32_t)st.st_size;
    }
    closedir(dp);
    qsort(job->files, (size_t)job->n, sizeof job->files[0], newest_first);
}

static void list_done(void *ctx)
{
    listing_t *job = ctx;

    memcpy(rec.files, job->files, sizeof rec.files);
    rec.nfiles = job->n;
    rec.selected = -1;
    for (int i = 0; i < rec.nfiles; i++)
        if (!strcmp(rec.files[i].name, rec.view_name)) rec.selected = i;
    if (rec.dirty) *rec.dirty = 1;
}

static void list_free(void *ctx) { free(ctx); rec.listing = 0; }

static void refresh(void)
{
    listing_t *job;
    tiku_work_job_t work = {list_run, list_done, list_free, NULL, "recorder-list"};

    if (rec.listing || (job = calloc(1, sizeof *job)) == NULL) return;
    work.ctx = job;
    if (tiku_work_post(&work) != 0) { free(job); return; }
    rec.listing = 1;
}

/* ---- a recording's waveform, read on a worker ---- */

typedef struct {
    char path[TIKU_PATH_MAX + 64], name[64], error[96];
    column_t cols[COLS];
    unsigned n;
    uint32_t samples;
    int16_t *pcm;
} overview_t;

static int16_t from_pcm(void *ctx, uint32_t i) { return ((overview_t *)ctx)->pcm[i]; }

static void overview_run(void *ctx)
{
    overview_t *job = ctx;
    uint8_t head[44];
    FILE *f = fopen(job->path, "rb");

    if (!f) { snprintf(job->error, sizeof job->error, "%s", strerror(errno)); return; }
    if (fread(head, 1, sizeof head, f) != sizeof head || memcmp(head, "RIFF", 4) ||
        memcmp(head + 8, "WAVE", 4) || head[22] != 1 || head[34] != 16 ||
        (head[24] | head[25] << 8 | head[26] << 16 | (uint32_t)head[27] << 24) != RATE) {
        snprintf(job->error, sizeof job->error, "not a mono 16 kHz WAV");
        fclose(f);
        return;
    }
    uint32_t bytes = head[40] | head[41] << 8 | head[42] << 16 | (uint32_t)head[43] << 24;
    job->samples = bytes / 2u;
    job->pcm = malloc((size_t)job->samples * 2u + 2u);
    if (!job->pcm) { snprintf(job->error, sizeof job->error, "not enough memory"); fclose(f); return; }
    job->samples = (uint32_t)(fread(job->pcm, 2, job->samples, f));
    fclose(f);
    job->n = squeeze(job->cols, job->samples, from_pcm, job);
}

static void overview_done(void *ctx)
{
    overview_t *job = ctx;
    char text[160];

    if (job->error[0]) {
        snprintf(text, sizeof text, "%.60s: %s.", job->name, job->error);
        note(text);
        return;
    }
    memcpy(rec.view, job->cols, sizeof rec.view);
    rec.view_n = job->n;
    snprintf(rec.view_name, sizeof rec.view_name, "%s", job->name);
    snprintf(text, sizeof text, "%.60s: %lu:%04.1f.", job->name,
             (unsigned long)(job->samples / RATE / 60u), (double)(job->samples % (60u * RATE)) / RATE);
    note(text);
    redraw(rec.scope);
}

static void overview_free(void *ctx)
{
    overview_t *job = ctx;

    free(job->pcm);
    free(job);
    rec.reading = 0;
}

static void show(int index)
{
    overview_t *job;
    tiku_work_job_t work = {overview_run, overview_done, overview_free, NULL, "recorder-view"};
    char dir[TIKU_PATH_MAX];

    if (rec.reading || index < 0 || index >= rec.nfiles || (job = calloc(1, sizeof *job)) == NULL) return;
    folder(dir, sizeof dir);
    snprintf(job->path, sizeof job->path, "%s/%.63s", dir, rec.files[index].name);
    memcpy(job->name, rec.files[index].name, sizeof job->name);
    work.ctx = job;
    if (tiku_work_post(&work) != 0) { free(job); return; }
    rec.reading = 1;
    note("Reading the recording...");
}

/* ---- a take on its way to the card ---- */

typedef struct {
    int16_t *chunk[MAX_CHUNKS];
    unsigned chunks;
    uint32_t samples, lost;     /* lost: samples the ring dropped */
    char path[TIKU_PATH_MAX], error[96];
} save_t;

/** @brief On a worker: make ~/Recordings if need be, name the take by the
 *  time with a number after it when that name is taken, write it. */
static void save_run(void *ctx)
{
    save_t *job = ctx;
    char dir[TIKU_PATH_MAX];
    uint8_t head[44];
    struct tm when;
    struct stat st;
    time_t now = time(NULL);
    FILE *f;

    folder(dir, sizeof dir);
    (void)mkdir(dir, 0755);
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        snprintf(job->error, sizeof job->error, "~/Recordings could not be made");
        return;
    }
    (void)localtime_r(&now, &when);
    job->path[0] = 0;
    for (int k = 1; k < 100 && !job->path[0]; k++) {
        int n = snprintf(job->path, sizeof job->path, "%s/recording-%04d%02d%02d-%02d%02d%02d%s%.0d.wav",
                         dir, when.tm_year + 1900, when.tm_mon + 1, when.tm_mday, when.tm_hour,
                         when.tm_min, when.tm_sec, k > 1 ? "-" : "", k > 1 ? k : 0);
        if (n < 0 || (size_t)n >= sizeof job->path || lstat(job->path, &st) == 0) job->path[0] = 0;
    }
    if (!job->path[0]) {
        snprintf(job->error, sizeof job->error, "No free name for the recording");
        return;
    }
    surface_recorder_wav_header(head, job->samples);
    f = fopen(job->path, "wb");
    int ok = f && fwrite(head, 1, sizeof head, f) == sizeof head;
    for (unsigned c = 0; ok && c < job->chunks; c++) {
        uint32_t n = c + 1u < job->chunks ? CHUNK : job->samples - c * CHUNK;
        ok = fwrite(job->chunk[c], 2, n, f) == n;
    }
    if (!ok || fclose(f) != 0) {
        snprintf(job->error, sizeof job->error, "The recording was not saved (%s)", strerror(errno));
        if (!ok && f) (void)fclose(f);
        (void)remove(job->path);
    }
}

static void save_done(void *ctx)
{
    save_t *job = ctx;
    const char *name = strrchr(job->path, '/');
    char text[160];

    if (job->error[0]) {
        note(job->error);
        return;
    }
    snprintf(rec.view_name, sizeof rec.view_name, "%.63s", name ? name + 1 : job->path);
    snprintf(text, sizeof text, "Saved %.80s, %lu KB%s.", rec.view_name,
             (unsigned long)(job->samples * 2u + 44u + 1023u) / 1024u,
             job->lost ? "; audio was lost while the desktop was busy" : "");
    note(text);
    refresh();
}

static void save_free(void *ctx)
{
    save_t *job = ctx;

    for (unsigned c = 0; c < job->chunks; c++) free(job->chunk[c]);
    free(job);
    rec.saving = 0;
}

/* ---- recording ---- */

static int16_t from_take(void *ctx, uint32_t i)
{ (void)ctx; return rec.chunk[i / CHUNK][i % CHUNK]; }

/** @brief Add @p n samples to the take, the scope and the meter; 0, or -1
 *  when the heap has no room for the next second. */
static int take(const int16_t *pcm, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        int16_t v = pcm[i];
        unsigned m = (unsigned)(v < 0 ? -(int)v : v);

        if (rec.samples % CHUNK == 0) {
            if (rec.chunks == MAX_CHUNKS || (rec.chunk[rec.chunks] = malloc(CHUNK * 2u)) == NULL)
                return -1;
            rec.chunks++;
        }
        rec.chunk[rec.chunks - 1u][rec.samples % CHUNK] = v;
        rec.samples++;
        if (m > rec.peak) rec.peak = m;
        if (v < rec.acc_lo) rec.acc_lo = v;
        if (v > rec.acc_hi) rec.acc_hi = v;
        if (++rec.acc_n == COL_SAMPLES) {
            rec.live[rec.live_head] = (column_t){(int16_t)rec.acc_lo, (int16_t)rec.acc_hi};
            rec.live_head = (rec.live_head + 1u) % COLS;
            rec.acc_lo = 32767;
            rec.acc_hi = -32768;
            rec.acc_n = 0;
        }
    }
    return 0;
}

/** @brief Read what /dev/mic holds into the take, up to 16 reads; 1 when
 *  the take must end with @p *why set, else 0. */
static int drain(const char **why)
{
    static int16_t pcm[READ_BYTES / 2u];

    for (int reads = 0; reads < 16; reads++) {
        ssize_t got = tiku_posix_read(rec.fd, pcm, READ_BYTES);

        if (got < 0) {
            *why = "The microphone failed.";
            return 1;
        }
        if (got == 0) break;
        if (take(pcm, (unsigned)got / 2u) != 0) {
            *why = "Memory is full.";
            return 1;
        }
        if ((size_t)got < READ_BYTES) break;
    }
    return 0;
}

static void start(void)
{
    int err;
    char text[160];

    if (rec.recording || rec.saving) return;
    err = node_write("/sys/audio/mic/enable", "1");
    if (err != 0) {
        snprintf(text, sizeof text, "The microphone did not start: %s.", strerror(err));
        note(text);
        return;
    }
    rec.fd = tiku_posix_open("/dev/mic", O_RDONLY);
    if (rec.fd < 0) {
        snprintf(text, sizeof text, "/dev/mic did not open: %s.", strerror(errno));
        note(text);
        (void)node_write("/sys/audio/mic/enable", "0");
        return;
    }
    memset(rec.live, 0, sizeof rec.live);
    rec.live_head = rec.acc_n = 0;
    rec.acc_lo = 32767;
    rec.acc_hi = -32768;
    rec.chunks = rec.samples = rec.peak = rec.hold = 0;
    rec.view_n = 0;
    rec.view_name[0] = 0;
    rec.selected = -1;
    rec.recording = 1;
    note("Recording.");
}

/** @brief Stop the microphone and hand the take to a worker; @p why, when
 *  set, says why the take ended. */
static void stop(const char *why)
{
    unsigned long lost = node_number("/sys/audio/mic/dropped");
    save_t *job;
    tiku_work_job_t work = {save_run, save_done, save_free, NULL, "recorder-save"};
    char text[160];

    if (!rec.recording) return;
    if (!why) (void)drain(&why);    /* what the ring still holds */
    tiku_posix_close(rec.fd);
    rec.fd = -1;
    (void)node_write("/sys/audio/mic/enable", "0");
    rec.recording = 0;
    rec.view_n = squeeze(rec.view, rec.samples, from_take, NULL);
    if (rec.samples == 0 || (job = calloc(1, sizeof *job)) == NULL) {
        note(rec.samples ? "Not enough memory to save the recording." : "Nothing was recorded.");
        for (unsigned c = 0; c < rec.chunks; c++) free(rec.chunk[c]);
        rec.chunks = rec.samples = 0;
        return;
    }
    memcpy(job->chunk, rec.chunk, sizeof job->chunk);
    job->chunks = rec.chunks;
    job->samples = rec.samples;
    job->lost = (uint32_t)lost;
    rec.chunks = 0;
    work.ctx = job;
    if (tiku_work_post(&work) != 0) {
        save_free(job);
        note("The recording could not be queued for saving.");
        return;
    }
    rec.saving = 1;
    snprintf(text, sizeof text, "%s%sSaving %lu:%02lu%s...", why ? why : "", why ? " " : "",
             (unsigned long)(rec.samples / RATE / 60u), (unsigned long)(rec.samples / RATE % 60u),
             lost ? ", some audio was lost" : "");
    note(text);
}

void surface_recorder_tick(int64_t now)
{
    const char *why = NULL;

    if (!rec.recording) {
        if (rec.closing && !rec.saving) rec.closing = 0;
        return;
    }
    if (rec.closing) {
        stop(NULL);
        return;
    }
    if (drain(&why)) {
        stop(why);
        return;
    }
    if (rec.peak >= rec.hold || now > rec.hold_until) {
        rec.hold = rec.peak;
        rec.hold_until = now + HOLD_US;
    }
    redraw(rec.scope);
    redraw(rec.meter);
    redraw(rec.clock);
}

/* ---- the window ---- */

static void layout(tiku_rect_t r)
{
    int bh = tiku_ui_button_h(), x = r.x + 8;
    int rw = tiku_ui_button_w("Record", 96), sw = tiku_ui_button_w("Stop", 80);
    int scope_h = r.h * 2 / 5 > 180 ? 180 : r.h * 2 / 5;
    int y;

    rec.scope = (tiku_rect_t){r.x + 8, r.y + 8, r.w - 16, scope_h};
    rec.meter = (tiku_rect_t){r.x + 8, rec.scope.y + scope_h + 6, r.w - 16, 12};
    y = rec.meter.y + rec.meter.h + 8;
    tiku_dialog_begin(&rec.dialog, REC_RECORD, -1);
    (void)tiku_dialog_add(&rec.dialog, REC_RECORD, (tiku_rect_t){x, y, rw, bh}, TIKU_DIALOG_BUTTON,
                          !rec.recording && !rec.saving);
    (void)tiku_dialog_add(&rec.dialog, REC_STOP, (tiku_rect_t){x + rw + 6, y, sw, bh}, TIKU_DIALOG_BUTTON,
                          rec.recording);
    tiku_dialog_end(&rec.dialog);
    rec.clock = (tiku_rect_t){x + rw + sw + 24, y, 120, bh};
    rec.status_rect = (tiku_rect_t){r.x + 8, r.y + r.h - ROW_H - 4, r.w - 16, ROW_H};
    rec.list = (tiku_rect_t){r.x + 8, y + bh + 8, r.w - 16, rec.status_rect.y - 4 - (y + bh + 8)};
    if (rec.list.h < 0) rec.list.h = 0;
}

/** @brief The waveform: the last four seconds while recording, else the
 *  take just made or the selected recording, on a dark field; full height
 *  is the loudest sample shown, or 2048 when every sample is quieter. */
static void draw_scope(tiku_surface_t *s)
{
    tiku_rect_t r = rec.scope;
    int mid = r.y + r.h / 2, half = r.h / 2 - 2;
    const column_t *cols = rec.recording ? rec.live : rec.view;
    unsigned n = rec.recording ? COLS : rec.view_n;
    unsigned shown = (unsigned)r.w < COLS ? (unsigned)r.w : COLS;

    tiku_fill(s, r, 0xFF0A120Au);
    for (int g = 1; g < 4; g++) tiku_hline(s, r.x, r.y + r.h * g / 4, r.w, 0xFF1C2C1Cu);
    tiku_hline(s, r.x, mid, r.w, 0xFF2E4A2Eu);
    if (n == 0) {
        tiku_text_line_draw(s, tiku_inset(r, 10), 0,
                            rec.saving ? "Saving..." : "Press Record to record from the board's microphones.",
                            0xFF7FA07Fu);
        return;
    }
    int32_t scale = 2048;
    for (unsigned c = 0; c < n; c++) {
        int32_t lo = -(int32_t)cols[c].lo, hi = cols[c].hi;
        if (cols[c].lo > cols[c].hi) continue;
        if (lo > scale) scale = lo;
        if (hi > scale) scale = hi;
    }
    for (int x = 0; x < r.w; x++) {
        unsigned c;
        column_t v;

        if (rec.recording) {
            unsigned back = (unsigned)(r.w - 1 - x);
            if (back >= shown) continue;
            c = (rec.live_head + COLS - 1u - back) % COLS;
        } else {
            c = (unsigned)((uint64_t)x * n / (unsigned)r.w);
        }
        v = cols[c];
        if (v.lo > v.hi) continue;
        int top = mid - (int)((int32_t)v.hi * half / scale), bot = mid - (int)((int32_t)v.lo * half / scale);
        if (top < r.y) top = r.y;
        if (bot > r.y + r.h - 1) bot = r.y + r.h - 1;
        if (bot < top) bot = top;
        tiku_vline(s, r.x + x, top, bot - top + 1, 0xFF6CE07Cu);
    }
}

/** @brief The level of this turn's loudest sample, -60..0 dBFS, green to
 *  yellow to red, with the held peak as a line. */
static void draw_meter(tiku_surface_t *s)
{
    tiku_rect_t r = rec.meter;
    float level = rec.peak ? 20.0f * log10f((float)rec.peak / 32768.0f) : -60.0f;
    float held = rec.hold ? 20.0f * log10f((float)rec.hold / 32768.0f) : -60.0f;
    int w = (int)((level + 60.0f) / 60.0f * (float)r.w), h = (int)((held + 60.0f) / 60.0f * (float)r.w);
    int green = r.w * 48 / 60, yellow = r.w * 57 / 60;

    tiku_fill(s, r, 0xFF101810u);
    if (w > r.w) w = r.w;
    if (w > 0) tiku_fill(s, (tiku_rect_t){r.x, r.y, w < green ? w : green, r.h}, 0xFF3CC04Cu);
    if (w > green) tiku_fill(s, (tiku_rect_t){r.x + green, r.y, (w < yellow ? w : yellow) - green, r.h},
                             0xFFE0C83Cu);
    if (w > yellow) tiku_fill(s, (tiku_rect_t){r.x + yellow, r.y, w - yellow, r.h}, 0xFFE0483Cu);
    if (h > 1 && h <= r.w) tiku_vline(s, r.x + h - 1, r.y, r.h, 0xFFF0F0F0u);
    rec.peak = 0;
}

static void draw_clock(tiku_surface_t *s)
{
    char text[32];
    uint32_t t = rec.recording ? rec.samples : 0;

    snprintf(text, sizeof text, "%lu:%02lu.%lu", (unsigned long)(t / RATE / 60u),
             (unsigned long)(t / RATE % 60u), (unsigned long)(t % RATE / (RATE / 10u)));
    tiku_ui_panel(s, rec.clock);
    tiku_text_line_draw(s, rec.clock, TIKU_FONT_BOLD, text, TIKU_C_TEXT);
}

static void draw_list(tiku_surface_t *s)
{
    tiku_rect_t r = rec.list;
    char text[128];
    int rows = r.h / ROW_H;

    tiku_ui_sunken(s, r, TIKU_C_DOC);
    if (rec.nfiles == 0) {
        tiku_text_line_draw(s, (tiku_rect_t){r.x + 6, r.y + 4, r.w - 12, ROW_H}, 0,
                            rec.listing ? "Reading ~/Recordings..." :
                            "No recordings in ~/Recordings yet.", TIKU_C_TEXT);
        return;
    }
    if (rec.first > rec.nfiles - rows) rec.first = rec.nfiles - rows > 0 ? rec.nfiles - rows : 0;
    for (int i = 0; i < rows && rec.first + i < rec.nfiles; i++) {
        const entry_t *e = &rec.files[rec.first + i];
        uint32_t secs = e->bytes > 44u ? (e->bytes - 44u) / 2u / RATE : 0u;

        snprintf(text, sizeof text, "%-40s %3lu:%02lu %6lu KB", e->name, (unsigned long)(secs / 60u),
                 (unsigned long)(secs % 60u), (unsigned long)(e->bytes + 1023u) / 1024u);
        tiku_ui_list_row(s, (tiku_rect_t){r.x + 2, r.y + 2 + i * ROW_H, r.w - 4, ROW_H}, text,
                         rec.first + i == rec.selected);
    }
}

/** @brief The overlap of @p a and @p b; empty when none. */
static int touches(tiku_rect_t a, tiku_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

/** @brief Draw what of the window the clip holds; each part is drawn when
 *  the clip touches it, since the scope's, the meter's and the clock's
 *  damage of a turn arrive as one rectangle that spans the buttons. */
static void draw(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r, void *ctx)
{
    tiku_rect_t clip = s->clip;
    tiku_rect_t row = {r.x, rec.clock.y, r.w, rec.clock.h};

    (void)w; (void)ctx;
    layout(r);
    row = (tiku_rect_t){r.x, rec.clock.y, r.w, rec.clock.h};
    tiku_clip_set(s, r);
    tiku_ui_panel(s, r);
    if (touches(s->clip, row)) {
        for (int i = 0; i < rec.dialog.count; i++) {
            const tiku_dialog_control_t *d = &rec.dialog.control[i];
            unsigned flags = d->enabled ? 0 : TIKU_S_DISABLED;

            if (tiku_dialog_pressed(&rec.dialog, d->id)) flags |= TIKU_S_PRESSED;
            tiku_ui_button(s, d->rect, d->id == REC_RECORD ? "Record" : "Stop", flags);
        }
    }
    if (touches(s->clip, rec.list)) draw_list(s);
    if (touches(s->clip, rec.status_rect))
        tiku_text_line_draw(s, rec.status_rect, 0, rec.status[0] ? rec.status :
                            "Mono, 16 kHz; recordings go to ~/Recordings.", TIKU_C_TEXT);
    if (touches(s->clip, rec.scope)) draw_scope(s);
    if (touches(s->clip, rec.meter)) draw_meter(s);
    if (touches(s->clip, rec.clock)) draw_clock(s);
    s->clip = clip;
}

static int event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    tiku_dialog_result_t res;

    (void)ctx;
    layout(tiku_window_content(w));
    res = tiku_dialog_event(&rec.dialog, ev);
    if (res.activated == REC_RECORD) start();
    if (res.activated == REC_STOP) stop(NULL);
    if (!res.taken && ev->type == TIKU_EVENT_POINTER_DOWN && !rec.recording &&
        ev->x >= rec.list.x && ev->x < rec.list.x + rec.list.w &&
        ev->y >= rec.list.y && ev->y < rec.list.y + rec.list.h) {
        int i = rec.first + (ev->y - rec.list.y - 2) / ROW_H;

        if (i >= 0 && i < rec.nfiles && i != rec.selected) {
            rec.selected = i;
            show(i);
        }
        res.taken = 1;
    }
    if (ev->type == TIKU_EVENT_WHEEL && ev->dy) {
        rec.first += ev->dy > 0 ? -1 : 1;
        if (rec.first < 0) rec.first = 0;
        res.taken = 1;
    }
    if (res.taken && rec.dirty) *rec.dirty = 1;
    return res.taken;
}

/** @brief The window went away: a take in progress stops and is saved. */
static void gone(tiku_window_t *w, void *ctx)
{
    (void)w; (void)ctx;
    rec.window = NULL;
    if (rec.recording) rec.closing = 1;
}

tiku_window_t *surface_recorder_open(tiku_workspace_t *workspace, tiku_rect_t area, int *dirty,
                                     void (*damage)(tiku_window_t *window, tiku_rect_t rect))
{
    rec.workspace = workspace;
    rec.dirty = dirty;
    rec.damage = damage;
    if (!rec.window) {
        if (rec.closing) return NULL;
        rec.window = tiku_workspace_open(workspace, "Sound Recorder", tiku_inset(area, 2), draw,
                                         &rec, &rec);
        if (!rec.window) return NULL;
        tiku_window_set_handlers(rec.window, event, NULL, gone);
        tiku_window_set_opaque(rec.window, 1);
        tiku_dialog_init(&rec.dialog);
        rec.status[0] = 0;
        refresh();
    }
    tiku_workspace_activate(workspace, rec.window);
    if (dirty) *dirty = 1;
    return rec.window;
}
