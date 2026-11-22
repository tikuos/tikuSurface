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
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "bridge.h"
#include "tiku_crypto.h"

int _open(const char *path, int flags, int mode);
int _close(int fd);
int _unlink(const char *path);
int _rename(const char *old, const char *new);
int _stat(const char *path, struct stat *st);
int surface_desktop_files(void);

/* The desktop side the layer calls: no card, no log, a fixed clock. */
surface_desktop_bridge_t surface_desktop_bridge;
char **environ;
void surface_desktop_card(surface_media_io_t *io) { io->result = SURFACE_MEDIA_UNAVAILABLE; }
void surface_desktop_log(const void *bytes, size_t length) { (void)bytes; (void)length; }
uint64_t surface_desktop_now_us(void) { return 1000000u; }
void surface_os_desktop_exit(void) {}
void surface_os_desktop_kick(void) {}
void surface_os_desktop_lock(void) {}
void surface_os_desktop_unlock(void) {}
void surface_os_desktop_wait(int (*ready)(void *), void *context, uint64_t timeout_us)
{ (void)ready; (void)context; (void)timeout_us; }
int surface_os_desktop_random(void *bytes, size_t length) { memset(bytes, 7, length); return 0; }
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

    puts("posix layer: the home's note is seeded; directory streams outlive the "
         "entries they reach");
    return 0;
}
