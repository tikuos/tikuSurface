/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * posix.c - the POSIX and C-library services the desktop calls, on TikuOS.
 *
 * Files live in a tree held in the desktop's PSRAM heap: the desktop's home,
 * its settings and the /proc files the volume kit reads.  newlib's file
 * calls reach this tree through the system calls defined here, which take
 * precedence over libnosys.  The desktop is the only thread that calls
 * these functions; the C library's heap grows inside the arena the runtime
 * hands over, under the interrupt-masked allocator lock.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bridge.h"
#include "tiku_crypto.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <reent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef SURFACE_EPOCH
#define SURFACE_EPOCH 1795000000LL
#endif
#define NAME_MAX_BYTES 128
#define FDS 48
#define FIRST_FD 3

/*---------------------------------------------------------------------------*/
/* THE TREE                                                                  */
/*---------------------------------------------------------------------------*/

typedef struct rnode {
    char name[NAME_MAX_BYTES];
    struct rnode *parent, *child, *next;
    unsigned char *data;
    size_t size, cap;
    mode_t mode;
    ino_t ino;
    time_t mtime;
    int opened, unlinked;
    struct card_node *card;    /* set for nodes under /Card */
    uint32_t born, counted;    /* observation window it was created in, and */
    uint32_t changes;          /* its changes counted in window `counted` */
} rnode_t;

static rnode_t *root;
/* Every change made to the tree counts here: creation, removal, renaming,
 * writing, truncation, and modes and times that change.  A call that
 * changes nothing does not count.  tiku_posix_mutations() hands the count
 * to the posix backend, which reports a quiet tree to the views; each call
 * starts a new observation window.  A file created and removed within one
 * window is no change: its counts are taken back. */
static uint64_t mutations;
static uint32_t window = 1;

int tiku_posix_mutations(uint64_t *count)
{
    *count = mutations;
    window++;
    return 0;
}

/** @brief Count one change to @p n. */
static void changed(rnode_t *n)
{
    mutations++;
    if (n->counted != window) {
        n->counted = window;
        n->changes = 0;
    }
    n->changes++;
}

/** @brief Count the removal of @p n, or take back its changes when it was
 *  created in this window. */
static void removed(rnode_t *n)
{
    if (n->born == window && n->counted == window) mutations -= n->changes;
    else mutations++;
}
/* /dev/urandom and /dev/random: SHA-256 in counter mode over a key seeded
 * from the hardware random source when the desktop starts.  The key is
 * replaced after each read, so earlier output cannot be recomputed from
 * the state.  Without a seed, reads fail with EIO. */
static rnode_t random_device = { .name = "urandom", .mode = S_IFCHR | 0444, .ino = 1 };
static unsigned char random_key[32];
static uint64_t random_counter;
static int random_seeded;

/** @brief One SHA-256 block of the generator: the key, a domain byte (0 for
 *  output, 1 for the next key) and the counter. */
static void random_block(unsigned char domain, unsigned char out[32])
{
    unsigned char block[41];

    memcpy(block, random_key, 32);
    block[32] = domain;
    memcpy(block + 33, &random_counter, 8);
    random_counter++;
    tiku_sha256(block, sizeof block, out);
    memset(block, 0, sizeof block);
}

int surface_desktop_random_seed(void)
{
    unsigned char seed[56];
    uint64_t now = surface_desktop_now_us();

    if (surface_os_desktop_random(seed, 48) != 0) return -1;
    memcpy(seed + 48, &now, sizeof now);
    tiku_sha256(seed, sizeof seed, random_key);
    memset(seed, 0, sizeof seed);
    random_counter = 0;
    random_seeded = 1;
    return 0;
}

/** @brief Fill @p n bytes from the generator, then replace its key. */
static void random_fill(unsigned char *out, size_t n)
{
    unsigned char digest[32];

    while (n > 0) {
        size_t k = n < sizeof digest ? n : sizeof digest;

        random_block(0, digest);
        memcpy(out, digest, k);
        out += k;
        n -= k;
    }
    random_block(1, random_key);
    memset(digest, 0, sizeof digest);
}
static ino_t next_ino = 2;
static char cwd[PATH_MAX] = "/";

typedef struct {
    rnode_t *node;
    off_t pos;
    int flags;
    uint64_t reader;    /* a card file's open reader, or 0 */
} fd_t;
static fd_t fds[FDS];

/** @brief Seconds of wall time: the build's epoch plus the time since boot. */
static time_t wall(void)
{
    return (time_t)(SURFACE_EPOCH + (long long)(surface_desktop_now_us() / 1000000u));
}

static rnode_t *node_new(rnode_t *parent, const char *name, mode_t mode)
{
    rnode_t *n = calloc(1, sizeof *n);

    if (n == NULL) return NULL;
    n->born = window;
    changed(n);
    snprintf(n->name, sizeof n->name, "%s", name);
    n->mode = mode;
    n->ino = next_ino++;
    n->mtime = wall();
    n->parent = parent;
    if (parent != NULL) {
        n->next = parent->child;
        parent->child = n;
        parent->mtime = n->mtime;
    }
    return n;
}

static void streams_pass(const rnode_t *n);

/** @brief Take @p n out of its directory; an open directory stream about to
 *  return @p n goes on to the entry after it. */
static void node_detach(rnode_t *n)
{
    rnode_t **p;

    if (n->parent == NULL) return;
    streams_pass(n);
    for (p = &n->parent->child; *p != NULL; p = &(*p)->next) {
        if (*p == n) {
            *p = n->next;
            break;
        }
    }
    n->parent->mtime = wall();
    n->parent = NULL;
    n->next = NULL;
}

static void card_forget(rnode_t *n);

static void node_free(rnode_t *n)
{
    free(n->data);
    card_forget(n);
    free(n);
}

static rnode_t *child_named(rnode_t *dir, const char *name, size_t len)
{
    rnode_t *c;

    for (c = dir->child; c != NULL; c = c->next) {
        if (strlen(c->name) == len && memcmp(c->name, name, len) == 0) return c;
    }
    return NULL;
}

/*---------------------------------------------------------------------------*/
/* THE SD CARD AT /Card                                                      */
/*---------------------------------------------------------------------------*/
/* The card's native media volume names its objects with '/' inside the
 * name.  /Card shows each prefix as a directory, listed the first time a
 * path walks into it, and each object as a read-only file whose bytes come
 * through the kernel's media bridge.  Nothing here writes to the card. */
#define CARD_NAME 64u                 /* bytes of a media name, NUL included */
#define CARD_READ 1024u               /* bytes one read request returns */
#define CARD_ENTRIES 4096u            /* enumeration steps one listing takes */

typedef struct card_node {
    char name[CARD_NAME];   /* a file's full name; a directory's prefix */
    uint64_t epoch, rank;   /* where the enumeration found a file */
    int loaded;             /* a directory's entries are in the tree */
    unsigned char *head;    /* a file's first bytes, once read, or NULL */
    size_t head_len;        /* bytes in head */
} card_node_t;

/** @brief Release @p n's card information and the bytes it keeps. */
static void card_forget(rnode_t *n)
{
    if (n->card != NULL) free(n->card->head);
    free(n->card);
    n->card = NULL;
}

/** @brief Give @p n card information named @p name; zero, or -1. */
static int card_mark(rnode_t *n, const char *name, size_t len)
{
    n->card = calloc(1, sizeof *n->card);
    if (n->card == NULL || len >= CARD_NAME) return -1;
    memcpy(n->card->name, name, len);
    n->card->name[len] = '\0';
    return 0;
}

/** @brief List the card objects under @p dir's prefix into the tree: the
 *  first name segment past the prefix becomes a directory, a name without
 *  one a file.  A changed names epoch ends the listing where it stands. */
static void card_load(rnode_t *dir)
{
    surface_media_io_t io;
    char prefix[CARD_NAME];
    size_t n = strlen(dir->card->name);

    dir->card->loaded = 1;
    if (n + 2u > CARD_NAME) return;
    memcpy(prefix, dir->card->name, n);
    if (n) prefix[n++] = '/';
    prefix[n] = '\0';
    memset(&io, 0, sizeof io);
    memcpy(io.name, prefix, n + 1u);
    for (unsigned step = 0; step < CARD_ENTRIES; step++) {
        const char *rest, *slash;
        char found[CARD_NAME];

        io.operation = SURFACE_MEDIA_ENUMERATE;
        surface_desktop_card(&io);
        if (io.result != SURFACE_MEDIA_OK) break;
        memcpy(found, io.found, CARD_NAME);
        found[CARD_NAME - 1u] = '\0';
        if (strncmp(found, prefix, n) != 0) break;
        rest = found + n;
        slash = strchr(rest, '/');
        if (slash != NULL) {
            size_t d = (size_t)(slash - rest);
            char leaf[NAME_MAX_BYTES];

            if (d == 0 || d >= NAME_MAX_BYTES || n + d + 2u > CARD_NAME) break;
            memcpy(leaf, rest, d);
            leaf[d] = '\0';
            if (child_named(dir, leaf, d) == NULL) {
                rnode_t *c = node_new(dir, leaf, S_IFDIR | 0555);

                if (c == NULL || card_mark(c, found, n + d) != 0) break;
            }
            /* The next name past the whole subtree: '0' follows '/'. */
            memcpy(io.name, found, n + d);
            io.name[n + d] = '0';
            io.name[n + d + 1u] = '\0';
            io.exclusive = 0;
        } else {
            size_t f = strlen(rest);

            if (f > 0 && f < NAME_MAX_BYTES && child_named(dir, rest, f) == NULL) {
                rnode_t *c = node_new(dir, rest, S_IFREG | 0444);

                if (c == NULL || card_mark(c, found, strlen(found)) != 0) break;
                c->size = (size_t)io.size;
                c->card->epoch = io.epoch;
                c->card->rank = io.rank;
            }
            memcpy(io.name, found, CARD_NAME);
            io.exclusive = 1;
        }
    }
}

/** @brief @p dir's child named by @p len bytes of @p name, listing a card
 *  directory first. */
static rnode_t *child_in(rnode_t *dir, const char *name, size_t len)
{
    if (dir->card != NULL && S_ISDIR(dir->mode) && !dir->card->loaded) card_load(dir);
    return child_named(dir, name, len);
}

/** @brief Whether @p path names /Card or a path under it; such paths refuse
 *  changes with EROFS. */
static int card_path(const char *path)
{
    char full[PATH_MAX];

    if (path == NULL) return 0;
    if (path[0] != '/') {
        snprintf(full, sizeof full, "%s/%s", cwd, path);
        path = full;
    }
    while (path[0] == '/' && path[1] == '/') path++;
    if (strncmp(path, "/Card", 5) != 0 || (path[5] != '\0' && path[5] != '/')) return 0;
    errno = EROFS;
    return 1;
}

/** @brief Open @p n's object for reading; the reader id, or 0. */
static uint64_t card_open(const rnode_t *n)
{
    surface_media_io_t io;

    memset(&io, 0, sizeof io);
    io.operation = SURFACE_MEDIA_OPEN;
    memcpy(io.name, n->card->name, CARD_NAME);
    io.epoch = n->card->epoch;
    io.rank = n->card->rank;
    surface_desktop_card(&io);
    return io.result == SURFACE_MEDIA_OK ? io.reader : 0;
}

/** @brief Close a card reader. */
static void card_close(uint64_t reader)
{
    surface_media_io_t io;

    memset(&io, 0, sizeof io);
    io.operation = SURFACE_MEDIA_CLOSE;
    io.reader = reader;
    surface_desktop_card(&io);
}

/** @brief Read up to @p n bytes at @p at; the bytes read, or -1. */
static _ssize_t card_read(uint64_t reader, unsigned char *buf, size_t n, off_t at)
{
    size_t done = 0;

    while (done < n) {
        surface_media_io_t io;
        size_t want = n - done > CARD_READ ? CARD_READ : n - done;

        memset(&io, 0, sizeof io);
        io.operation = SURFACE_MEDIA_READ;
        io.reader = reader;
        io.offset = (uint64_t)at + done;
        io.bytes = buf + done;
        io.length = want;
        surface_desktop_card(&io);
        if (io.result != SURFACE_MEDIA_OK) {
            if (done) break;
            errno = EIO;
            return -1;
        }
        done += io.count;
        if (io.count < want) break;
    }
    return (_ssize_t)done;
}

/** @brief Resolve @p path; with @p parent, stop at the last component and
 *         hand back its name.  NULL with errno set when a component is
 *         missing or not a directory. */
static rnode_t *resolve(const char *path, rnode_t **parent, const char **leaf, size_t *leaf_len)
{
    rnode_t *n;
    const char *p, *end;

    if (path == NULL || *path == '\0') {
        errno = ENOENT;
        return NULL;
    }
    if (root == NULL) {
        errno = ENOENT;
        return NULL;
    }
    n = root;
    if (path[0] != '/') {
        rnode_t *base = resolve(cwd, NULL, NULL, NULL);

        n = (base != NULL) ? base : root;
    }
    p = path;
    for (;;) {
        while (*p == '/') p++;
        if (*p == '\0') {
            if (parent != NULL) {
                *parent = n->parent ? n->parent : n;
                *leaf = n->name;
                *leaf_len = strlen(n->name);
            }
            return n;
        }
        end = p;
        while (*end != '\0' && *end != '/') end++;
        {
            size_t len = (size_t)(end - p);
            const char *after = end;

            while (*after == '/') after++;
            if (!S_ISDIR(n->mode)) {
                errno = ENOTDIR;
                return NULL;
            }
            if (parent != NULL && *after == '\0') {
                if (len == 1 && p[0] == '.') {
                    *parent = n->parent ? n->parent : n;
                    *leaf = n->name;
                    *leaf_len = strlen(n->name);
                    return n;
                }
                *parent = n;
                *leaf = p;
                *leaf_len = len;
                if (len == 2 && p[0] == '.' && p[1] == '.') {
                    rnode_t *up = n->parent ? n->parent : n;

                    *parent = up->parent ? up->parent : up;
                    *leaf = up->name;
                    *leaf_len = strlen(up->name);
                    return up;
                }
                {
                    rnode_t *c = child_in(n, p, len);

                    if (c == NULL) errno = ENOENT;
                    return c;
                }
            }
            if (len == 1 && p[0] == '.') {
                /* stays */
            } else if (len == 2 && p[0] == '.' && p[1] == '.') {
                if (n->parent != NULL) n = n->parent;
            } else {
                rnode_t *c = child_in(n, p, len);

                if (c == NULL) {
                    errno = ENOENT;
                    return NULL;
                }
                n = c;
            }
            p = end;
        }
    }
}

static rnode_t *lookup(const char *path)
{
    return resolve(path, NULL, NULL, NULL);
}

static void fill_stat(const rnode_t *n, struct stat *st)
{
    memset(st, 0, sizeof *st);
    st->st_dev = n->card != NULL ? 2 : 1;     /* /Card is its own device */
    st->st_ino = n->ino;
    st->st_mode = n->mode;
    st->st_nlink = S_ISDIR(n->mode) ? 2 : 1;
    st->st_size = (off_t)n->size;
    st->st_blksize = 512;
    st->st_blocks = (blkcnt_t)((n->size + 511u) / 512u);
    st->st_mtim.tv_sec = n->mtime;
    st->st_atim.tv_sec = n->mtime;
    st->st_ctim.tv_sec = n->mtime;
}

static int reserve(rnode_t *n, size_t need)
{
    unsigned char *grown;
    size_t cap;

    if (need <= n->cap) return 0;
    cap = n->cap ? n->cap : 256u;
    while (cap < need) {
        if (cap > SIZE_MAX / 2u) {
            errno = EFBIG;
            return -1;
        }
        cap *= 2u;
    }
    grown = realloc(n->data, cap);
    if (grown == NULL) {
        errno = ENOSPC;
        return -1;
    }
    memset(grown + n->cap, 0, cap - n->cap);
    n->data = grown;
    n->cap = cap;
    return 0;
}

/** @brief Make every missing directory of @p path; 0, or -1. */
static int make_dirs(const char *path)
{
    char buf[PATH_MAX];
    char *p;

    snprintf(buf, sizeof buf, "%s", path);
    for (p = buf + 1; ; p++) {
        if (*p == '/' || *p == '\0') {
            char saved = *p;

            *p = '\0';
            if (lookup(buf) == NULL && mkdir(buf, 0755) != 0) return -1;
            *p = saved;
            if (saved == '\0') break;
        }
    }
    return 0;
}

/** @brief Create @p path with @p text, making its directories. */
static void seed(const char *path, const char *text)
{
    char dir[PATH_MAX];
    char *slash;
    int fd;

    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash != NULL && slash != dir) {
        *slash = '\0';
        (void)make_dirs(dir);
    }
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        (void)write(fd, text, strlen(text));
        (void)close(fd);
    }
}

/*---------------------------------------------------------------------------*/
/* THE ARENA AND THE ENVIRONMENT                                             */
/*---------------------------------------------------------------------------*/

static unsigned char *heap_base, *heap_top, *heap_limit;

/** @brief Hand newlib's heap the desktop's arena; called before the thread starts. */
void surface_desktop_heap(void *base, size_t bytes)
{
    heap_base = heap_top = base;
    heap_limit = (unsigned char *)base + bytes;
}

/* Heap growth requests refused, and the largest of them. */
static uint32_t heap_refused;
static size_t heap_refused_max;

/** @brief Count of refused heap growth requests and the largest one's size. */
void surface_desktop_heap_refused(uint32_t *count, size_t *largest)
{
    if (count != NULL) *count = heap_refused;
    if (largest != NULL) *largest = heap_refused_max;
}

/** @brief Heap bytes in use and the arena's size. */
void surface_desktop_heap_use(size_t *used, size_t *total)
{
    if (used != NULL) *used = (size_t)(heap_top - heap_base);
    if (total != NULL) *total = (size_t)(heap_limit - heap_base);
}

void *_sbrk_r(struct _reent *r, ptrdiff_t increment)
{
    unsigned char *old;

    surface_os_desktop_lock();
    old = heap_top;
    if (heap_base == NULL ||
        (increment > 0 && (size_t)increment > (size_t)(heap_limit - heap_top)) ||
        (increment < 0 && (size_t)-increment > (size_t)(heap_top - heap_base))) {
        if (increment > 0) {
            heap_refused++;
            if ((size_t)increment > heap_refused_max) heap_refused_max = (size_t)increment;
        }
        surface_os_desktop_unlock();
        r->_errno = ENOMEM;
        return (void *)-1;
    }
    heap_top += increment;
    surface_os_desktop_unlock();
    return old;
}

void __malloc_lock(struct _reent *r) { (void)r; surface_os_desktop_lock(); }
void __malloc_unlock(struct _reent *r) { (void)r; surface_os_desktop_unlock(); }

extern char **environ;
static char *desktop_environment[] = { "HOME=/home", "USER=tiku", "LANG=en_US.UTF-8", NULL };

/** @brief Build the file tree and the environment the desktop starts in. */
int surface_desktop_files(void)
{
    environ = desktop_environment;
    root = node_new(NULL, "", S_IFDIR | 0755);
    if (root == NULL) return -1;
    (void)make_dirs("/home/Desktop");
    (void)make_dirs("/home/Documents");
    (void)make_dirs("/home/.config/tracker");
    (void)make_dirs("/system/deskbar");
    (void)make_dirs("/system/apps");
    (void)make_dirs("/tmp");
    if (surface_desktop_bridge.card_mounted) {
        rnode_t *card = node_new(root, "Card", S_IFDIR | 0555);

        if (card == NULL || card_mark(card, "", 0) != 0) return -1;
        seed("/proc/self/mounts", "tikufs / ramfs rw 0 0\ncard /Card mfs ro 0 0\n");
    } else {
        seed("/proc/self/mounts", "tikufs / ramfs rw 0 0\n");
    }
    seed("/proc/partitions", "major minor  #blocks  name\n");
    /* Thumbnails are deferred on the panel: no image codecs measured yet. */
    seed("/home/.config/TrackerSettings", "GenerateImageThumbnails off\n");
    seed("/home/Documents/Welcome.txt",
         "TikuSurface runs TikuDesktop's shell on TikuOS.\n");
    return 0;
}

/*---------------------------------------------------------------------------*/
/* SYSTEM CALLS                                                              */
/*---------------------------------------------------------------------------*/

static fd_t *fd_of(int fd)
{
    if (fd < FIRST_FD || fd >= FDS || fds[fd].node == NULL) {
        errno = EBADF;
        return NULL;
    }
    return &fds[fd];
}

int _open(const char *path, int flags, int mode)
{
    rnode_t *parent = NULL, *n;
    const char *leaf;
    size_t len;
    int fd;

    if (path != NULL && (strcmp(path, "/dev/urandom") == 0 || strcmp(path, "/dev/random") == 0)) {
        if ((flags & O_ACCMODE) != O_RDONLY) {
            errno = EACCES;
            return -1;
        }
        n = &random_device;
        goto descriptor;
    }
    if (((flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC))) && card_path(path))
        return -1;
    n = resolve(path, &parent, &leaf, &len);
    if (n == NULL) {
        if (errno != ENOENT || parent == NULL || !(flags & O_CREAT)) return -1;
        if (len == 0 || len >= NAME_MAX_BYTES) {
            errno = ENAMETOOLONG;
            return -1;
        }
        {
            char name[NAME_MAX_BYTES];

            memcpy(name, leaf, len);
            name[len] = '\0';
            n = node_new(parent, name, S_IFREG | (mode_t)(mode & 0777));
        }
        if (n == NULL) {
            errno = ENOSPC;
            return -1;
        }
    } else if ((flags & O_CREAT) && (flags & O_EXCL)) {
        errno = EEXIST;
        return -1;
    }
    if (S_ISDIR(n->mode) && (flags & O_ACCMODE) != O_RDONLY) {
        errno = EISDIR;
        return -1;
    }
descriptor:
    for (fd = FIRST_FD; fd < FDS && fds[fd].node != NULL; fd++) {
    }
    if (fd == FDS) {
        errno = EMFILE;
        return -1;
    }
    if ((flags & O_TRUNC) && S_ISREG(n->mode) && (flags & O_ACCMODE) != O_RDONLY) {
        if (n->size != 0) changed(n);
        n->size = 0;
        n->mtime = wall();
    }
    /* A card file's reader is opened by its first read past the bytes the
     * node keeps, so an open that only checks the file costs no card
     * request. */
    fds[fd] = (fd_t){ n, 0, flags, 0 };
    n->opened++;
    return fd;
}

int _close(int fd)
{
    fd_t *f = fd_of(fd);

    if (fd >= 0 && fd < FIRST_FD) return 0;
    if (f == NULL) return -1;
    if (f->reader != 0) {
        card_close(f->reader);
        f->reader = 0;
    }
    if (--f->node->opened == 0 && f->node->unlinked && f->node != &random_device) node_free(f->node);
    f->node = NULL;
    return 0;
}

_ssize_t _read(int fd, void *buf, size_t n)
{
    fd_t *f;
    size_t left;

    if (fd >= 0 && fd < FIRST_FD) return 0;
    f = fd_of(fd);
    if (f == NULL) return -1;
    if ((f->flags & O_ACCMODE) == O_WRONLY) {
        errno = EBADF;
        return -1;
    }
    if (f->node == &random_device) {
        if (!random_seeded) {
            errno = EIO;
            return -1;
        }
        random_fill(buf, n);
        return (_ssize_t)n;
    }
    if (S_ISDIR(f->node->mode)) {
        errno = EISDIR;
        return -1;
    }
    if (f->node->card != NULL && S_ISREG(f->node->mode)) {
        card_node_t *c = f->node->card;
        _ssize_t got;

        if ((size_t)f->pos >= f->node->size) return 0;
        left = f->node->size - (size_t)f->pos;
        if (n > left) n = left;
        if (c->head == NULL && f->pos == 0) {
            /* The first bytes are kept: type checks and value columns read
             * them again and again, and each card request is slow. */
            size_t want = f->node->size < CARD_READ ? f->node->size : CARD_READ;

            if (f->reader == 0 && (f->reader = card_open(f->node)) == 0) {
                errno = EIO;
                return -1;
            }
            c->head = malloc(want ? want : 1u);
            if (c->head != NULL) {
                got = card_read(f->reader, c->head, want, 0);
                if (got < 0) {
                    free(c->head);
                    c->head = NULL;
                    return -1;
                }
                c->head_len = (size_t)got;
            }
        }
        if (c->head != NULL && (size_t)f->pos < c->head_len) {
            size_t k = c->head_len - (size_t)f->pos;

            if (k > n) k = n;
            memcpy(buf, c->head + f->pos, k);
            f->pos += (off_t)k;
            return (_ssize_t)k;
        }
        if (f->reader == 0 && (f->reader = card_open(f->node)) == 0) {
            errno = EIO;
            return -1;
        }
        got = card_read(f->reader, buf, n, f->pos);
        if (got > 0) f->pos += got;
        return got;
    }
    if ((size_t)f->pos >= f->node->size) return 0;
    left = f->node->size - (size_t)f->pos;
    if (n > left) n = left;
    memcpy(buf, f->node->data + f->pos, n);
    f->pos += (off_t)n;
    return (_ssize_t)n;
}

/* Bytes written to the console and to files, and the file written last. */
static uint32_t console_bytes, file_bytes, file_writes;
static const rnode_t *last_written;

/** @brief Console and file bytes written, file write calls, and the name of
 *  the file written last (empty before any). */
void surface_desktop_write_stats(uint32_t *console, uint32_t *files, uint32_t *calls,
                                 const char **last)
{
    *console = console_bytes;
    *files = file_bytes;
    *calls = file_writes;
    *last = last_written != NULL ? last_written->name : "";
}

_ssize_t _write(int fd, const void *buf, size_t n)
{
    fd_t *f;
    size_t at;

    if (fd == 1 || fd == 2) {
        console_bytes += (uint32_t)n;
        surface_desktop_log(buf, n);
        return (_ssize_t)n;
    }
    f = fd_of(fd);
    if (f == NULL) return -1;
    if ((f->flags & O_ACCMODE) == O_RDONLY) {
        errno = EBADF;
        return -1;
    }
    at = (f->flags & O_APPEND) ? f->node->size : (size_t)f->pos;
    if (at > SIZE_MAX - n || reserve(f->node, at + n) != 0) return -1;
    memcpy(f->node->data + at, buf, n);
    changed(f->node);
    file_bytes += (uint32_t)n;
    file_writes++;
    last_written = f->node;
    if (at + n > f->node->size) f->node->size = at + n;
    f->pos = (off_t)(at + n);
    f->node->mtime = wall();
    return (_ssize_t)n;
}

off_t _lseek(int fd, off_t offset, int whence)
{
    fd_t *f = fd_of(fd);
    off_t base;

    if (f == NULL) return -1;
    base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? f->pos :
           whence == SEEK_END ? (off_t)f->node->size : -1;
    if (base < 0 || base + offset < 0) {
        errno = EINVAL;
        return -1;
    }
    f->pos = base + offset;
    return f->pos;
}

int _fstat(int fd, struct stat *st)
{
    fd_t *f;

    if (fd >= 0 && fd < FIRST_FD) {
        memset(st, 0, sizeof *st);
        st->st_mode = S_IFCHR | 0620;
        return 0;
    }
    f = fd_of(fd);
    if (f == NULL) return -1;
    fill_stat(f->node, st);
    return 0;
}

int _stat(const char *path, struct stat *st)
{
    rnode_t *n = lookup(path);

    if (n == NULL) return -1;
    fill_stat(n, st);
    return 0;
}

int lstat(const char *path, struct stat *st) { return _stat(path, st); }
int _isatty(int fd) { return fd >= 0 && fd < FIRST_FD; }

/**
 * @brief The descriptor's flags, for newlib's fdopen(), which refuses a
 *        descriptor whose access mode it cannot read, and for callers that
 *        set close-on-exec.
 * @return F_GETFL: the access mode the descriptor was opened with, with its
 *         O_APPEND and O_NONBLOCK; read and write for the three standard
 *         streams.  F_SETFL: 0, with O_APPEND and O_NONBLOCK taken from the
 *         argument.  F_GETFD and F_SETFD: 0; there is no exec to close a
 *         descriptor on.  Any other command: -1 with EINVAL.
 */
int _fcntl(int fd, int cmd, ...)
{
    fd_t *f = fd_of(fd);
    va_list ap;
    int arg;

    if (f == NULL && !(fd >= 0 && fd < FIRST_FD)) {
        errno = EBADF;
        return -1;
    }
    va_start(ap, cmd);
    arg = va_arg(ap, int);
    va_end(ap);
    switch (cmd) {
    case F_GETFL:
        return (f != NULL) ? f->flags & (O_ACCMODE | O_APPEND | O_NONBLOCK) : O_RDWR;
    case F_SETFL:
        if (f != NULL) {
            f->flags = (f->flags & ~(O_APPEND | O_NONBLOCK)) | (arg & (O_APPEND | O_NONBLOCK));
        }
        return 0;
    case F_GETFD:
    case F_SETFD:
        return 0;
    default:
        errno = EINVAL;
        return -1;
    }
}

/** @brief The one process there is; newlib's tmpnam() puts it in a name. */
int _getpid(void) { return 1; }

ssize_t pread(int fd, void *buf, size_t n, off_t offset)
{
    fd_t *f = fd_of(fd);
    off_t saved;
    ssize_t got;

    if (f == NULL) return -1;
    saved = f->pos;
    f->pos = offset;
    got = _read(fd, buf, n);
    f->pos = saved;
    return got;
}

ssize_t pwrite(int fd, const void *buf, size_t n, off_t offset)
{
    fd_t *f = fd_of(fd);
    off_t saved;
    ssize_t put;

    if (f == NULL) return -1;
    if (f->flags & O_APPEND) return _write(fd, buf, n);
    saved = f->pos;
    f->pos = offset;
    put = _write(fd, buf, n);
    f->pos = saved;
    return put;
}

int _unlink(const char *path)
{
    if (card_path(path)) return -1;
    rnode_t *n = lookup(path);

    if (n == NULL) return -1;
    if (S_ISDIR(n->mode)) {
        errno = EISDIR;
        return -1;
    }
    removed(n);
    node_detach(n);
    if (n->opened) n->unlinked = 1;
    else node_free(n);
    return 0;
}

int _link(const char *old, const char *new)
{
    if (card_path(old) || card_path(new)) return -1;
    (void)old; (void)new;
    errno = EPERM;
    return -1;
}

static int is_ancestor(const rnode_t *a, const rnode_t *n)
{
    for (; n != NULL; n = n->parent) {
        if (n == a) return 1;
    }
    return 0;
}

int _rename(const char *old, const char *new)
{
    if (card_path(old) || card_path(new)) return -1;
    rnode_t *from = lookup(old), *parent = NULL, *to;
    const char *leaf;
    size_t len;

    if (from == NULL) return -1;
    if (from == root) {
        errno = EBUSY;
        return -1;
    }
    to = resolve(new, &parent, &leaf, &len);
    if (to == NULL && errno != ENOENT) return -1;
    if (parent == NULL || len == 0 || len >= NAME_MAX_BYTES) {
        errno = ENOENT;
        return -1;
    }
    if (S_ISDIR(from->mode) && is_ancestor(from, parent)) {
        errno = EINVAL;
        return -1;
    }
    if (to == from) return 0;
    if (to != NULL) {
        if (S_ISDIR(to->mode)) {
            if (!S_ISDIR(from->mode)) {
                errno = EISDIR;
                return -1;
            }
            if (to->child != NULL) {
                errno = ENOTEMPTY;
                return -1;
            }
        } else if (S_ISDIR(from->mode)) {
            errno = ENOTDIR;
            return -1;
        }
        node_detach(to);
        if (to->opened) to->unlinked = 1;
        else node_free(to);
    }
    node_detach(from);
    memcpy(from->name, leaf, len);
    from->name[len] = '\0';
    from->parent = parent;
    from->next = parent->child;
    parent->child = from;
    parent->mtime = wall();
    mutations++;
    return 0;
}

int mkdir(const char *path, mode_t mode)
{
    if (card_path(path)) return -1;
    rnode_t *parent = NULL, *n;
    const char *leaf;
    size_t len;

    n = resolve(path, &parent, &leaf, &len);
    if (n != NULL) {
        errno = EEXIST;
        return -1;
    }
    if (errno != ENOENT || parent == NULL) return -1;
    if (len == 0 || len >= NAME_MAX_BYTES) {
        errno = ENAMETOOLONG;
        return -1;
    }
    {
        char name[NAME_MAX_BYTES];

        memcpy(name, leaf, len);
        name[len] = '\0';
        if (node_new(parent, name, S_IFDIR | (mode & 0777)) == NULL) {
            errno = ENOSPC;
            return -1;
        }
    }
    return 0;
}

int _mkdir(const char *path, mode_t mode) { return mkdir(path, mode); }

int rmdir(const char *path)
{
    if (card_path(path)) return -1;
    rnode_t *n = lookup(path);

    if (n == NULL) return -1;
    if (!S_ISDIR(n->mode)) {
        errno = ENOTDIR;
        return -1;
    }
    if (n == root) {
        errno = EBUSY;
        return -1;
    }
    if (n->child != NULL) {
        errno = ENOTEMPTY;
        return -1;
    }
    removed(n);
    node_detach(n);
    /* A stream still open on it keeps it until the last closedir(). */
    if (n->opened) n->unlinked = 1;
    else node_free(n);
    return 0;
}

/* The desktop owns every node, so the owner's bits answer. */
int access(const char *path, int mode)
{
    rnode_t *n = lookup(path);

    if (n == NULL) return -1;
    if (((mode & R_OK) && !(n->mode & S_IRUSR)) || ((mode & W_OK) && !(n->mode & S_IWUSR)) ||
        ((mode & X_OK) && !(n->mode & S_IXUSR))) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

int chdir(const char *path)
{
    rnode_t *n = lookup(path);
    char resolved[PATH_MAX];

    if (n == NULL) return -1;
    if (!S_ISDIR(n->mode)) {
        errno = ENOTDIR;
        return -1;
    }
    if (realpath(path, resolved) == NULL) return -1;
    snprintf(cwd, sizeof cwd, "%s", resolved);
    return 0;
}

char *getcwd(char *buf, size_t size)
{
    if (buf == NULL || size <= strlen(cwd)) {
        errno = ERANGE;
        return NULL;
    }
    strcpy(buf, cwd);
    return buf;
}

char *realpath(const char *path, char *resolved)
{
    rnode_t *n = lookup(path), *chain[64];
    int depth = 0;
    char *out = resolved;
    size_t at = 0;

    if (n == NULL) return NULL;
    if (out == NULL && (out = malloc(PATH_MAX)) == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    for (; n != NULL && n != root && depth < 64; n = n->parent) chain[depth++] = n;
    out[0] = '/';
    out[1] = '\0';
    at = 0;
    while (depth-- > 0) {
        int w = snprintf(out + at, PATH_MAX - at, "/%s", chain[depth]->name);

        if (w < 0 || (size_t)w >= PATH_MAX - at) {
            errno = ENAMETOOLONG;
            if (resolved == NULL) free(out);
            return NULL;
        }
        at += (size_t)w;
    }
    return out;
}

ssize_t readlink(const char *path, char *buf, size_t size)
{
    (void)buf; (void)size;
    if (lookup(path) != NULL) errno = EINVAL;
    return -1;
}

int symlink(const char *target, const char *path)
{
    if (card_path(path)) return -1;
    (void)target; (void)path;
    errno = EPERM;
    return -1;
}

int chmod(const char *path, mode_t mode)
{
    if (card_path(path)) return -1;
    rnode_t *n = lookup(path);

    if (n == NULL) return -1;
    if ((n->mode & 07777) != (mode & 07777)) changed(n);
    n->mode = (n->mode & ~(mode_t)07777) | (mode & 07777);
    return 0;
}

int fchmod(int fd, mode_t mode)
{
    fd_t *f = fd_of(fd);

    if (f != NULL && f->node->card != NULL) {
        errno = EROFS;
        return -1;
    }

    if (f == NULL) return -1;
    if ((f->node->mode & 07777) != (mode & 07777)) changed(f->node);
    f->node->mode = (f->node->mode & ~(mode_t)07777) | (mode & 07777);
    return 0;
}

int chown(const char *path, uid_t uid, gid_t gid) { (void)uid; (void)gid; return lookup(path) ? 0 : -1; }
int lchown(const char *path, uid_t uid, gid_t gid) { return chown(path, uid, gid); }
mode_t umask(mode_t mask) { (void)mask; return 022; }
int fsync(int fd) { return (fd >= 0 && fd < FIRST_FD) || fd_of(fd) ? 0 : -1; }

int ftruncate(int fd, off_t length)
{
    fd_t *f = fd_of(fd);

    if (f != NULL && f->node->card != NULL) {
        errno = EROFS;
        return -1;
    }

    if (f == NULL) return -1;
    if (length < 0) {
        errno = EINVAL;
        return -1;
    }
    if (reserve(f->node, (size_t)length) != 0) return -1;
    if ((size_t)length > f->node->size) memset(f->node->data + f->node->size, 0, (size_t)length - f->node->size);
    if (f->node->size != (size_t)length) changed(f->node);
    f->node->size = (size_t)length;
    return 0;
}

int utimensat(int dirfd, const char *path, const struct timespec times[2], int flags)
{
    if (card_path(path)) return -1;
    rnode_t *n = lookup(path);

    (void)dirfd; (void)flags;
    if (n == NULL) return -1;
    {
        time_t t = times != NULL ? times[1].tv_sec : wall();

        if (n->mtime != t) changed(n);
        n->mtime = t;
    }
    return 0;
}

int statvfs(const char *path, struct statvfs *sv)
{
    size_t used, total;

    if (lookup(path) == NULL) return -1;
    surface_desktop_heap_use(&used, &total);
    memset(sv, 0, sizeof *sv);
    sv->f_bsize = sv->f_frsize = 4096;
    sv->f_blocks = total / 4096u;
    sv->f_bfree = sv->f_bavail = (total - used) / 4096u;
    sv->f_namemax = NAME_MAX_BYTES - 1;
    return 0;
}

int fstatvfs(int fd, struct statvfs *sv) { (void)fd; return statvfs("/", sv); }

int mount(const char *source, const char *target, const char *type,
          unsigned long flags, const void *data)
{
    (void)source; (void)target; (void)type; (void)flags; (void)data;
    errno = EPERM;
    return -1;
}

int umount(const char *target) { (void)target; errno = EPERM; return -1; }

/** @brief Fill the trailing X characters of @p tmpl with a fresh name. */
static int fill_template(char *tmpl, size_t suffix)
{
    static uint32_t counter;
    size_t len = tmpl != NULL ? strlen(tmpl) : 0u, xs = 0;
    uint32_t v;
    static const char digits[] = "abcdefghijklmnopqrstuvwxyz0123456789";

    while (xs < len - suffix && tmpl[len - suffix - 1u - xs] == 'X') xs++;
    if (tmpl == NULL || xs < 6u) {
        errno = EINVAL;
        return -1;
    }
    v = (uint32_t)surface_desktop_now_us() ^ (++counter * 2654435761u);
    for (size_t i = 0; i < xs; i++) {
        tmpl[len - suffix - 1u - i] = digits[v % 36u];
        v = v / 36u + counter * 7919u;
    }
    return 0;
}

char *mkdtemp(char *tmpl)
{
    char saved[PATH_MAX];
    int tries;

    if (tmpl == NULL || strlen(tmpl) >= sizeof saved) {
        errno = EINVAL;
        return NULL;
    }
    strcpy(saved, tmpl);
    for (tries = 0; tries < 100; tries++) {
        strcpy(tmpl, saved);
        if (fill_template(tmpl, 0) != 0) return NULL;
        if (mkdir(tmpl, 0700) == 0) return tmpl;
        if (errno != EEXIST) return NULL;
    }
    errno = EEXIST;
    return NULL;
}

int mkstemps(char *tmpl, int suffix)
{
    char saved[PATH_MAX];
    int tries, fd;

    if (tmpl == NULL || suffix < 0 || strlen(tmpl) >= sizeof saved) {
        errno = EINVAL;
        return -1;
    }
    strcpy(saved, tmpl);
    for (tries = 0; tries < 100; tries++) {
        strcpy(tmpl, saved);
        if (fill_template(tmpl, (size_t)suffix) != 0) return -1;
        fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0 || errno != EEXIST) return fd;
    }
    errno = EEXIST;
    return -1;
}

int mkstemp(char *tmpl) { return mkstemps(tmpl, 0); }

/*---------------------------------------------------------------------------*/
/* DIRECTORIES                                                               */
/*---------------------------------------------------------------------------*/

typedef struct rdir {
    DIR base;
    rnode_t *dir, *at;
    int dots;
    struct dirent entry;
    struct rdir *next;          /* the next open stream */
} rdir_t;

/* Every open directory stream, so that removing an entry can move a stream
 * that is about to return it. */
static rdir_t *streams;

static void streams_pass(const rnode_t *n)
{
    rdir_t *d;

    for (d = streams; d != NULL; d = d->next) {
        if (d->at == n) d->at = n->next;
    }
}

DIR *opendir(const char *path)
{
    rnode_t *n = lookup(path);
    rdir_t *d;

    if (n == NULL) return NULL;
    if (!S_ISDIR(n->mode)) {
        errno = ENOTDIR;
        return NULL;
    }
    if (n->card != NULL && !n->card->loaded) card_load(n);
    d = calloc(1, sizeof *d);
    if (d == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    d->dir = n;
    d->at = n->child;
    d->next = streams;
    streams = d;
    n->opened++;
    return &d->base;
}

struct dirent *readdir(DIR *dp)
{
    rdir_t *d = (rdir_t *)dp;

    if (d == NULL) {
        errno = EBADF;
        return NULL;
    }
    if (d->dots < 2) {
        d->entry.d_ino = d->dir->ino;
        d->entry.d_type = DT_DIR;
        snprintf(d->entry.d_name, sizeof d->entry.d_name, "%s", d->dots ? ".." : ".");
        d->dots++;
        return &d->entry;
    }
    if (d->at == NULL) return NULL;
    d->entry.d_ino = d->at->ino;
    d->entry.d_type = S_ISDIR(d->at->mode) ? DT_DIR : DT_REG;
    snprintf(d->entry.d_name, sizeof d->entry.d_name, "%s", d->at->name);
    d->at = d->at->next;
    return &d->entry;
}

void rewinddir(DIR *dp)
{
    rdir_t *d = (rdir_t *)dp;

    if (d == NULL) return;
    d->at = d->dir->child;
    d->dots = 0;
}

int closedir(DIR *dp)
{
    rdir_t *d = (rdir_t *)dp, **p;

    if (d == NULL) {
        errno = EBADF;
        return -1;
    }
    for (p = &streams; *p != NULL; p = &(*p)->next) {
        if (*p == d) {
            *p = d->next;
            break;
        }
    }
    /* A directory removed while this stream held it is freed with the last
     * stream. */
    if (--d->dir->opened == 0 && d->dir->unlinked) node_free(d->dir);
    free(d);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* TIME, PROCESSES AND THREADS                                               */
/*---------------------------------------------------------------------------*/

int clock_gettime(clockid_t clock, struct timespec *now)
{
    uint64_t us = surface_desktop_now_us();

    if (now == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (clock == CLOCK_REALTIME) {
        now->tv_sec = (time_t)(SURFACE_EPOCH + (long long)(us / 1000000u));
    } else {
        now->tv_sec = (time_t)(us / 1000000u);
    }
    now->tv_nsec = (long)(us % 1000000u) * 1000L;
    return 0;
}

int _gettimeofday(struct timeval *tv, void *tz)
{
    struct timespec ts;

    (void)tz;
    if (tv == NULL) return 0;
    (void)clock_gettime(CLOCK_REALTIME, &ts);
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / 1000L;
    return 0;
}

static int never(void *context) { (void)context; return 0; }

int nanosleep(const struct timespec *request, struct timespec *remain)
{
    uint64_t us;

    if (request == NULL || request->tv_sec < 0 || request->tv_nsec < 0) {
        errno = EINVAL;
        return -1;
    }
    us = (uint64_t)request->tv_sec * 1000000u + (uint64_t)request->tv_nsec / 1000u;
    if (us == 0) us = 1;
    surface_os_desktop_wait(never, NULL, us);
    if (remain != NULL) remain->tv_sec = remain->tv_nsec = 0;
    return 0;
}

long sysconf(int name)
{
    switch (name) {
    case _SC_PAGESIZE: return 4096;
    case _SC_NPROCESSORS_ONLN: return 1;
    case _SC_NPROCESSORS_CONF: return 1;
    case _SC_CLK_TCK: return 128;
    default: errno = EINVAL; return -1;
    }
}

uid_t geteuid(void) { return 0; }
int getloadavg(double loadavg[], int count) { (void)loadavg; (void)count; return -1; }

int execlp(const char *file, const char *arg, ...) { (void)file; (void)arg; errno = ENOSYS; return -1; }
int execvp(const char *file, char *const argv[]) { (void)file; (void)argv; errno = ENOSYS; return -1; }
pid_t waitpid(pid_t pid, int *status, int options) { (void)pid; (void)status; (void)options; errno = ECHILD; return -1; }

void _exit(int code)
{
    surface_desktop_bridge.exit_code = code;
    surface_desktop_bridge.exited = 1;
    surface_os_desktop_kick();
    surface_os_desktop_exit();
    for (;;) {
    }
}

/* One thread runs the desktop, so a mutex is never contended. */
int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) { (void)m; (void)a; return 0; }
int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutex_trylock(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }
pthread_t pthread_self(void) { return (pthread_t)1; }
int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

/*---------------------------------------------------------------------------*/
/* SELF-TEST                                                                 */
/*---------------------------------------------------------------------------*/

/** @brief Exercise newlib's file, directory and environment calls through
 *         this layer and report each result on standard error. */
void surface_desktop_selftest(void)
{
    char dir[] = "/tmp/selftest-XXXXXX";
    char path[96], back[32];
    const char *home = getenv("HOME");
    FILE *f;
    DIR *d;
    struct dirent *e;
    struct stat st;
    int seen = 0, ok;
    char *made = mkdtemp(dir);

    fprintf(stderr, "posix: home=%s mkdtemp=%s errno=%d\n", home ? home : "(none)",
            made ? made : "(null)", made ? 0 : errno);
    ok = setenv("SURFACE_SELFTEST", "1", 1) == 0 && getenv("SURFACE_SELFTEST") != NULL;
    fprintf(stderr, "posix: setenv=%d errno=%d\n", ok, ok ? 0 : errno);
    snprintf(path, sizeof path, "%s/file.txt", made ? made : "/tmp");
    f = fopen(path, "w");
    ok = f != NULL && fprintf(f, "hello %d", 42) > 0 && fclose(f) == 0;
    fprintf(stderr, "posix: fopen-write=%d errno=%d\n", ok, ok ? 0 : errno);
    f = fopen(path, "r");
    memset(back, 0, sizeof back);
    ok = f != NULL && fread(back, 1, sizeof back - 1u, f) == 8u && strcmp(back, "hello 42") == 0;
    if (f != NULL) fclose(f);
    fprintf(stderr, "posix: fopen-read=%d text=%s\n", ok, back);
    d = opendir(made ? made : "/tmp");
    while (d != NULL && (e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, "file.txt") == 0) seen = 1;
    }
    if (d != NULL) closedir(d);
    fprintf(stderr, "posix: readdir=%d\n", seen);
    snprintf(back, sizeof back, "%s", "/tmp/renamed.txt");
    ok = rename(path, back) == 0 && stat(back, &st) == 0 && st.st_size == 8 && unlink(back) == 0;
    fprintf(stderr, "posix: rename-stat-unlink=%d errno=%d\n", ok, ok ? 0 : errno);
    {
        char tmpl[] = "/tmp/atomic-XXXXXX";
        int fd = mkstemp(tmpl);

        ok = fd >= 0 && write(fd, "x", 1) == 1 && close(fd) == 0 && unlink(tmpl) == 0;
        fprintf(stderr, "posix: mkstemp=%d errno=%d\n", ok, ok ? 0 : errno);
    }
    {
        /* A stream on a descriptor, as an atomic file replacement writes. */
        char tmpl[] = "/tmp/stream-XXXXXX";
        int fd = mkstemp(tmpl), failed;
        FILE *s = (fd >= 0) ? fdopen(fd, "wb") : NULL;

        ok = s != NULL && fputs("y", s) >= 0 && fclose(s) == 0 && stat(tmpl, &st) == 0 &&
             st.st_size == 1;
        failed = errno;
        if (s == NULL && fd >= 0) close(fd);
        unlink(tmpl);
        fprintf(stderr, "posix: fdopen=%d errno=%d\n", ok, ok ? 0 : failed);
    }
    {
        /* A nameless file, as the settings reader and writer use. */
        FILE *t = tmpfile();
        char got[4] = "";
        int failed;

        ok = t != NULL && fputs("tmp", t) >= 0 && fseek(t, 0, SEEK_SET) == 0 &&
             fread(got, 1, 3u, t) == 3u && memcmp(got, "tmp", 3u) == 0;
        failed = errno;
        if (t != NULL) fclose(t);
        fprintf(stderr, "posix: tmpfile=%d errno=%d\n", ok, ok ? 0 : failed);
    }
    ok = access("/home/Documents/Welcome.txt", R_OK | W_OK) == 0 &&
         access("/home/Documents/Welcome.txt", X_OK) != 0 && errno == EACCES &&
         access("/home/Documents", X_OK) == 0;
    fprintf(stderr, "posix: access=%d\n", ok);
    {
        unsigned char a[16], b[16];
        int fd = open("/dev/urandom", O_RDONLY);

        ok = fd >= 0 && read(fd, a, sizeof a) == (ssize_t)sizeof a &&
             read(fd, b, sizeof b) == (ssize_t)sizeof b && memcmp(a, b, sizeof a) != 0 &&
             close(fd) == 0;
        fprintf(stderr, "posix: urandom=%d errno=%d\n", ok, ok ? 0 : errno);
    }
}

void surface_desktop_hash_poll(void)
{
    surface_desktop_bridge_t *b = &surface_desktop_bridge;
    unsigned token = b->hash_token;
    unsigned char digest[32], *bytes = NULL;
    char path[sizeof b->hash_path];
    size_t total = 0, cap = 0, n;
    FILE *f;

    if (token == b->hash_done) return;
    memcpy(path, b->hash_path, sizeof path);
    path[sizeof path - 1u] = '\0';
    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "hash %s: errno=%d\n", path, errno);
        b->hash_done = token;
        return;
    }
    for (;;) {
        if (total == cap) {
            unsigned char *grown = realloc(bytes, cap ? cap * 2u : 4096u);

            if (grown == NULL) break;
            bytes = grown;
            cap = cap ? cap * 2u : 4096u;
        }
        n = fread(bytes + total, 1, cap - total, f);
        if (n == 0) break;
        total += n;
    }
    fclose(f);
    tiku_sha256(bytes, total, digest);
    free(bytes);
    fprintf(stderr, "hash %s: bytes=%lu sha256=", path, (unsigned long)total);
    for (int k = 0; k < 32; k++) fprintf(stderr, "%02x", digest[k]);
    fprintf(stderr, "\n");
    b->hash_done = token;
}
