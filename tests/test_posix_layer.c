/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_posix_layer.c - the desktop's file layer on the host, under the sanitizers.
 *
 * Built with tests/posix_host/ ahead of the system headers and its rename.h
 * forced in, so these calls reach desktop/posix.c's tree.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "bridge.h"
#include "tiku_crypto.h"
#include "tiku_posix.h"

int _open(const char *path, int flags, int mode);
int _close(int fd);
int _unlink(const char *path);
int _rename(const char *old, const char *new);
ssize_t _read(int fd, void *buf, size_t n);
ssize_t _write(int fd, const void *buf, size_t n);
int tiku_posix_mutations(uint64_t *count);
off_t _lseek(int fd, off_t offset, int whence);
int _fstat(int fd, struct stat *st);
int _stat(const char *path, struct stat *st);
int surface_desktop_files(void);
int _fcntl(int fd, int cmd, int arg);
int mkstemp(char *tmpl);
char *realpath(const char *path, char *resolved);

/* The desktop side the layer calls: no card, no log, a fixed clock. */
surface_desktop_bridge_t surface_desktop_bridge;
char **environ;
void surface_desktop_card(surface_media_io_t *io) { io->result = SURFACE_MEDIA_UNAVAILABLE; }
void surface_desktop_log(const void *bytes, size_t length) { (void)bytes; (void)length; }
uint64_t surface_desktop_now_us(void) { return 1000000u; }
void surface_os_desktop_exit(void) {}
void surface_os_desktop_kick(void) {}
void surface_os_desktop_lock(void) {}
void surface_os_desktop_heap_use(size_t *used, size_t *total, uint32_t *refused, size_t *largest)
{
    if (used != NULL) *used = 1u << 20;
    if (total != NULL) *total = 4u << 20;
    if (refused != NULL) *refused = 0;
    if (largest != NULL) *largest = 0;
}
void surface_os_desktop_unlock(void) {}
void surface_os_desktop_wait(int (*ready)(void *), void *context, uint64_t timeout_us)
{ (void)ready; (void)context; (void)timeout_us; }
int surface_os_desktop_random(void *bytes, size_t length) { memset(bytes, 7, length); return 0; }
static int clock_state = SURFACE_CLOCK_SET;

#include "fake_store.h"

/* TikuOS's compat/posix over a fixed namespace: /sys holding uptime, "77\n",
 * and /data, the store, in fake_store.h.  One /sys descriptor and one stream
 * at a time; each call is counted. */
static const char board_text[] = "77\n";
static int board_fd = -1, board_calls, board_pos;
static size_t board_off;
static DIR board_stream;
static struct dirent board_entry;
int tiku_posix_open(const char *path, int flags)
{
    board_calls++;
    if (fake_path(path)) return fake_open(path, flags);
    if (strcmp(path, "/sys/uptime") != 0) {
        errno = ENOENT;
        return -1;
    }
    if ((flags & O_ACCMODE) != O_RDONLY) {
        errno = EROFS;
        return -1;
    }
    board_fd = 3;
    board_off = 0;
    return board_fd;
}
ssize_t tiku_posix_read(int fd, void *buf, size_t n)
{
    size_t left = sizeof board_text - 1u - board_off;

    board_calls++;
    if (fake_fd(fd)) return fake_read(fd, buf, n);
    if (fd != board_fd) {
        errno = EBADF;
        return -1;
    }
    if (n > left) n = left;
    memcpy(buf, board_text + board_off, n);
    board_off += n;
    return (ssize_t)n;
}
off_t tiku_posix_lseek(int fd, off_t offset, int whence)
{
    board_calls++;
    if (fake_fd(fd)) return fake_lseek(fd, offset, whence);
    if (fd != board_fd || whence != SEEK_SET || offset < 0) {
        errno = EINVAL;
        return -1;
    }
    board_off = (size_t)offset;
    return offset;
}
int tiku_posix_close(int fd)
{
    board_calls++;
    if (fake_fd(fd)) return fake_close(fd);
    if (fd != board_fd) {
        errno = EBADF;
        return -1;
    }
    board_fd = -1;
    return 0;
}
int tiku_posix_fcntl(int fd, int cmd) { (void)fd; (void)cmd; return O_RDONLY; }
int tiku_posix_fstat(int fd, struct stat *st)
{
    board_calls++;
    if (fake_fd(fd)) return fake_fstat(fd, st);
    if (fd != board_fd) {
        errno = EBADF;
        return -1;
    }
    memset(st, 0, sizeof *st);
    st->st_mode = S_IFREG | 0444;
    return 0;
}
int tiku_posix_stat(const char *path, struct stat *st)
{
    board_calls++;
    if (fake_path(path)) return fake_stat(path, st);
    memset(st, 0, sizeof *st);
    st->st_ino = (ino_t)(strlen(path) + 100u);
    if (strcmp(path, "/") == 0 || strcmp(path, "/sys") == 0) st->st_mode = S_IFDIR | 0555;
    else if (strcmp(path, "/sys/uptime") == 0) st->st_mode = S_IFREG | 0444;
    else {
        errno = ENOENT;
        return -1;
    }
    return 0;
}
int tiku_posix_is_open(int fd) { return fd == board_fd || fake_slot(fd) != NULL; }
ssize_t tiku_posix_write(int fd, const void *buf, size_t n) { return fake_write(fd, buf, n); }
int tiku_posix_fsync(int fd) { return fake_fsync(fd); }
int tiku_posix_rename(const char *from, const char *to)
{
    if (!fake_path(from) || !fake_path(to)) { errno = EXDEV; return -1; }
    return fake_rename(from, to);
}
int tiku_posix_unlink(const char *path) { return fake_path(path) ? fake_unlink(path) : (errno = EROFS, -1); }
DIR *tiku_posix_opendir(const char *path)
{
    board_calls++;
    if (strcmp(path, "/sys") != 0) {
        errno = ENOENT;
        return NULL;
    }
    board_pos = 0;
    return &board_stream;
}
struct dirent *tiku_posix_readdir(DIR *dir)
{
    board_calls++;
    if (dir != &board_stream || board_pos > 0) return NULL;
    board_pos++;
    snprintf(board_entry.d_name, sizeof board_entry.d_name, "uptime");
    board_entry.d_type = DT_REG;
    return &board_entry;
}
void tiku_posix_rewinddir(DIR *dir) { if (dir == &board_stream) board_pos = 0; }
int tiku_posix_closedir(DIR *dir)
{
    board_calls++;
    board_pos = -1;
    return dir == &board_stream ? 0 : -1;
}
int tiku_posix_is_dir_stream(const DIR *dir) { return dir == &board_stream; }
uint8_t tiku_posix_authority_set(uint8_t cap) { (void)cap; return 0; }
int surface_os_clock_state(void) { return clock_state; }
void tiku_sha256(const void *data, size_t n, unsigned char out[32]) { (void)data; (void)n; memset(out, 1, 32); }

/** @brief Create an empty file at @p path. */
static void touch(const char *path)
{
    int fd = _open(path, O_WRONLY | O_CREAT, 0644);

    assert(fd >= 0 && _close(fd) == 0);
}

/** @brief The next entry's name, or "" at the end. */
static const char *next(DIR *d)
{
    struct dirent *e = readdir(d);

    return e != NULL ? e->d_name : "";
}

/** @brief The bytes @p path reads back through the layer, in @p out. */
static ssize_t slurp(const char *path, char *out, size_t max)
{
    int fd = _open(path, O_RDONLY, 0);
    ssize_t n, got = 0;

    if (fd < 0) return -1;
    while ((size_t)got < max && (n = _read(fd, out + got, max - (size_t)got)) > 0) got += n;
    assert(_close(fd) == 0);
    return got;
}

/** @brief Save @p text over @p path as Stomme's guarded save does: a
 *  temporary beside it, fchmod to the target's mode, fsync, close, rename,
 *  then the directory's fsync.  @return 0, or -1 at the first failure */
static int guarded_save(const char *path, const char *dir, const char *text)
{
    char tmp[PATH_MAX];
    struct stat st;
    int fd, d, ok;

    snprintf(tmp, sizeof tmp, "%s.tmp-XXXXXX", path);
    if (lstat(path, &st) != 0 || (fd = mkstemp(tmp)) < 0) return -1;
    ok = _fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 &&
         _write(fd, text, strlen(text)) == (ssize_t)strlen(text) &&
         fchmod(fd, st.st_mode & 0777) == 0 && fsync(fd) == 0;
    ok = _close(fd) == 0 && ok;
    ok = ok && _rename(tmp, path) == 0;
    if (!ok) (void)_unlink(tmp);
    d = _open(dir, O_RDONLY | O_DIRECTORY, 0);
    ok = ok && d >= 0 && fsync(d) == 0;
    if (d >= 0) (void)_close(d);
    return ok ? 0 : -1;
}

/** @brief The settings files in the store: seeded once confirmed missing,
 *  read from a copy, saved through a temporary and a rename. */
static void test_settings_store(void)
{
    const char *path = "/home/.config/TrackerSettings";
    char buf[256], real[PATH_MAX];
    struct stat st;
    int fd, rd;
    unsigned before;

    fake_reset();
    assert(slurp(path, buf, sizeof buf) == 28 && !memcmp(buf, "GenerateImageThumbnails off\n", 28));
    assert(fake_holds("ts/settings", "GenerateImageThumbnails off\n") && fake_publishes == 1);
    assert(_stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 28 && st.st_dev == 4);
    assert(realpath(path, real) != NULL && !strcmp(real, path));
    assert(access(path, R_OK | W_OK) == 0);
    before = fake_publishes;
    assert(slurp(path, buf, sizeof buf) == 28 && fake_publishes == before);

    /* A save through the temporary replaces the record; a reader opened
     * before it keeps the old copy, and the next read sees the new. */
    rd = _open(path, O_RDONLY, 0);
    assert(rd >= 0);
    assert(guarded_save(path, "/home/.config", "DarkMode 1\nGenerateImageThumbnails off\n") == 0);
    assert(fake_holds("ts/settings", "DarkMode 1\nGenerateImageThumbnails off\n"));
    assert(fake_renames == 1 && fake_dir_syncs == 1);
    for (int i = 0; i < FAKE_FILES; i++) assert(!fake_files[i].used || !strstr(fake_files[i].name, ".tmp-"));
    assert(_read(rd, buf, sizeof buf) == 28 && _close(rd) == 0);
    assert(slurp(path, buf, sizeof buf) == 39 && !memcmp(buf, "DarkMode 1\n", 11));

    /* The temporary's name fits the store: the key replaces the path. */
    fd = mkstemp(strcpy(real, "/home/.config/TrackerSettings.tmp-XXXXXX"));
    assert(fd >= 0 && _close(fd) == 0);
    {
        char key[64];
        snprintf(key, sizeof key, "ts/settings%s", strstr(real, ".tmp-"));
        assert(strlen(key) == 22 && fake_find(key) != NULL && _unlink(real) == 0 && fake_find(key) == NULL);
    }

    /* The store file keeps mode 0644; a rename out of the store is EXDEV;
     * a failed publication is reported and leaves the record. */
    fd = _open("/home/.config/TrackerSettings.new", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    errno = 0;
    assert(fd >= 0 && fchmod(fd, 0600) == -1 && errno == EPERM && fchmod(fd, 0644) == 0);
    assert(_close(fd) == 0);
    errno = 0;
    assert(_rename("/home/.config/TrackerSettings.new", "/tmp/x") == -1 && errno == EXDEV);
    assert(_unlink("/home/.config/TrackerSettings.new") == 0);
    fake_fail = 1;
    assert(guarded_save(path, "/home/.config", "DarkMode 2\n") == -1);
    fd = _open("/home/.config/TrackerSettings.new", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fd >= 0 && _write(fd, "x", 1) == 1);
    errno = 0;
    assert(fsync(fd) == -1 && errno == EIO);
    errno = 0;
    assert(_close(fd) == -1 && errno == EIO && !fake_find("ts/settings.new"));
    fake_fail = 0;
    assert(fake_holds("ts/settings", "DarkMode 1\nGenerateImageThumbnails off\n"));
    assert(slurp(path, buf, sizeof buf) == 39);

    /* The session state saves to its own key; a file the store does not
     * have and has no default is missing. */
    errno = 0;
    assert(_open("/home/.local/share/tracker/devices/local.state", O_RDONLY, 0) == -1 &&
           errno == ENOENT);
    fd = _open("/home/.local/share/tracker/devices/local.state.new", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fd >= 0 && _write(fd, "state", 5) == 5 && _close(fd) == 0);
    assert(_rename("/home/.local/share/tracker/devices/local.state.new",
                   "/home/.local/share/tracker/devices/local.state") == 0);
    assert(fake_holds("ts/session", "state") && slurp("/home/.local/share/tracker/devices/local.state", buf, sizeof buf) == 5);
}

int main(void)
{
    DIR *d;

    assert(surface_desktop_files() == 0);

    /* Desktop and Documents say the home is not kept, read-only. */
    {
        struct stat st;

        assert(_stat("/home/Desktop/Files here are lost at restart.txt", &st) == 0);
        assert(S_ISREG(st.st_mode) && (st.st_mode & 0222) == 0 && st.st_size > 0);
        assert(_stat("/home/Documents/Files here are lost at restart.txt", &st) == 0);
        assert((st.st_mode & 0222) == 0);
    }

    /* A descriptor's writes count as a change at its first write and at
     * its close, not at each write. */
    {
        uint64_t before, after;
        int fd = _open("/tmp/counted", O_WRONLY | O_CREAT, 0644);

        assert(fd >= 0 && tiku_posix_mutations(&before) == 0);
        assert(_write(fd, "a", 1) == 1 && tiku_posix_mutations(&after) == 0);
        assert(after == before + 1u);
        assert(_write(fd, "b", 1) == 1 && _write(fd, "c", 1) == 1);
        assert(tiku_posix_mutations(&before) == 0 && before == after);
        assert(_close(fd) == 0 && tiku_posix_mutations(&after) == 0);
        assert(after == before + 1u);
        fd = _open("/tmp/counted", O_RDONLY, 0);
        assert(fd >= 0 && _close(fd) == 0 && tiku_posix_mutations(&before) == 0);
        assert(before == after);
    }

    /* A directory removed while a stream holds it: the stream gives the
     * dots and its end, and the last close frees it (#7). */
    assert(mkdir("/tmp/d", 0755) == 0);
    d = opendir("/tmp/d");
    assert(d != NULL && rmdir("/tmp/d") == 0 && opendir("/tmp/d") == NULL);
    assert(!strcmp(next(d), ".") && !strcmp(next(d), "..") && !strcmp(next(d), ""));
    rewinddir(d);
    assert(!strcmp(next(d), ".") && !strcmp(next(d), "..") && !strcmp(next(d), ""));
    assert(closedir(d) == 0);

    /* The same for an empty directory a rename replaces: without the free
     * on the last close the sanitizer reports a leak at exit. */
    assert(mkdir("/tmp/a", 0755) == 0 && mkdir("/tmp/b", 0755) == 0);
    d = opendir("/tmp/b");
    assert(d != NULL && _rename("/tmp/a", "/tmp/b") == 0);
    assert(!strcmp(next(d), ".") && !strcmp(next(d), "..") && !strcmp(next(d), ""));
    assert(closedir(d) == 0);

    /* Entries removed after the stream reached them are skipped, not
     * read (#8).  Newer entries come first: y, then x. */
    assert(mkdir("/tmp/e", 0755) == 0);
    touch("/tmp/e/x");
    touch("/tmp/e/y");
    d = opendir("/tmp/e");
    assert(!strcmp(next(d), ".") && !strcmp(next(d), ".."));
    assert(_unlink("/tmp/e/y") == 0);
    assert(!strcmp(next(d), "x") && !strcmp(next(d), ""));
    rewinddir(d);
    assert(!strcmp(next(d), ".") && !strcmp(next(d), ".."));
    assert(_unlink("/tmp/e/x") == 0 && !strcmp(next(d), ""));
    assert(closedir(d) == 0);

    /* An entry moved out to another directory, and one replaced by a
     * rename, leave the stream on the entry after them. */
    assert(mkdir("/tmp/f", 0755) == 0);
    touch("/tmp/f/p");
    touch("/tmp/f/q");
    touch("/tmp/f/r");
    d = opendir("/tmp/f");
    assert(!strcmp(next(d), ".") && !strcmp(next(d), ".."));
    assert(_rename("/tmp/f/r", "/tmp/r") == 0);
    assert(!strcmp(next(d), "q"));
    assert(_rename("/tmp/r", "/tmp/f/p") == 0);
    assert(!strcmp(next(d), ""));
    assert(closedir(d) == 0);

    /* A file's times: none while the clock was never set, the clock's time
     * once it is, carried over a restart or not. */
    {
        struct stat st;
        time_t now = time(NULL);

        clock_state = SURFACE_CLOCK_UNSET;
        touch("/tmp/undated");
        assert(_stat("/tmp/undated", &st) == 0 && st.st_mtime == 0);
        clock_state = SURFACE_CLOCK_RESUMED;
        touch("/tmp/resumed");
        assert(_stat("/tmp/resumed", &st) == 0 && st.st_mtime >= now && st.st_mtime <= now + 5);
        clock_state = SURFACE_CLOCK_SET;
        touch("/tmp/dated");
        assert(_stat("/tmp/dated", &st) == 0 && st.st_mtime >= now && st.st_mtime <= now + 5);
    }

    /* /Board: the kernel's namespace through compat/posix, read-only. */
    {
        char got[8];
        struct stat st;
        int fd, calls;
        DIR *bd;
        struct dirent *be;
        int listed = 0;

        fd = _open("/Board/sys/uptime", O_RDONLY, 0);
        assert(fd >= 0 && board_fd == 3);
        assert(_read(fd, got, sizeof got) == 3 && memcmp(got, "77\n", 3) == 0);
        assert(_read(fd, got, sizeof got) == 0);
        assert(_lseek(fd, 1, SEEK_SET) == 1 && _read(fd, got, 1) == 1 && got[0] == '7');
        assert(_fstat(fd, &st) == 0 && S_ISREG(st.st_mode));
        assert(_close(fd) == 0 && board_fd == -1);
        errno = 0;
        assert(_close(fd) == -1 && errno == EBADF);
        calls = board_calls;
        errno = 0;
        assert(_open("/Board/sys/uptime", O_WRONLY, 0) == -1 && errno == EROFS);
        assert(board_calls == calls);
        errno = 0;
        assert(_open("/Board/sys/missing", O_RDONLY, 0) == -1 && errno == ENOENT);
        assert(_stat("/Board", &st) == 0 && S_ISDIR(st.st_mode) && st.st_dev == 3);
        assert(_stat("/Board/sys/uptime", &st) == 0 && S_ISREG(st.st_mode));
        assert(access("/Board/sys/uptime", R_OK) == 0);
        errno = 0;
        assert(access("/Board/sys/uptime", W_OK) == -1 && errno == EROFS);
        errno = 0;
        assert(_unlink("/Board/sys/uptime") == -1 && errno == EROFS);
        errno = 0;
        assert(mkdir("/Board/new", 0755) == -1 && errno == EROFS);
        errno = 0;
        assert(_rename("/tmp/dated", "/Board/x") == -1 && errno == EROFS);
        bd = opendir("/Board/sys");
        assert(bd != NULL && !strcmp(next(bd), "uptime") && !strcmp(next(bd), ""));
        rewinddir(bd);
        assert(!strcmp(next(bd), "uptime") && closedir(bd) == 0 && board_pos == -1);
        bd = opendir("/");
        while (bd != NULL && (be = readdir(bd)) != NULL) {
            if (strcmp(be->d_name, "Board") == 0) listed = 1;
        }
        assert(bd != NULL && closedir(bd) == 0 && listed);
        assert(chdir("/Board") == 0);
        fd = _open("sys/uptime", O_RDONLY, 0);
        assert(fd >= 0 && _close(fd) == 0);
        assert(chdir("/") == 0);
    }

    test_settings_store();
    puts("posix layer: the home's note is seeded; directory streams outlive the "
         "entries they reach; files have no date while the clock is unset; "
         "/Board reads through compat/posix; the settings files are the store's");
    return 0;
}
