/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_posix_card.c - the desktop's file layer writing to the SD card: the
 * real bridge, media service and MFS on a RAM disk under /Card.
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
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/xattr.h>
#include <unistd.h>
#include "bridge.h"
#include "tiku_crypto.h"
#include "tiku_posix.h"
#include "tikuos/card_fixture.h"

int _open(const char *path, int flags, int mode);
int _close(int fd);
int _unlink(const char *path);
int _rename(const char *old, const char *new);
ssize_t _read(int fd, void *buf, size_t n);
ssize_t _write(int fd, const void *buf, size_t n);
off_t _lseek(int fd, off_t offset, int whence);
int _stat(const char *path, struct stat *st);
int surface_desktop_files(void);
int mkstemp(char *tmpl);

/* The desktop side the layer calls: the card is the fixture's. */
surface_desktop_bridge_t surface_desktop_bridge;
char **environ;
void surface_desktop_card(surface_media_io_t *io) { card_fixture_request(io); }
unsigned surface_desktop_card_foreign(void) { return card_fixture_foreign(); }
void surface_desktop_log(const void *bytes, size_t length) { (void)bytes; (void)length; }
uint64_t surface_desktop_now_us(void) { return 1000000u; }
void surface_os_desktop_exit(void) {}
void surface_os_desktop_kick(void) {}
void surface_os_desktop_lock(void) {}
void surface_os_desktop_unlock(void) {}
void surface_os_desktop_heap_use(size_t *used, size_t *total, uint32_t *refused, size_t *largest)
{
    if (used != NULL) *used = 1u << 20;
    if (total != NULL) *total = 4u << 20;
    if (refused != NULL) *refused = 0;
    if (largest != NULL) *largest = 0;
}
void surface_os_desktop_wait(int (*ready)(void *), void *context, uint64_t timeout_us)
{ (void)ready; (void)context; (void)timeout_us; }
int surface_os_desktop_random(void *bytes, size_t length) { memset(bytes, 7, length); return 0; }
int surface_os_clock_state(void) { return SURFACE_CLOCK_UNSET; }

#include "fake_store.h"
int tiku_posix_open(const char *path, int flags)
{ if (fake_path(path)) return fake_open(path, flags); errno = ENOENT; return -1; }
ssize_t tiku_posix_read(int fd, void *buf, size_t n)
{ if (fake_fd(fd)) return fake_read(fd, buf, n); errno = EBADF; return -1; }
off_t tiku_posix_lseek(int fd, off_t offset, int whence)
{ if (fake_fd(fd)) return fake_lseek(fd, offset, whence); errno = EBADF; return -1; }
int tiku_posix_close(int fd) { if (fake_fd(fd)) return fake_close(fd); errno = EBADF; return -1; }
int tiku_posix_fcntl(int fd, int cmd) { (void)fd; (void)cmd; return O_RDONLY; }
int tiku_posix_fstat(int fd, struct stat *st)
{ if (fake_fd(fd)) return fake_fstat(fd, st); errno = EBADF; return -1; }
int tiku_posix_stat(const char *path, struct stat *st)
{ if (fake_path(path)) return fake_stat(path, st); errno = ENOENT; return -1; }
int tiku_posix_is_open(int fd) { return fake_slot(fd) != NULL; }
ssize_t tiku_posix_write(int fd, const void *buf, size_t n) { return fake_write(fd, buf, n); }
int tiku_posix_fsync(int fd) { return fake_fsync(fd); }
int tiku_posix_rename(const char *from, const char *to)
{ if (!fake_path(from) || !fake_path(to)) { errno = EXDEV; return -1; } return fake_rename(from, to); }
int tiku_posix_unlink(const char *path) { return fake_path(path) ? fake_unlink(path) : (errno = EROFS, -1); }
DIR *tiku_posix_opendir(const char *path) { (void)path; errno = ENOENT; return NULL; }
struct dirent *tiku_posix_readdir(DIR *dir) { (void)dir; return NULL; }
void tiku_posix_rewinddir(DIR *dir) { (void)dir; }
int tiku_posix_closedir(DIR *dir) { (void)dir; errno = EBADF; return -1; }
int tiku_posix_is_dir_stream(const DIR *dir) { (void)dir; return 0; }
uint8_t tiku_posix_authority_set(uint8_t cap) { (void)cap; return 0; }
void tiku_sha256(const void *data, size_t n, unsigned char out[32]) { (void)data; (void)n; memset(out, 1, 32); }

#define CARD_WRITE_PROBE 65536u     /* the largest file the layer publishes */
static unsigned char big[100u * 1024u], buf[100u * 1024u];

/** @brief The bytes @p path reads back through the layer, or -1. */
static ssize_t slurp(const char *path, void *out, size_t max)
{
    int fd = _open(path, O_RDONLY, 0);
    ssize_t n, got = 0;

    if (fd < 0) return -1;
    while ((size_t)got < max && (n = _read(fd, (char *)out + got, max - (size_t)got)) > 0) got += n;
    assert(_close(fd) == 0);
    return got;
}

/** @brief Write @p text to @p path through the layer: 0, or close's -1. */
static int put(const char *path, int flags, const char *text)
{
    int fd = _open(path, O_WRONLY | O_CREAT | flags, 0644);

    assert(fd >= 0);
    assert(_write(fd, text, strlen(text)) == (ssize_t)strlen(text));
    return _close(fd);
}

/** @brief Whether card object @p name holds exactly @p text. */
static int card_has(const char *name, const char *text)
{
    long n = card_fixture_get(name, buf, sizeof buf);

    return n == (long)strlen(text) && memcmp(buf, text, (size_t)n) == 0;
}

/** @brief Whether @p path reads back exactly @p text through the layer. */
static int card_read_is(const char *path, const char *text)
{
    static char got[256];
    ssize_t n = slurp(path, got, sizeof got);

    return n == (ssize_t)strlen(text) && memcmp(got, text, (size_t)n) == 0;
}

/** @brief The names in directory @p path, as one string. */
static const char *names(const char *path)
{
    static char out[512];
    DIR *d = opendir(path);
    struct dirent *e;

    out[0] = '\0';
    assert(d != NULL);
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        strcat(out, e->d_name);
        strcat(out, " ");
    }
    assert(closedir(d) == 0);
    return out;
}

int main(void)
{
    struct stat st;
    struct statvfs sv;
    char tmp[PATH_MAX];
    unsigned replaces;
    int fd;

    for (size_t i = 0; i < sizeof big; i++) big[i] = (unsigned char)(i * 7u);
    fake_reset();
    assert(!fake_holds("/data/ts/settings", ""));
    card_fixture_reset();
    card_fixture_put("notes/hello.txt", "TikuOS hello card\n", 18);
    card_fixture_put("audio/big.wav", big, sizeof big);
    card_fixture_put("samples/a.bin", big, 4116);
    /* Hidden objects of large writes an earlier start left, and one that is
     * not this layer's. */
    card_fixture_put(".~7", "left", 4);
    card_fixture_unfinished(".~9", big, 5000);
    card_fixture_put(".~x", "other", 5);
    card_fixture_put("photos/.folder", "", 0);
    card_fixture_put("loose/only.txt", "x", 1);
    /* A Trash left by an earlier start, with its record of origins. */
    card_fixture_put("home/.Trash/.folder", "", 0);
    card_fixture_put("home/.Trash/old.txt", "old", 3);
    card_fixture_put("home/.Trash/.origins", "old.txt\t/home/Documents/old.txt\n", 32);
    surface_desktop_bridge.card_mounted = 1;
    assert(surface_desktop_files() == 0);

    /* The card mounted read-write, its own size, its folders listed. */
    assert(slurp("/proc/self/mounts", buf, sizeof buf) > 0 && strstr((char *)buf, "/Card mfs rw") != NULL);
    assert(statvfs("/Card", &sv) == 0 && sv.f_blocks == 64u * 256u && sv.f_bfree < sv.f_blocks &&
           sv.f_namemax == 63u);
    /* Tracker asks every volume's space whenever it looks at the volumes:
     * the card is asked once until the layer changes it. */
    {
        unsigned asked = card_fixture_requests(SURFACE_MEDIA_SPACE);
        fsblkcnt_t before = sv.f_bfree;

        for (int i = 0; i < 5; i++) assert(statvfs("/home/Documents", &sv) == 0 && sv.f_bfree == before);
        assert(card_fixture_requests(SURFACE_MEDIA_SPACE) == asked);
        assert(put("/home/Documents/space.bin", 0, "a few bytes") == 0);
        {
            int fd = _open("/home/Documents/space.bin", O_RDONLY, 0);
            struct statvfs fv;

            assert(fd >= 0 && fstatvfs(fd, &fv) == 0 && fv.f_blocks == 64u * 256u && fv.f_namemax == 63u);
            assert(_close(fd) == 0);
        }
        assert(statvfs("/Card", &sv) == 0 && card_fixture_requests(SURFACE_MEDIA_SPACE) == asked + 1u);
        assert(_unlink("/home/Documents/space.bin") == 0);
    }
    assert(strstr(names("/Card"), "notes") != NULL && strstr(names("/Card"), "audio") != NULL);
    assert(slurp("/Card/notes/hello.txt", buf, sizeof buf) == 18 && memcmp(buf, "TikuOS hello", 12) == 0);
    /* A folder whose placeholder is on the card is listed, and empty. */
    assert(strstr(names("/Card"), "photos") != NULL && strcmp(names("/Card/photos"), "") == 0);
    assert(_stat("/Card/photos", &st) == 0 && S_ISDIR(st.st_mode));

    /* The home is the card's home folder: Desktop and Documents made there
     * at the first start, Welcome.txt in Documents; /Card does not show it,
     * and the desktop's own state folders stay off the card. */
    assert(card_has("home/Desktop/.folder", "") && card_has("home/Documents/.folder", ""));
    assert(card_has("home/Documents/Welcome.txt", "TikuSurface runs TikuDesktop's shell on TikuOS.\n"));
    assert(strstr(names("/Card"), "home") == NULL && mkdir("/Card/home", 0755) == -1 && errno == EEXIST);
    {
        struct stat home, config, card;

        assert(_stat("/home", &home) == 0 && _stat("/home/.config", &config) == 0 && _stat("/Card", &card) == 0);
        assert(home.st_dev == card.st_dev && config.st_dev != card.st_dev);
    }
    {
        unsigned before = card_fixture_count();

        assert(put("/home/.config/scratch", 0, "not on the card") == 0 && card_fixture_count() == before);
    }
    assert(put("/home/Documents/note.txt", 0, "kept") == 0 && card_has("home/Documents/note.txt", "kept"));
    /* The Trash from the earlier start: its item knows where it came from,
     * and the record is not listed and not a name a file takes. */
    {
        char origin[64];
        ssize_t n = getxattr("/home/.Trash/old.txt", "user._trk/original_path", origin, sizeof origin);

        assert(n == 23 && memcmp(origin, "/home/Documents/old.txt", 23) == 0);
        assert(strcmp(names("/home/.Trash"), "old.txt ") == 0);
        assert(_open("/home/.Trash/.origins", O_WRONLY | O_CREAT, 0644) == -1 && errno == EINVAL);
    }
    /* Trashing is a rename on the card; the origin Stomme records goes into
     * the Trash's record with the earlier one. */
    assert(_rename("/home/Documents/note.txt", "/home/.Trash/note.txt") == 0);
    assert(card_has("home/.Trash/note.txt", "kept") && card_fixture_get("home/Documents/note.txt", buf, sizeof buf) == -1);
    assert(setxattr("/home/.Trash/note.txt", "user._trk/original_path", "/home/Documents/note.txt", 24, 0) == 0);
    assert(card_has("home/.Trash/.origins", "note.txt\t/home/Documents/note.txt\nold.txt\t/home/Documents/old.txt\n") ||
           card_has("home/.Trash/.origins", "old.txt\t/home/Documents/old.txt\nnote.txt\t/home/Documents/note.txt\n"));
    /* Put back: the record keeps only the item still in the Trash; emptied,
     * it goes. */
    assert(_rename("/home/.Trash/old.txt", "/home/Documents/old.txt") == 0);
    assert(card_has("home/.Trash/.origins", "note.txt\t/home/Documents/note.txt\n"));
    assert(_unlink("/home/.Trash/note.txt") == 0 && card_fixture_get("home/.Trash/.origins", buf, sizeof buf) == -1);
    assert(card_has("home/.Trash/.folder", "") && card_has("home/Documents/old.txt", "old"));

    /* A folder made here keeps a hidden placeholder, not listed. */
    assert(mkdir("/Card/k4-test", 0755) == 0);
    assert(card_has("k4-test/.folder", "") && strcmp(names("/Card/k4-test"), "") == 0);
    assert(mkdir("/Card/k4-test", 0755) == -1 && errno == EEXIST);

    /* A new file is on the card only once closed, whole. */
    fd = _open("/Card/k4-test/a.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fd >= 0 && _write(fd, "hel", 3) == 3 && _write(fd, "lo", 2) == 2);
    assert(card_fixture_get("k4-test/a.txt", buf, sizeof buf) == -1);
    assert(_close(fd) == 0 && card_has("k4-test/a.txt", "hello"));
    assert(_stat("/Card/k4-test/a.txt", &st) == 0 && st.st_size == 5 && S_ISREG(st.st_mode));
    assert(slurp("/Card/k4-test/a.txt", buf, sizeof buf) == 5 && memcmp(buf, "hello", 5) == 0);
    fd = _open("/Card/k4-test/empty", O_WRONLY | O_CREAT, 0644);
    assert(fd >= 0 && _close(fd) == 0 && card_has("k4-test/empty", ""));

    /* fsync publishes; the close after it has nothing more to write. */
    replaces = card_fixture_requests(SURFACE_MEDIA_REPLACE);
    fd = _open("/Card/k4-test/a.txt", O_WRONLY | O_TRUNC, 0);
    assert(fd >= 0 && _write(fd, "bye", 3) == 3 && fsync(fd) == 0 && card_has("k4-test/a.txt", "bye"));
    assert(_close(fd) == 0 && card_fixture_requests(SURFACE_MEDIA_REPLACE) == replaces + 1u);
    /* A writer without O_TRUNC starts from the card's bytes. */
    assert(put("/Card/k4-test/a.txt", O_APPEND, "!") == 0 && card_has("k4-test/a.txt", "bye!"));

    /* A card file is read in requests of up to 64 KiB, and tells stdio so. */
    {
        unsigned reads = card_fixture_requests(SURFACE_MEDIA_READ);

        assert(_stat("/Card/audio/big.wav", &st) == 0 && st.st_blksize == 65536);
        assert(slurp("/Card/audio/big.wav", buf, sizeof buf) == (ssize_t)sizeof big && memcmp(buf, big, sizeof big) == 0);
        assert(card_fixture_requests(SURFACE_MEDIA_READ) - reads <= 3u);
        assert(_stat("/home/.config", &st) == 0 && st.st_blksize == 512);
    }

    /* A file past one replacement streams: written front to back, it takes
     * the file's name at its close, and until then the card keeps the old
     * version whole.  The first large write removes the hidden objects an
     * earlier start left, and its own is gone after the close. */
    assert(strstr(names("/Card"), ".~") == NULL);
    assert(put("/Card/k4-test/full", O_TRUNC, "old") == 0);
    fd = _open("/Card/k4-test/full", O_WRONLY | O_TRUNC, 0);
    assert(fd >= 0 && _write(fd, big, CARD_WRITE_PROBE) == (ssize_t)CARD_WRITE_PROBE);
    assert(_write(fd, big + CARD_WRITE_PROBE, 1000) == 1000 && card_has("k4-test/full", "old"));
    /* Between pieces the card takes other changes. */
    assert(put("/Card/k4-test/side.txt", 0, "side") == 0 && card_has("k4-test/side.txt", "side"));
    assert(_lseek(fd, 0, SEEK_SET) == 0 && _write(fd, "x", 1) == -1 && errno == ESPIPE);
    {
        int rd = _open("/Card/k4-test/full", O_RDONLY, 0);

        assert(rd >= 0 && _read(rd, buf, 1) == -1 && errno == EBUSY && _close(rd) == 0);
    }
    assert(_lseek(fd, 0, SEEK_END) == (off_t)(CARD_WRITE_PROBE + 1000));
    assert(_write(fd, big + CARD_WRITE_PROBE + 1000, sizeof big - CARD_WRITE_PROBE - 1000) ==
           (ssize_t)(sizeof big - CARD_WRITE_PROBE - 1000));
    assert(card_has("k4-test/full", "old") && card_fixture_exists(".~1"));
    assert(!card_fixture_exists(".~7") && !card_fixture_exists(".~9") && card_fixture_exists(".~x"));
    assert(_close(fd) == 0 && !card_fixture_exists(".~1"));
    assert(card_fixture_get("k4-test/full", buf, sizeof buf) == (long)sizeof big && memcmp(buf, big, sizeof big) == 0);
    assert(slurp("/Card/k4-test/full", buf, sizeof buf) == (ssize_t)sizeof big && memcmp(buf, big, sizeof big) == 0);
    assert(strstr(names("/Card"), ".~") == NULL);
    assert(_open("/Card/.~3", O_WRONLY | O_CREAT, 0644) == -1 && errno == EINVAL);
    /* A writer that would start from such a file's card bytes is refused. */
    assert(_open("/Card/audio/big.wav", O_WRONLY, 0) == -1 && errno == EFBIG);
    /* A large file removed while written leaves nothing on the card. */
    fd = _open("/Card/k4-test/doomed", O_WRONLY | O_CREAT, 0644);
    assert(fd >= 0 && _write(fd, big, sizeof big) == (ssize_t)sizeof big && card_fixture_exists(".~2"));
    assert(_unlink("/Card/k4-test/doomed") == 0 && _write(fd, big, sizeof big) == (ssize_t)sizeof big);
    assert(_close(fd) == 0 && !card_fixture_exists(".~2") && !card_fixture_exists("k4-test/doomed"));

    /* Stomme's guarded save: a temporary, fsync, close, rename over the
     * file; the file is replaced in one step and the temporary is gone. */
    snprintf(tmp, sizeof tmp, "/Card/k4-test/a.txt.tmp-XXXXXX");
    fd = mkstemp(tmp);
    assert(fd >= 0 && _write(fd, "saved", 5) == 5 && fchmod(fd, 0644) == 0 && fsync(fd) == 0);
    assert(_close(fd) == 0 && _rename(tmp, "/Card/k4-test/a.txt") == 0);
    assert(card_has("k4-test/a.txt", "saved") && card_fixture_get(tmp + 6, buf, sizeof buf) == -1);
    assert(_stat(tmp, &st) == -1 && errno == ENOENT);
    /* The guarded save of a large file: the temporary streams, fsync commits
     * its pieces, and the rename puts it over the file in one transaction. */
    snprintf(tmp, sizeof tmp, "/Card/k4-test/full.tmp-XXXXXX");
    fd = mkstemp(tmp);
    assert(fd >= 0 && _write(fd, big + 1, sizeof big - 1) == (ssize_t)(sizeof big - 1) && fsync(fd) == 0);
    assert(card_fixture_get(".~3", buf, sizeof buf) == (long)sizeof big - 1 && memcmp(buf, big + 1, sizeof big - 1) == 0);
    assert(card_fixture_get("k4-test/full", buf, sizeof buf) == (long)sizeof big && memcmp(buf, big, sizeof big) == 0);
    {
        unsigned overs = card_fixture_requests(SURFACE_MEDIA_RENAME_OVER), deletes = card_fixture_requests(SURFACE_MEDIA_DELETE);

        assert(_close(fd) == 0 && card_fixture_requests(SURFACE_MEDIA_RENAME_OVER) == overs + 1u);
        assert(_rename(tmp, "/Card/k4-test/full") == 0 && card_fixture_requests(SURFACE_MEDIA_RENAME_OVER) == overs + 2u &&
               card_fixture_requests(SURFACE_MEDIA_DELETE) == deletes);
    }
    assert(card_fixture_get("k4-test/full", buf, sizeof buf) == (long)sizeof big - 1 &&
           memcmp(buf, big + 1, sizeof big - 1) == 0 && !card_fixture_exists(tmp + 6));

    /* Renames: a file to a new folder; the folder it leaves keeps a
     * placeholder when it was the folder's last file. */
    assert(mkdir("/Card/k4-test/sub", 0755) == 0);
    assert(put("/Card/k4-test/sub/longer-file-name.txt", 0, "long") == 0);
    assert(_rename("/Card/k4-test/a.txt", "/Card/k4-test/sub/b.txt") == 0);
    assert(card_has("k4-test/sub/b.txt", "saved") && card_fixture_get("k4-test/a.txt", buf, sizeof buf) == -1);
    assert(slurp("/Card/k4-test/sub/b.txt", buf, sizeof buf) == 5);
    assert(_rename("/Card/samples/a.bin", "/Card/k4-test/a.bin") == 0);
    assert(card_has("samples/.folder", "") && strstr(names("/Card"), "samples") != NULL);
    assert(card_fixture_get("k4-test/a.bin", buf, sizeof buf) == 4116 && memcmp(buf, big, 4116) == 0);
    /* /Card and the home are one volume: a move between them is a rename. */
    assert(_rename("/Card/k4-test/a.bin", "/home/Documents/a.bin") == 0);
    assert(card_fixture_get("home/Documents/a.bin", buf, sizeof buf) == 4116 &&
           card_fixture_get("k4-test/a.bin", buf, sizeof buf) == -1);
    assert(_rename("/home/Documents/a.bin", "/Card/k4-test/a.bin") == 0);

    /* A folder: every object under it, its placeholder among them. */
    assert(_rename("/Card/k4-test/sub", "/Card/k4-test/moved") == 0);
    assert(card_has("k4-test/moved/b.txt", "saved") && card_has("k4-test/moved/.folder", "") &&
           card_has("k4-test/moved/longer-file-name.txt", "long"));
    assert(card_fixture_get("k4-test/sub/b.txt", buf, sizeof buf) == -1 &&
           card_fixture_get("k4-test/sub/.folder", buf, sizeof buf) == -1);
    assert(slurp("/Card/k4-test/moved/b.txt", buf, sizeof buf) == 5);
    /* A name the folder fits but one of its files would outgrow changes
     * nothing. */
    assert(_rename("/Card/k4-test/moved", "/Card/k4-test/forty-bytes-of-folder-name-0123456789abcd") == -1 &&
           errno == ENAMETOOLONG && card_has("k4-test/moved/b.txt", "saved"));

    /* An object that refuses its rename part-way through a folder: those
     * already renamed are put back, and the folder stays where it was. */
    assert(mkdir("/Card/k4-test/held", 0755) == 0);
    assert(put("/Card/k4-test/held/1", 0, "one") == 0 && put("/Card/k4-test/held/2", 0, "two") == 0 &&
           put("/Card/k4-test/held/3", 0, "three") == 0);
    card_fixture_pending("k4-test/held/2");
    assert(_rename("/Card/k4-test/held", "/Card/k4-test/gone") == -1 && errno == EBUSY);
    assert(card_has("k4-test/held/1", "one") && card_has("k4-test/held/2", "two") &&
           card_has("k4-test/held/3", "three") && card_has("k4-test/held/.folder", ""));
    assert(card_fixture_get("k4-test/gone/1", buf, sizeof buf) == -1 &&
           card_fixture_get("k4-test/gone/3", buf, sizeof buf) == -1 &&
           card_fixture_get("k4-test/gone/.folder", buf, sizeof buf) == -1);
    assert(slurp("/Card/k4-test/held/3", buf, sizeof buf) == 5 && _stat("/Card/k4-test/gone", &st) == -1);

    /* The card and the tree's own folders are two file systems. */
    assert(_rename("/Card/k4-test/moved/b.txt", "/tmp/b.txt") == -1 && errno == EXDEV);
    assert(_rename("/home/.config/scratch", "/Card/k4-test/W.txt") == -1 && errno == EXDEV);
    assert(_rename("/home/.config/scratch", "/home/Documents/scratch") == -1 && errno == EXDEV);

    /* Removal: the file, then its folder with the placeholder. */
    assert(rmdir("/Card/k4-test/moved") == -1 && errno == ENOTEMPTY);
    assert(_unlink("/Card/k4-test/moved/b.txt") == 0 && card_fixture_get("k4-test/moved/b.txt", buf, sizeof buf) == -1);
    assert(_unlink("/Card/k4-test/moved/longer-file-name.txt") == 0);
    assert(card_has("k4-test/moved/.folder", "") && rmdir("/Card/k4-test/moved") == 0);
    /* A folder that was only its file's prefix stays when the file goes. */
    assert(_unlink("/Card/loose/only.txt") == 0 && card_has("loose/.folder", "") &&
           card_fixture_get("loose/only.txt", buf, sizeof buf) == -1);
    assert(_stat("/Card/loose", &st) == 0 && strcmp(names("/Card/loose"), "") == 0);
    assert(card_fixture_get("k4-test/moved/.folder", buf, sizeof buf) == -1);
    assert(_stat("/Card/k4-test/moved", &st) == -1 && errno == ENOENT);

    /* Names the card does not take. */
    assert(_open("/Card/k4-test/.folder", O_WRONLY | O_CREAT, 0644) == -1 && errno == EINVAL);
    assert(_open("/Card/k4-test/back\\slash", O_WRONLY | O_CREAT, 0644) == -1 && errno == EINVAL);
    {
        char path[96];

        snprintf(path, sizeof path, "/Card/k4-test/%.56s", "a-name-that-takes-the-card-past-its-sixty-three-bytes-ok");
        assert(_open(path, O_WRONLY | O_CREAT, 0644) == -1 && errno == ENAMETOOLONG);
    }

    /* A card that fails: nothing is published; a new file goes, an old one
     * reads its card version again. */
    card_fixture_fail_writes(1);
    assert(put("/Card/k4-test/lost", 0, "x") == -1 && errno == EIO);
    assert(_stat("/Card/k4-test/lost", &st) == -1 && errno == ENOENT);
    assert(put("/Card/k4-test/empty", O_TRUNC, "changed") == -1 && errno == EIO);
    assert(_stat("/Card/k4-test/empty", &st) == 0 && st.st_size == 0);
    fd = _open("/Card/k4-test/large-lost", O_WRONLY | O_CREAT, 0644);
    assert(fd >= 0 && _write(fd, big, CARD_WRITE_PROBE) == (ssize_t)CARD_WRITE_PROBE);
    assert(_write(fd, big, 10) == -1 && errno == EIO && _write(fd, big, 1) == -1 && errno == EIO);
    assert(_close(fd) == -1 && errno == EIO);
    assert(_stat("/Card/k4-test/large-lost", &st) == -1 && errno == ENOENT);
    fd = _open("/Card/k4-test/full", O_WRONLY | O_TRUNC, 0);
    assert(fd >= 0 && _write(fd, big, sizeof big) == -1 && errno == EIO && _close(fd) == -1 && errno == EIO);
    assert(_stat("/Card/k4-test/full", &st) == 0 && st.st_size == (off_t)sizeof big - 1);
    card_fixture_fail_writes(0);

    /* After a remount the layer finds its files again by name. */
    card_fixture_remount();
    assert(card_has("k4-test/empty", "") && card_has("notes/hello.txt", "TikuOS hello card\n"));
    assert(card_fixture_get("k4-test/full", buf, sizeof buf) == (long)sizeof big - 1 &&
           memcmp(buf, big + 1, sizeof big - 1) == 0);
    fd = _open("/Card/k4-test/a.bin", O_RDONLY, 0);
    assert(fd >= 0 && _lseek(fd, 2000, SEEK_SET) == 2000 && _read(fd, buf, 16) == 16);
    assert(memcmp(buf, big + 2000, 16) == 0 && _close(fd) == 0);

    /* Another client's changes: a file it removed is refused and leaves the
     * tree, though the layer kept its first bytes; one it replaced refuses a
     * rename once, then reads as replaced; a removed name can be made again.
     * The layer's own changes send no file to be found again. */
    {
        unsigned enumerates;

        assert(put("/Card/k4-test/gone.txt", 0, "kept head") == 0 && card_read_is("/Card/k4-test/gone.txt", "kept head"));
        assert(put("/Card/k4-test/swap.txt", 0, "old bytes") == 0 && card_read_is("/Card/k4-test/swap.txt", "old bytes"));
        enumerates = card_fixture_requests(SURFACE_MEDIA_ENUMERATE);
        assert(put("/Card/k4-test/again.txt", 0, "first") == 0);
        assert(put("/Card/k4-test/target.txt", 0, "target") == 0 && put("/Card/k4-test/mover.txt", 0, "mover") == 0);
        fd = _open("/Card/k4-test/streamed", O_WRONLY | O_CREAT, 0644);
        assert(fd >= 0 && _write(fd, big, sizeof big) == (ssize_t)sizeof big && _close(fd) == 0);
        assert(card_read_is("/Card/k4-test/gone.txt", "kept head"));
        assert(card_fixture_requests(SURFACE_MEDIA_ENUMERATE) == enumerates);

        card_fixture_delete("k4-test/gone.txt");
        card_fixture_delete("k4-test/swap.txt");
        card_fixture_put("k4-test/swap.txt", "new bytes!", 10);
        card_fixture_delete("k4-test/again.txt");
        card_fixture_delete("k4-test/target.txt");
        assert(_open("/Card/k4-test/gone.txt", O_RDONLY, 0) == -1 && errno == ENOENT);
        assert(_stat("/Card/k4-test/gone.txt", &st) == -1 && errno == ENOENT);
        assert(_rename("/Card/k4-test/swap.txt", "/Card/k4-test/swapped.txt") == -1 && errno == ESTALE);
        assert(card_read_is("/Card/k4-test/swap.txt", "new bytes!"));
        assert(_rename("/Card/k4-test/swap.txt", "/Card/k4-test/swapped.txt") == 0 &&
               card_has("k4-test/swapped.txt", "new bytes!"));
        assert(put("/Card/k4-test/again.txt", O_TRUNC, "made again") == 0 && card_has("k4-test/again.txt", "made again"));
        assert(_rename("/Card/k4-test/mover.txt", "/Card/k4-test/target.txt") == 0 &&
               card_has("k4-test/target.txt", "mover") && card_fixture_get("k4-test/mover.txt", buf, sizeof buf) == -1);
    }

    /* The files that were on the card are as they were. */
    assert(card_fixture_get("audio/big.wav", buf, sizeof buf) == (long)sizeof big && memcmp(buf, big, sizeof big) == 0);
    puts("posix card: the home is the card's home folder with Desktop, Documents and a Trash; the card mounts read-write with its own size; folders keep a hidden placeholder; files up to 64 KiB publish whole at close or fsync, larger ones stream and take the name at close; the guarded save replaces in one step; files and folders rename on the card, folders object by object; the card and the tree are separate; removals keep the folder; refused names, a failing card, a remount and another client's changes");
    return 0;
}
