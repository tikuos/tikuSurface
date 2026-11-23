/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * fake_store.h - a stand-in for TikuOS compat/posix's store group on a host:
 * files under /data/ written through private stages and published by fsync,
 * close and rename, as the library does, without its waiting.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_FAKE_STORE_H
#define SURFACE_FAKE_STORE_H
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#define FAKE_FD_BASE 20
#define FAKE_FILES 16
#define FAKE_FDS 6
#define FAKE_BYTES 16384

typedef struct { int used; char name[64]; unsigned char data[FAKE_BYTES]; size_t len; } fake_file_t;
typedef struct {
    int used, mode, dirty, append, dir;
    char name[64];
    unsigned char stage[FAKE_BYTES];
    size_t len, off;
} fake_fd_t;
static fake_file_t fake_files[FAKE_FILES];
static fake_fd_t fake_fds[FAKE_FDS];
/* Publications made; a publish that fails with EIO while fake_fail is set;
 * fake_during, when set, runs inside each publish, as the wait would. */
static unsigned fake_publishes, fake_renames, fake_dir_syncs;
static int fake_fail;
static void (*fake_during)(void);

static int fake_path(const char *path)
{
    return strncmp(path, "/data", 5) == 0 && (path[5] == '\0' || path[5] == '/');
}
static int fake_fd(int fd) { return fd >= FAKE_FD_BASE && fd < FAKE_FD_BASE + FAKE_FDS; }
static fake_file_t *fake_find(const char *name)
{
    for (int i = 0; i < FAKE_FILES; i++) {
        if (fake_files[i].used && strcmp(fake_files[i].name, name) == 0) return &fake_files[i];
    }
    return NULL;
}
/** @brief A file the store holds: what a publication writes. */
static int fake_put(const char *name, const void *data, size_t len)
{
    fake_file_t *f = fake_find(name);

    if (fake_during != NULL) fake_during();
    if (fake_fail) {
        errno = EIO;
        return -1;
    }
    for (int i = 0; f == NULL && i < FAKE_FILES; i++) {
        if (!fake_files[i].used) f = &fake_files[i];
    }
    if (f == NULL || len > FAKE_BYTES) {
        errno = ENOSPC;
        return -1;
    }
    f->used = 1;
    snprintf(f->name, sizeof f->name, "%s", name);
    memcpy(f->data, data, len);
    f->len = len;
    fake_publishes++;
    return 0;
}
static void fake_reset(void)
{
    memset(fake_files, 0, sizeof fake_files);
    memset(fake_fds, 0, sizeof fake_fds);
    fake_publishes = fake_renames = fake_dir_syncs = 0;
    fake_fail = 0;
    fake_during = NULL;
}
static int fake_open(const char *path, int flags)
{
    const char *name = path[5] == '\0' ? "" : path + 6;
    int acc = flags & O_ACCMODE, write = acc != O_RDONLY || (flags & (O_CREAT | O_TRUNC | O_APPEND));
    fake_file_t *f = fake_find(name);

    for (int i = 0; i < FAKE_FDS; i++) {
        fake_fd_t *d = &fake_fds[i];
        if (d->used) continue;
        memset(d, 0, sizeof *d);
        snprintf(d->name, sizeof d->name, "%s", name);
        if (*name == '\0') {
            if (write) { errno = EISDIR; return -1; }
            d->used = d->dir = 1;
            return FAKE_FD_BASE + i;
        }
        if (f == NULL && !(flags & O_CREAT)) { errno = ENOENT; return -1; }
        if (f != NULL && (flags & O_CREAT) && (flags & O_EXCL)) { errno = EEXIST; return -1; }
        d->used = 1;
        d->mode = acc;
        d->append = (flags & O_APPEND) != 0;
        if (f != NULL && !(flags & O_TRUNC)) {
            memcpy(d->stage, f->data, f->len);
            d->len = f->len;
        }
        d->dirty = write && (f == NULL || (flags & O_TRUNC));
        return FAKE_FD_BASE + i;
    }
    errno = EMFILE;
    return -1;
}
static fake_fd_t *fake_slot(int fd)
{
    fake_fd_t *d = fake_fd(fd) ? &fake_fds[fd - FAKE_FD_BASE] : NULL;
    if (d == NULL || !d->used) { errno = EBADF; return NULL; }
    return d;
}
static ssize_t fake_read(int fd, void *buf, size_t n)
{
    fake_fd_t *d = fake_slot(fd);
    if (d == NULL) return -1;
    if (d->off >= d->len) return 0;
    if (n > d->len - d->off) n = d->len - d->off;
    memcpy(buf, d->stage + d->off, n);
    d->off += n;
    return (ssize_t)n;
}
static ssize_t fake_write(int fd, const void *buf, size_t n)
{
    fake_fd_t *d = fake_slot(fd);
    size_t at;
    if (d == NULL) return -1;
    if (d->mode == O_RDONLY) { errno = EBADF; return -1; }
    at = d->append ? d->len : d->off;
    if (at + n > FAKE_BYTES) { errno = EFBIG; return -1; }
    memcpy(d->stage + at, buf, n);
    if (at + n > d->len) d->len = at + n;
    d->off = at + n;
    d->dirty = 1;
    return (ssize_t)n;
}
static off_t fake_lseek(int fd, off_t offset, int whence)
{
    fake_fd_t *d = fake_slot(fd);
    off_t to;
    if (d == NULL) return -1;
    to = whence == SEEK_SET ? offset : whence == SEEK_CUR ? (off_t)d->off + offset :
         whence == SEEK_END ? (off_t)d->len + offset : -1;
    if (to < 0) { errno = EINVAL; return -1; }
    d->off = (size_t)to;
    return to;
}
static int fake_fsync(int fd)
{
    fake_fd_t *d = fake_slot(fd);
    if (d == NULL) return -1;
    if (d->dir) { fake_dir_syncs++; return 0; }
    if (!d->dirty) return 0;
    if (fake_put(d->name, d->stage, d->len) != 0) return -1;
    d->dirty = 0;
    return 0;
}
static int fake_close(int fd)
{
    fake_fd_t *d = fake_slot(fd);
    int r;
    if (d == NULL) return -1;
    r = d->dirty && !d->dir ? fake_fsync(fd) : 0;
    d->used = 0;
    return r;
}
static int fake_fstat(int fd, struct stat *st)
{
    fake_fd_t *d = fake_slot(fd);
    if (d == NULL) return -1;
    memset(st, 0, sizeof *st);
    st->st_mode = d->dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    st->st_size = (off_t)d->len;
    return 0;
}
static int fake_stat(const char *path, struct stat *st)
{
    fake_file_t *f = fake_find(path[5] == '\0' ? "" : path + 6);
    memset(st, 0, sizeof *st);
    if (path[5] == '\0') { st->st_mode = S_IFDIR | 0755; return 0; }
    if (f == NULL) { errno = ENOENT; return -1; }
    st->st_mode = S_IFREG | 0644;
    st->st_size = (off_t)f->len;
    return 0;
}
static int fake_rename(const char *from, const char *to)
{
    fake_file_t *a = fake_find(from + 6), *b = fake_find(to + 6);
    if (a == NULL) { errno = ENOENT; return -1; }
    if (a == b) return 0;
    if (fake_during != NULL) fake_during();
    if (fake_fail) { errno = EIO; return -1; }
    if (b != NULL) b->used = 0;
    snprintf(a->name, sizeof a->name, "%s", to + 6);
    fake_renames++;
    return 0;
}
static int fake_unlink(const char *path)
{
    fake_file_t *f = fake_find(path + 6);
    if (f == NULL) { errno = ENOENT; return -1; }
    f->used = 0;
    return 0;
}
/** @brief Whether the store holds @p text under @p name. */
static int fake_holds(const char *name, const char *text)
{
    fake_file_t *f = fake_find(name);
    return f != NULL && f->len == strlen(text) && memcmp(f->data, text, f->len) == 0;
}
#endif
