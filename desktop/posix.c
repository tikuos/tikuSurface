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
#include "tiku_posix.h"
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
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>

#define NAME_MAX_BYTES 128
/* A read-only file in Desktop and Documents that says the home is not kept. */
#define SURFACE_HOME_NOTE "Files here are lost at restart.txt"
#define SURFACE_HOME_NOTE_TEXT "Desktop and Documents are held in memory on this board.\n" \
    "Files saved here are lost when the board restarts.\n"
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
    struct xattr *attrs;       /* its extended attributes */
    uint32_t born, counted;    /* observation window it was created in, and */
    uint32_t changes;          /* its changes counted in window `counted` */
} rnode_t;

static rnode_t *root;
/* Every change made to the tree counts here: creation, removal, renaming,
 * writing, truncation, and modes and times that change.  A call that
 * changes nothing does not count, and a descriptor's writes count at its
 * first write and at its close, so a file being copied in does not make
 * every view re-list at each write.  tiku_posix_mutations() hands the count
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
    unsigned gen;       /* opens of this number so far */
    int busy;           /* a card request on it is outstanding */
    int wrote;          /* 0 unwritten, 1 its first write counted, 2 written since */
    int stored;         /* a settings file's index + 1 when on the store, else 0 */
    int stored_main;    /* the settings file itself, opened to write */
} fd_t;
static fd_t fds[FDS];

/*---------------------------------------------------------------------------*/
/* THE LAYER'S LOCK                                                          */
/*---------------------------------------------------------------------------*/
/* One lock over the tree, the descriptors, the directory streams and the
 * card's listing state.  Each system call below holds it from entry to
 * return, and a call made inside another holds it again.  A card request
 * gives back every hold while the owner runs it and takes them again after,
 * so other threads use the layer meanwhile; what a call keeps across that
 * wait is a /Card node, which is never freed, or a descriptor it marked
 * busy.  The kernel thread enters the layer only before the desktop thread
 * starts, when no other thread holds it. */
static pthread_mutex_t layer = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t layer_moved = PTHREAD_COND_INITIALIZER;
static pthread_t layer_owner;   /* the holder, 0 when free; read by any thread */
static unsigned layer_depth;    /* the holder's holds */

static void enter(void)
{
    pthread_t self = pthread_self();

    if (__atomic_load_n(&layer_owner, __ATOMIC_RELAXED) == self) {
        layer_depth++;
        return;
    }
    (void)pthread_mutex_lock(&layer);
    __atomic_store_n(&layer_owner, self, __ATOMIC_RELAXED);
    layer_depth = 1u;
}

static void leave(void)
{
    if (--layer_depth == 0u) {
        __atomic_store_n(&layer_owner, (pthread_t)0, __ATOMIC_RELAXED);
        (void)pthread_mutex_unlock(&layer);
    }
}

/** @brief Give back every hold; the number of them, for enter_again(). */
static unsigned leave_all(void)
{
    unsigned held = layer_depth;

    if (held != 0u) {
        layer_depth = 1u;
        leave();
    }
    return held;
}

static void enter_again(unsigned held)
{
    if (held != 0u) {
        enter();
        layer_depth = held;
    }
}

/** @brief Wait, without the lock, until another thread announces a change;
 *  the caller holds the lock as before on return. */
static void await_change(void)
{
    unsigned held = layer_depth;

    __atomic_store_n(&layer_owner, (pthread_t)0, __ATOMIC_RELAXED);
    layer_depth = 0u;
    (void)pthread_cond_wait(&layer_moved, &layer);
    __atomic_store_n(&layer_owner, pthread_self(), __ATOMIC_RELAXED);
    layer_depth = held;
}

static void announce_change(void) { (void)pthread_cond_broadcast(&layer_moved); }

/** @brief Seconds of wall time for a file's times, from the kernel's clock
 *  through compat/posix; 0, which Tracker shows as no date, while the clock
 *  has never been set. */
static time_t wall(void)
{
    struct timespec now;

    if (surface_os_clock_state() == SURFACE_CLOCK_UNSET ||
        clock_gettime(CLOCK_REALTIME, &now) != 0) return 0;
    return now.tv_sec;
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

static void attrs_free(rnode_t *n);

static void node_free(rnode_t *n)
{
    attrs_free(n);
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
#define CARD_UNLISTED 0               /* a directory's entries are not read */
#define CARD_LISTING 1                /* a thread is reading them */
#define CARD_LISTED 2                 /* they are in the tree */

typedef struct card_node {
    char name[CARD_NAME];   /* a file's full name; a directory's prefix */
    uint64_t epoch, rank;   /* where the enumeration found a file */
    int loaded;             /* a directory's CARD_* listing state */
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

/** @brief Run one card request, with the layer's holds given back while the
 *  owner runs it. */
static void card_request(surface_media_io_t *io)
{
    unsigned held = leave_all();

    surface_desktop_card(io);
    enter_again(held);
}

/** @brief List the card objects under @p dir's prefix into the tree: the
 *  first name segment past the prefix becomes a directory, a name without
 *  one a file.  A changed names epoch ends the listing where it stands. */
static void card_load(rnode_t *dir)
{
    surface_media_io_t io;
    char prefix[CARD_NAME];
    size_t n = strlen(dir->card->name);

    dir->card->loaded = CARD_LISTING;
    if (n + 2u > CARD_NAME) {
        dir->card->loaded = CARD_LISTED;
        announce_change();
        return;
    }
    memcpy(prefix, dir->card->name, n);
    if (n) prefix[n++] = '/';
    prefix[n] = '\0';
    memset(&io, 0, sizeof io);
    memcpy(io.name, prefix, n + 1u);
    for (unsigned step = 0; step < CARD_ENTRIES; step++) {
        const char *rest, *slash;
        char found[CARD_NAME];

        io.operation = SURFACE_MEDIA_ENUMERATE;
        card_request(&io);
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
    dir->card->loaded = CARD_LISTED;
    announce_change();
}

/** @brief Have card directory @p dir's entries in the tree: list them, or
 *  wait while another thread lists them. */
static void card_ready(rnode_t *dir)
{
    while (dir->card->loaded == CARD_LISTING) await_change();
    if (dir->card->loaded == CARD_UNLISTED) card_load(dir);
}

/** @brief @p dir's child named by @p len bytes of @p name, listing a card
 *  directory first. */
static rnode_t *child_in(rnode_t *dir, const char *name, size_t len)
{
    if (dir->card != NULL && S_ISDIR(dir->mode)) card_ready(dir);
    return child_named(dir, name, len);
}

/** @brief Whether @p path is @p mount or under it, as an absolute path in
 *  @p full: relative to the working directory, without leading doubled
 *  slashes. */
static const char *under(const char *path, const char *mount, char full[PATH_MAX])
{
    size_t len = strlen(mount);

    if (path == NULL) return NULL;
    if (path[0] != '/') {
        snprintf(full, PATH_MAX, "%s/%s", cwd, path);
        path = full;
    }
    while (path[0] == '/' && path[1] == '/') path++;
    if (strncmp(path, mount, len) != 0 || (path[len] != '\0' && path[len] != '/')) return NULL;
    return path;
}

/** @brief Whether @p path names /Card or a path under it; such paths refuse
 *  changes with EROFS. */
static int read_only_path(const char *path)
{
    char full[PATH_MAX];

    if (under(path, "/Card", full) == NULL) return 0;
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
    card_request(&io);
    return io.result == SURFACE_MEDIA_OK ? io.reader : 0;
}

/** @brief Close a card reader. */
static void card_close(uint64_t reader)
{
    surface_media_io_t io;

    memset(&io, 0, sizeof io);
    io.operation = SURFACE_MEDIA_CLOSE;
    io.reader = reader;
    card_request(&io);
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
        card_request(&io);
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
int _open(const char *path, int flags, int mode);
_ssize_t _write(int fd, const void *buf, size_t n);
int _close(int fd);
int _stat(const char *path, struct stat *st);

/** @brief Create @p path with @p text and @p mode, making its directories. */
static void seed(const char *path, const char *text, mode_t mode)
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
    /* The layer's own calls: the C library's open() reaches them too. */
    fd = _open(path, O_WRONLY | O_CREAT | O_TRUNC, (int)mode);
    if (fd >= 0) {
        (void)_write(fd, text, strlen(text));
        (void)_close(fd);
    }
}

/*---------------------------------------------------------------------------*/
/* THE ENVIRONMENT                                                           */
/*---------------------------------------------------------------------------*/
/* The heap these files live in is TikuOS's compat/posix heap group over the
 * runtime's arena (surface_os_desktop_heap_memory()); this file defines no
 * heap hook and no allocator lock. */

extern char **environ;
static char *desktop_environment[] = { "HOME=/home", "USER=tiku", "LANG=en_US.UTF-8", NULL };

/** @brief Build the file tree and the environment the desktop starts in. */
static int surface_desktop_files_locked(void)
{
    environ = desktop_environment;
    root = node_new(NULL, "", S_IFDIR | 0755);
    if (root == NULL) return -1;
    (void)make_dirs("/home/Desktop");
    (void)make_dirs("/home/Documents");
    (void)make_dirs("/home/.config/tracker/Desktop");
    (void)make_dirs("/system/deskbar");
    (void)make_dirs("/system/apps");
    (void)make_dirs("/tmp");
    if (surface_desktop_bridge.card_mounted) {
        rnode_t *card = node_new(root, "Card", S_IFDIR | 0555);

        if (card == NULL || card_mark(card, "", 0) != 0) return -1;
        seed("/proc/self/mounts",
             "tikufs / ramfs rw 0 0\ncard /Card mfs ro 0 0\n", 0644);
    } else {
        seed("/proc/self/mounts", "tikufs / ramfs rw 0 0\n", 0644);
    }
    seed("/proc/partitions", "major minor  #blocks  name\n", 0644);
    /* /home/.config/TrackerSettings is in the store; its default, with
     * thumbnails off (no image codec is measured on the panel), is seeded
     * there at its first read (stored_copy()). */
    seed("/home/Documents/Welcome.txt",
         "TikuSurface runs TikuDesktop's shell on TikuOS.\n", 0644);
    /* The home is this tree: say so where files are saved. */
    seed("/home/Desktop/" SURFACE_HOME_NOTE, SURFACE_HOME_NOTE_TEXT, 0444);
    seed("/home/Documents/" SURFACE_HOME_NOTE, SURFACE_HOME_NOTE_TEXT, 0444);
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

/** @brief Give descriptor @p fd to @p n, its last open's number moved on. */
static void fd_fill(int fd, rnode_t *n, int flags, uint64_t reader)
{
    unsigned gen = fds[fd].gen + 1u;

    fds[fd] = (fd_t){ n, 0, flags, reader, gen, 0, 0, 0, 0 };
}

/** @brief Wait until no card request is outstanding on @p f, which @p gen
 *  opened; 0, or -1 with EBADF when it was closed meanwhile. */
static int fd_idle(fd_t *f, unsigned gen)
{
    while (f->node != NULL && f->gen == gen && f->busy) await_change();
    if (f->node == NULL || f->gen != gen) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* THE SETTINGS IN THE STORE                                                 */
/*---------------------------------------------------------------------------*/
/* The desktop's settings files are files of TikuOS's file store, through
 * compat/posix's store group: each path below, and the path with ".new" or
 * ".tmp-" and six letters after it, names the store file its key gives with
 * the same ending.  A descriptor on one holds store_device and keeps the
 * library's descriptor in its reader field; the library's calls that publish
 * wait, so the layer's lock is given back meanwhile.  Reads of a settings
 * file itself come from a copy of its published content, loaded at its first
 * read and again after the desktop publishes to it; a record the store is
 * confirmed not to have is seeded with its default first. */
typedef struct {
    const char *path, *key, *seed;
    rnode_t *copy;    /* the published content; NULL until loaded */
} stored_t;
static stored_t stored[] = {
    { "/home/.config/TrackerSettings", "/data/ts/settings", "GenerateImageThumbnails off\n", NULL },
    { "/home/.config/tracker/types", "/data/ts/types", NULL, NULL },
    { "/home/.local/share/tracker/devices/local.state", "/data/ts/session", NULL, NULL },
};
#define STORED (sizeof stored / sizeof stored[0])
static rnode_t store_device = { .name = "store", .mode = S_IFREG | 0644, .ino = 3 };

/** @brief Whether the @p n bytes at @p s are lowercase letters and digits,
 *  as fill_template() writes them. */
static int template_letters(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9'))) return 0;
    }
    return 1;
}

/** @brief For a settings file's path, or one with a store ending, its store
 *  path in @p key, and in @p main whether it is the file itself.
 *  @return the file's index, or -1 for any other path */
static int stored_path(const char *path, char *key, size_t max, int *main)
{
    char full[PATH_MAX];
    const char *p = under(path, "/home", full);

    if (p == NULL) return -1;
    for (unsigned i = 0; i < STORED; i++) {
        size_t n = strlen(stored[i].path);
        const char *end = p + n;

        if (strncmp(p, stored[i].path, n) != 0) continue;
        if (*end == '\0' || strcmp(end, ".new") == 0 ||
            (strncmp(end, ".tmp-", 5) == 0 && strlen(end) == 11 && template_letters(end + 5, 6))) {
            snprintf(key, max, "%s%s", stored[i].key, end);
            if (main != NULL) *main = *end == '\0';
            return (int)i;
        }
    }
    return -1;
}

/** @brief Forget settings file @p i's copy; the next read loads it again.
 *  A descriptor still reading the old copy keeps it until it closes. */
static void stored_changed(unsigned i)
{
    rnode_t *copy = stored[i].copy;

    stored[i].copy = NULL;
    if (copy == NULL) return;
    if (copy->opened) copy->unlinked = 1;
    else node_free(copy);
}

/** @brief @p n when it is a settings file's copy, else NULL. */
static rnode_t *stored_owner(const rnode_t *n)
{
    for (unsigned i = 0; i < STORED; i++) {
        if (n != NULL && stored[i].copy == n) return stored[i].copy;
    }
    return NULL;
}

/** @brief Give back the layer's lock while the library's call waits; the
 *  call's result, with its errno. */
#define WITHOUT_LAYER(result, call) \
    do { \
        unsigned held_ = leave_all(); \
        int errno_; \
        (result) = (call); \
        errno_ = errno; \
        enter_again(held_); \
        errno = errno_; \
    } while (0)

/** @brief Seed settings file @p i in the store with its default.
 *  @return 0, or -1 with errno */
static int stored_seed(unsigned i)
{
    int fd = tiku_posix_open(stored[i].key, O_CREAT | O_EXCL | O_WRONLY), r;
    size_t n = strlen(stored[i].seed);

    if (fd < 0) return errno == EEXIST ? 0 : -1;
    if (tiku_posix_write(fd, stored[i].seed, n) != (ssize_t)n) {
        int e = errno;
        WITHOUT_LAYER(r, tiku_posix_close(fd));
        errno = e;
        return -1;
    }
    WITHOUT_LAYER(r, tiku_posix_close(fd));
    return r;
}

/**
 * @brief Settings file @p i's published content, loaded from the store into
 *        a node outside the tree when it is not loaded; a missing record
 *        with a default is seeded first.
 * @return the copy, or NULL with errno: ENOENT for a record the store does
 *         not have
 */
static rnode_t *stored_copy(unsigned i)
{
    struct stat st;
    rnode_t *n;
    int fd, seeded = 0;
    size_t got = 0;

    if (stored[i].copy != NULL) return stored[i].copy;
    for (;;) {
        fd = tiku_posix_open(stored[i].key, O_RDONLY);
        if (fd >= 0 || errno != ENOENT || stored[i].seed == NULL || seeded) break;
        if (stored_seed(i) != 0) return NULL;
        seeded = 1;
    }
    if (fd < 0) return NULL;
    n = calloc(1, sizeof *n);
    if (n == NULL || tiku_posix_fstat(fd, &st) != 0 || reserve(n, (size_t)st.st_size + 1u) != 0) {
        int e = n == NULL ? ENOMEM : errno;
        if (n != NULL) node_free(n);
        (void)tiku_posix_close(fd);
        errno = e;
        return NULL;
    }
    while (got < (size_t)st.st_size) {
        ssize_t r = tiku_posix_read(fd, n->data + got, (size_t)st.st_size - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    (void)tiku_posix_close(fd);
    if (got != (size_t)st.st_size) {
        node_free(n);
        errno = EIO;
        return NULL;
    }
    snprintf(n->name, sizeof n->name, "%s", strrchr(stored[i].path, '/') + 1);
    n->size = got;
    n->mode = S_IFREG | 0644;
    n->ino = next_ino++;
    n->mtime = wall();
    stored[i].copy = n;
    return n;
}

/** @brief A store file's stat, on a device of its own (the tree's is 1,
 *  /Card's 2).  @return @p result */
static int stored_stat(int result, struct stat *st)
{
    if (result == 0) st->st_dev = 4;
    return result;
}

/** @brief Whether directory @p dir holds a settings file. */
static int stored_parent(const rnode_t *dir)
{
    char parent[PATH_MAX];

    for (unsigned i = 0; i < STORED; i++) {
        size_t n = (size_t)(strrchr(stored[i].path, '/') - stored[i].path);
        memcpy(parent, stored[i].path, n);
        parent[n] = '\0';
        if (lookup(parent) == dir) return 1;
    }
    return 0;
}

/** @brief Open a settings file or its temporary: a reader of the file
 *  itself gets its copy; any other open, the library's descriptor.
 *  @return the descriptor, or -1 with errno */
static int stored_open(unsigned i, const char *key, int main, int flags)
{
    int write = (flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC | O_APPEND)) != 0;
    int fd, s;
    rnode_t *copy;

    for (fd = FIRST_FD; fd < FDS && fds[fd].node != NULL; fd++) {
    }
    if (fd == FDS) {
        errno = EMFILE;
        return -1;
    }
    if (main && !write) {
        copy = stored_copy(i);
        if (copy == NULL) return -1;
        fd_fill(fd, copy, flags, 0);
        copy->opened++;
        return fd;
    }
    s = tiku_posix_open(key, flags);
    if (s < 0) return -1;
    fd_fill(fd, &store_device, flags, (uint64_t)s);
    fds[fd].stored = (int)i + 1;
    fds[fd].stored_main = main && write;
    return fd;
}

/** @brief Run the library's publishing call @p call on store descriptor
 *  @p f, busy and without the layer's lock; a write to the file itself
 *  forgets its copy.  @return the call's result, with its errno */
#define STORED_WAIT(f, result, call) \
    do { \
        (f)->busy = 1; \
        WITHOUT_LAYER(result, call); \
        (f)->busy = 0; \
        announce_change(); \
        if ((f)->stored_main) stored_changed((unsigned)(f)->stored - 1u); \
    } while (0)

/*---------------------------------------------------------------------------*/
/* EXTENDED ATTRIBUTES                                                       */
/*---------------------------------------------------------------------------*/
/* A node's extended attributes are a list on the node, so a file's go with
 * it.  The directories in `kept` exist at every start: their attributes are
 * also the store's /data/ts/views record, by path, loaded at start, written
 * when the desktop quits and, after a change, once VIEWS_PACE_US has passed
 * since the last write. */
#define XATTR_NAME 64u
#define XATTR_VALUE 4096u
#define VIEWS_KEY "/data/ts/views"
#define VIEWS_MAGIC "TSV1"
#define VIEWS_PACE_US (3600ull * 1000000ull)
typedef struct xattr {
    struct xattr *next;
    char name[XATTR_NAME];
    size_t len;
    unsigned char value[];
} xattr_t;
static const char *const kept[] = {
    "/home", "/home/Desktop", "/home/Documents", "/home/.config/tracker/Desktop",
};
#define KEPT (sizeof kept / sizeof kept[0])
static int views_dirty;
static uint64_t views_saved_us;

static void attrs_free(rnode_t *n)
{
    while (n->attrs != NULL) {
        xattr_t *a = n->attrs;
        n->attrs = a->next;
        free(a);
    }
}

/** @brief Whether @p n is a directory in `kept`. */
static int node_kept(const rnode_t *n)
{
    for (unsigned i = 0; i < KEPT; i++) {
        if (lookup(kept[i]) == n) return 1;
    }
    return 0;
}

static xattr_t *attr_find(rnode_t *n, const char *name)
{
    for (xattr_t *a = n->attrs; a != NULL; a = a->next) {
        if (strcmp(a->name, name) == 0) return a;
    }
    return NULL;
}

/** @brief Set or, with @p value NULL, remove @p name on @p n.
 *  @return 0, or -1 with errno */
static int attr_put(rnode_t *n, const char *name, const void *value, size_t len, int flags)
{
    xattr_t *old = attr_find(n, name), *a, **p;

    if (strlen(name) >= XATTR_NAME) {
        errno = ERANGE;
        return -1;
    }
    if ((flags & XATTR_CREATE) && old != NULL) {
        errno = EEXIST;
        return -1;
    }
    if (((flags & XATTR_REPLACE) || value == NULL) && old == NULL) {
        errno = ENODATA;
        return -1;
    }
    if (value != NULL && len > XATTR_VALUE) {
        errno = E2BIG;
        return -1;
    }
    a = NULL;
    if (value != NULL) {
        a = malloc(sizeof *a + len);
        if (a == NULL) {
            errno = ENOSPC;
            return -1;
        }
        snprintf(a->name, sizeof a->name, "%s", name);
        a->len = len;
        memcpy(a->value, value, len);
    }
    for (p = &n->attrs; *p != NULL; p = &(*p)->next) {
        if (*p == old) {
            *p = old->next;
            free(old);
            break;
        }
    }
    if (a != NULL) {
        a->next = n->attrs;
        n->attrs = a;
    }
    return 0;
}

static ssize_t attr_get(rnode_t *n, const char *name, void *value, size_t size)
{
    xattr_t *a = attr_find(n, name);

    if (a == NULL) {
        errno = ENODATA;
        return -1;
    }
    if (size == 0u) return (ssize_t)a->len;
    if (size < a->len) {
        errno = ERANGE;
        return -1;
    }
    memcpy(value, a->value, a->len);
    return (ssize_t)a->len;
}

static ssize_t attr_list(rnode_t *n, char *list, size_t size)
{
    size_t need = 0u, at = 0u;

    for (xattr_t *a = n->attrs; a != NULL; a = a->next) need += strlen(a->name) + 1u;
    if (size == 0u) return (ssize_t)need;
    if (size < need) {
        errno = ERANGE;
        return -1;
    }
    for (xattr_t *a = n->attrs; a != NULL; a = a->next) {
        size_t k = strlen(a->name) + 1u;
        memcpy(list + at, a->name, k);
        at += k;
    }
    return (ssize_t)need;
}

/** @brief The node whose attributes @p path names; NULL with ENOTSUP for a
 *  card or store path, whose files have none here. */
static rnode_t *attr_node(const char *path)
{
    char full[PATH_MAX], key[PATH_MAX];
    int main_file;

    if (under(path, "/Card", full) != NULL ||
        stored_path(path, key, sizeof key, &main_file) >= 0) {
        errno = ENOTSUP;
        return NULL;
    }
    return lookup(path);
}

/** @brief The node of descriptor @p fd's file; NULL with errno. */
static rnode_t *attr_fd_node(int fd)
{
    fd_t *f = fd_of(fd);

    if (f == NULL) return NULL;
    if (f->node == &store_device || f->node->card != NULL ||
        f->node == &random_device) {
        errno = ENOTSUP;
        return NULL;
    }
    return f->node;
}

/** @brief Write the kept directories' attributes to the store.
 *  @return 0, or -1 with errno; the record stays as it was on failure */
static int views_save(void)
{
    unsigned char *buf = NULL;
    size_t len = 4u, cap = 256u;
    int fd, r;

    buf = malloc(cap);
    if (buf == NULL) return -1;
    memcpy(buf, VIEWS_MAGIC, 4u);
    for (unsigned i = 0; i < KEPT; i++) {
        rnode_t *n = lookup(kept[i]);
        for (xattr_t *a = n != NULL ? n->attrs : NULL; a != NULL; a = a->next) {
            size_t pl = strlen(kept[i]) + 1u, nl = strlen(a->name) + 1u;
            size_t need = len + pl + nl + 4u + a->len;
            uint32_t vl = (uint32_t)a->len;
            if (need > cap) {
                unsigned char *grown;
                while (cap < need) cap *= 2u;
                grown = realloc(buf, cap);
                if (grown == NULL) {
                    free(buf);
                    errno = ENOMEM;
                    return -1;
                }
                buf = grown;
            }
            memcpy(buf + len, kept[i], pl);
            len += pl;
            memcpy(buf + len, a->name, nl);
            len += nl;
            memcpy(buf + len, &vl, 4u);
            len += 4u;
            memcpy(buf + len, a->value, a->len);
            len += a->len;
        }
    }
    fd = tiku_posix_open(VIEWS_KEY, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0 || tiku_posix_write(fd, buf, len) != (ssize_t)len) {
        int e = errno;
        free(buf);
        if (fd >= 0) WITHOUT_LAYER(r, tiku_posix_close(fd));
        errno = e;
        return -1;
    }
    free(buf);
    WITHOUT_LAYER(r, tiku_posix_close(fd));
    if (r == 0) {
        views_dirty = 0;
        views_saved_us = surface_desktop_now_us();
    }
    return r;
}

/** @brief After a change to a kept directory's attributes: write the record
 *  once VIEWS_PACE_US has passed since the last write. */
static void views_changed(void)
{
    views_dirty = 1;
    if (surface_desktop_now_us() - views_saved_us >= VIEWS_PACE_US) (void)views_save();
}

int surface_desktop_views_save(void)
{
    int r = 0;

    enter();
    if (views_dirty) r = views_save();
    leave();
    return r;
}

int surface_desktop_views_load(void)
{
    unsigned char *buf;
    struct stat st;
    size_t got = 0u, at = 4u;
    int fd;

    fd = tiku_posix_open(VIEWS_KEY, O_RDONLY);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    if (tiku_posix_fstat(fd, &st) != 0 || (buf = malloc((size_t)st.st_size + 1u)) == NULL) {
        (void)tiku_posix_close(fd);
        return -1;
    }
    while (got < (size_t)st.st_size) {
        ssize_t r = tiku_posix_read(fd, buf + got, (size_t)st.st_size - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    (void)tiku_posix_close(fd);
    enter();
    if (got >= 4u && memcmp(buf, VIEWS_MAGIC, 4u) == 0) {
        while (at < got) {
            const char *path = (const char *)buf + at, *name;
            size_t pl = strnlen(path, got - at), nl;
            uint32_t vl;
            rnode_t *n;
            if (pl == got - at) break;
            at += pl + 1u;
            name = (const char *)buf + at;
            nl = strnlen(name, got - at);
            if (nl == got - at || got - at - nl - 1u < 4u) break;
            at += nl + 1u;
            memcpy(&vl, buf + at, 4u);
            at += 4u;
            if (vl > got - at) break;
            n = lookup(path);
            if (n != NULL && node_kept(n)) (void)attr_put(n, name, buf + at, vl, 0);
            at += vl;
        }
    }
    leave();
    free(buf);
    views_saved_us = surface_desktop_now_us();
    return 0;
}

static int _open_locked(const char *path, int flags, int mode)
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
    if (((flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC))) && read_only_path(path))
        return -1;
    {
        char key[PATH_MAX];
        int main = 0, i = stored_path(path, key, sizeof key, &main);

        if (i >= 0) return stored_open((unsigned)i, key, main, flags);
    }
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
    fd_fill(fd, n, flags, 0);
    n->opened++;
    return fd;
}

static int _close_locked(int fd)
{
    fd_t *f = fd_of(fd);

    if (fd >= 0 && fd < FIRST_FD) return 0;
    if (f == NULL || fd_idle(f, f->gen) != 0) return -1;
    if (f->node == &store_device) {
        int r;

        STORED_WAIT(f, r, tiku_posix_close((int)f->reader));
        if (r != 0 && errno == EAGAIN) return -1;      /* still open: nothing published */
        f->node = NULL;
        return r;
    }
    if (f->reader != 0) {
        uint64_t reader = f->reader;

        /* Busy while the owner closes the reader: another call on this
         * descriptor waits, then finds it closed. */
        f->reader = 0;
        f->busy = 1;
        card_close(reader);
        f->busy = 0;
        announce_change();
    }
    /* The writes after the first are counted once, here. */
    if (f->wrote == 2) changed(f->node);
    if (--f->node->opened == 0 && f->node->unlinked && f->node != &random_device) node_free(f->node);
    f->node = NULL;
    return 0;
}

/** @brief Read up to @p n bytes of card file @p f, which @p gen opened, at
 *  @p pos, and move its offset past them when @p move.  The descriptor is
 *  busy until the bytes are in @p buf. */
static _ssize_t card_file_read(fd_t *f, unsigned gen, unsigned char *buf, size_t n, off_t pos,
                               int move)
{
    rnode_t *node = f->node;
    card_node_t *c = node->card;
    _ssize_t got = -1;

    if (fd_idle(f, gen) != 0) return -1;
    f->busy = 1;
    if ((size_t)pos >= node->size) {
        got = 0;
        goto done;
    }
    if (n > node->size - (size_t)pos) n = node->size - (size_t)pos;
    if (c->head == NULL && pos == 0) {
        /* The first bytes are kept: type checks and value columns read
         * them again and again, and each card request is slow. */
        size_t want = node->size < CARD_READ ? node->size : CARD_READ;
        unsigned char *head;

        if (f->reader == 0 && (f->reader = card_open(node)) == 0) {
            errno = EIO;
            goto done;
        }
        head = malloc(want ? want : 1u);
        if (head != NULL) {
            _ssize_t k = card_read(f->reader, head, want, 0);

            if (k < 0) {
                free(head);
                goto done;
            }
            /* Another thread may have kept them while this one read. */
            if (c->head == NULL) {
                c->head = head;
                c->head_len = (size_t)k;
            } else {
                free(head);
            }
        }
    }
    if (c->head != NULL && (size_t)pos < c->head_len) {
        size_t k = c->head_len - (size_t)pos;

        if (k > n) k = n;
        memcpy(buf, c->head + pos, k);
        got = (_ssize_t)k;
        goto done;
    }
    if (f->reader == 0 && (f->reader = card_open(node)) == 0) {
        errno = EIO;
        goto done;
    }
    got = card_read(f->reader, buf, n, pos);
done:
    if (got > 0 && move) f->pos = pos + got;
    f->busy = 0;
    announce_change();
    return got;
}

/** @brief Read up to @p n bytes of @p fd at @p at, or at its offset, which
 *  then moves past them, when @p at is NULL. */
static _ssize_t read_at(int fd, void *buf, size_t n, const off_t *at)
{
    fd_t *f;
    off_t pos;

    if (fd >= 0 && fd < FIRST_FD) return 0;
    f = fd_of(fd);
    if (f == NULL) return -1;
    if ((f->flags & O_ACCMODE) == O_WRONLY) {
        errno = EBADF;
        return -1;
    }
    if (f->node == &store_device) {
        off_t keep;
        _ssize_t got;

        if (at == NULL) return tiku_posix_read((int)f->reader, buf, n);
        keep = tiku_posix_lseek((int)f->reader, 0, SEEK_CUR);
        if (keep < 0 || tiku_posix_lseek((int)f->reader, *at, SEEK_SET) < 0) return -1;
        got = tiku_posix_read((int)f->reader, buf, n);
        (void)tiku_posix_lseek((int)f->reader, keep, SEEK_SET);
        return got;
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
    pos = at != NULL ? *at : f->pos;
    if (f->node->card != NULL && S_ISREG(f->node->mode))
        return card_file_read(f, f->gen, buf, n, pos, at == NULL);
    if ((size_t)pos >= f->node->size) return 0;
    if (n > f->node->size - (size_t)pos) n = f->node->size - (size_t)pos;
    memcpy(buf, f->node->data + pos, n);
    if (at == NULL) f->pos = pos + (off_t)n;
    return (_ssize_t)n;
}

static _ssize_t _read_locked(int fd, void *buf, size_t n) { return read_at(fd, buf, n, NULL); }

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

static _ssize_t _write_locked(int fd, const void *buf, size_t n)
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
    if (f->node == &store_device) return tiku_posix_write((int)f->reader, buf, n);
    at = (f->flags & O_APPEND) ? f->node->size : (size_t)f->pos;
    if (at > SIZE_MAX - n || reserve(f->node, at + n) != 0) return -1;
    memcpy(f->node->data + at, buf, n);
    if (!f->wrote) {
        changed(f->node);
        f->wrote = 1;
    } else {
        f->wrote = 2;
    }
    file_bytes += (uint32_t)n;
    file_writes++;
    last_written = f->node;
    if (at + n > f->node->size) f->node->size = at + n;
    f->pos = (off_t)(at + n);
    f->node->mtime = wall();
    return (_ssize_t)n;
}

static off_t _lseek_locked(int fd, off_t offset, int whence)
{
    fd_t *f = fd_of(fd);
    off_t base;

    if (f == NULL) return -1;
    if (f->node == &store_device) return tiku_posix_lseek((int)f->reader, offset, whence);
    base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? f->pos :
           whence == SEEK_END ? (off_t)f->node->size : -1;
    if (base < 0 || base + offset < 0) {
        errno = EINVAL;
        return -1;
    }
    f->pos = base + offset;
    return f->pos;
}

static int _fstat_locked(int fd, struct stat *st)
{
    fd_t *f;

    if (fd >= 0 && fd < FIRST_FD) {
        memset(st, 0, sizeof *st);
        st->st_mode = S_IFCHR | 0620;
        return 0;
    }
    f = fd_of(fd);
    if (f == NULL) return -1;
    if (f->node == &store_device) return stored_stat(tiku_posix_fstat((int)f->reader, st), st);
    fill_stat(f->node, st);
    return 0;
}

static int _stat_locked(const char *path, struct stat *st)
{
    char kernel[PATH_MAX];
    rnode_t *n;

    {
        int main = 0, i = stored_path(path, kernel, sizeof kernel, &main);

        if (i >= 0 && !main) return stored_stat(tiku_posix_stat(kernel, st), st);
        if (i >= 0) {
            n = stored_copy((unsigned)i);
            if (n == NULL) return -1;
            fill_stat(n, st);
            return stored_stat(0, st);
        }
    }
    n = lookup(path);
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
static int _fcntl_locked(int fd, int cmd, int arg)
{
    fd_t *f = fd_of(fd);

    if (f == NULL && !(fd >= 0 && fd < FIRST_FD)) {
        errno = EBADF;
        return -1;
    }
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

static ssize_t pread_locked(int fd, void *buf, size_t n, off_t offset) { return read_at(fd, buf, n, &offset); }

static ssize_t pwrite_locked(int fd, const void *buf, size_t n, off_t offset)
{
    fd_t *f = fd_of(fd);
    off_t saved;
    ssize_t put;

    if (f == NULL) return -1;
    if (f->node == &store_device && !(f->flags & O_APPEND)) {
        off_t keep = tiku_posix_lseek((int)f->reader, 0, SEEK_CUR);

        if (keep < 0 || tiku_posix_lseek((int)f->reader, offset, SEEK_SET) < 0) return -1;
        put = tiku_posix_write((int)f->reader, buf, n);
        (void)tiku_posix_lseek((int)f->reader, keep, SEEK_SET);
        return put;
    }
    if (f->flags & O_APPEND) return _write(fd, buf, n);
    saved = f->pos;
    f->pos = offset;
    put = _write(fd, buf, n);
    f->pos = saved;
    return put;
}

static int _unlink_locked(const char *path)
{
    char key[PATH_MAX];
    int main = 0, i = stored_path(path, key, sizeof key, &main), r;

    if (i >= 0) {
        WITHOUT_LAYER(r, tiku_posix_unlink(key));
        if (main) stored_changed((unsigned)i);
        return r;
    }
    if (read_only_path(path)) return -1;
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

static int _link_locked(const char *old, const char *new)
{
    if (read_only_path(old) || read_only_path(new)) return -1;
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

static int _rename_locked(const char *old, const char *new)
{
    char ka[PATH_MAX], kb[PATH_MAX];
    int ma = 0, mb = 0, r;
    int ia = stored_path(old, ka, sizeof ka, &ma), ib = stored_path(new, kb, sizeof kb, &mb);

    if (ia >= 0 || ib >= 0) {
        if (ia < 0 || ib < 0) {
            errno = EXDEV;          /* the store and the tree are two file systems */
            return -1;
        }
        WITHOUT_LAYER(r, tiku_posix_rename(ka, kb));
        if (ma) stored_changed((unsigned)ia);
        if (mb) stored_changed((unsigned)ib);
        return r;
    }
    if (read_only_path(old) || read_only_path(new)) return -1;
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

static int mkdir_locked(const char *path, mode_t mode)
{
    if (read_only_path(path)) return -1;
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

static int rmdir_locked(const char *path)
{
    if (read_only_path(path)) return -1;
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
static int access_locked(const char *path, int mode)
{
    char kernel[PATH_MAX];
    struct stat st;
    rnode_t *n;

    if (stored_path(path, kernel, sizeof kernel, NULL) >= 0) {
        if (_stat_locked(path, &st) != 0) return -1;
        if (mode & X_OK) {
            errno = EACCES;
            return -1;
        }
        return 0;
    }
    n = lookup(path);
    if (n == NULL) return -1;
    if (((mode & R_OK) && !(n->mode & S_IRUSR)) || ((mode & W_OK) && !(n->mode & S_IWUSR)) ||
        ((mode & X_OK) && !(n->mode & S_IXUSR))) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

static int chdir_locked(const char *path)
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

static char *getcwd_locked(char *buf, size_t size)
{
    if (buf == NULL || size <= strlen(cwd)) {
        errno = ERANGE;
        return NULL;
    }
    strcpy(buf, cwd);
    return buf;
}

static char *realpath_locked(const char *path, char *resolved)
{
    char key[PATH_MAX], full[PATH_MAX];
    struct stat st;

    if (stored_path(path, key, sizeof key, NULL) >= 0) {
        const char *at = under(path, "/home", full);
        char *out = resolved;

        if (_stat_locked(path, &st) != 0) return NULL;
        if (out == NULL && (out = malloc(PATH_MAX)) == NULL) {
            errno = ENOMEM;
            return NULL;
        }
        snprintf(out, PATH_MAX, "%s", at);
        return out;
    }
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

static ssize_t readlink_locked(const char *path, char *buf, size_t size)
{
    (void)buf; (void)size;
    if (lookup(path) != NULL) errno = EINVAL;
    return -1;
}

int symlink(const char *target, const char *path)
{
    if (read_only_path(path)) return -1;
    (void)target; (void)path;
    errno = EPERM;
    return -1;
}

/** @brief A store file keeps mode 0644: that mode is accepted, another
 *  refused with EPERM. */
static int stored_mode(mode_t mode)
{
    if ((mode & 07777) == 0644) return 0;
    errno = EPERM;
    return -1;
}

static int chmod_locked(const char *path, mode_t mode)
{
    char key[PATH_MAX];
    struct stat st;

    if (stored_path(path, key, sizeof key, NULL) >= 0)
        return _stat_locked(path, &st) != 0 ? -1 : stored_mode(mode);
    if (read_only_path(path)) return -1;
    rnode_t *n = lookup(path);

    if (n == NULL) return -1;
    if ((n->mode & 07777) != (mode & 07777)) changed(n);
    n->mode = (n->mode & ~(mode_t)07777) | (mode & 07777);
    return 0;
}

static int fchmod_locked(int fd, mode_t mode)
{
    fd_t *f = fd_of(fd);

    if (f != NULL && (f->node == &store_device || f->node == stored_owner(f->node)))
        return stored_mode(mode);
    if (f != NULL && f->node->card != NULL) {
        errno = EROFS;
        return -1;
    }

    if (f == NULL) return -1;
    if ((f->node->mode & 07777) != (mode & 07777)) changed(f->node);
    f->node->mode = (f->node->mode & ~(mode_t)07777) | (mode & 07777);
    return 0;
}

static int chown_locked(const char *path, uid_t uid, gid_t gid) { (void)uid; (void)gid; return lookup(path) ? 0 : -1; }
int lchown(const char *path, uid_t uid, gid_t gid) { return chown(path, uid, gid); }
mode_t umask(mode_t mask) { (void)mask; return 022; }
/**
 * @brief A store descriptor publishes its writes; a directory holding a
 *        settings file is synced as the store's directory, 0 while the store
 *        is mounted, since a rename returns once it is durable.
 */
static int fsync_locked(int fd)
{
    fd_t *f;
    int r = 0, d;

    if (fd >= 0 && fd < FIRST_FD) return 0;
    f = fd_of(fd);
    if (f == NULL) return -1;
    if (f->node == &store_device) {
        if (fd_idle(f, f->gen) != 0) return -1;
        STORED_WAIT(f, r, tiku_posix_fsync((int)f->reader));
        return r;
    }
    if (S_ISDIR(f->node->mode) && stored_parent(f->node)) {
        d = tiku_posix_open("/data", O_RDONLY);
        if (d < 0) return -1;
        r = tiku_posix_fsync(d);
        (void)tiku_posix_close(d);
    }
    return r;
}

static int ftruncate_locked(int fd, off_t length)
{
    fd_t *f = fd_of(fd);

    if (f != NULL && (f->node == &store_device || f->node == stored_owner(f->node))) {
        errno = EINVAL;             /* a store file is truncated by O_TRUNC */
        return -1;
    }
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

static int utimensat_locked(int dirfd, const char *path, const struct timespec times[2], int flags)
{
    if (read_only_path(path)) return -1;
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

static int statvfs_locked(const char *path, struct statvfs *sv)
{
    size_t used, total;

    if (lookup(path) == NULL) return -1;
    surface_os_desktop_heap_use(&used, &total, NULL, NULL);
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

static char *mkdtemp_locked(char *tmpl)
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

static int mkstemps_locked(char *tmpl, int suffix)
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
        fd = _open(tmpl, O_RDWR | O_CREAT | O_EXCL, 0600);
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

static DIR *opendir_locked(const char *path)
{
    rnode_t *n;
    rdir_t *d;

    n = lookup(path);
    if (n == NULL) return NULL;
    if (!S_ISDIR(n->mode)) {
        errno = ENOTDIR;
        return NULL;
    }
    if (n->card != NULL) card_ready(n);
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

static struct dirent *readdir_locked(DIR *dp)
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

static void rewinddir_locked(DIR *dp)
{
    rdir_t *d = (rdir_t *)dp;

    if (d == NULL) return;
    d->at = d->dir->child;
    d->dots = 0;
}

static int closedir_locked(DIR *dp)
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
    (void)surface_desktop_views_save();
    surface_desktop_bridge.exit_code = code;
    surface_desktop_bridge.exited = 1;
    surface_os_desktop_kick();
    surface_os_desktop_exit();
    for (;;) {
    }
}

/*---------------------------------------------------------------------------*/
/* SYSTEM CALLS UNDER THE LAYER'S LOCK                                       */
/*---------------------------------------------------------------------------*/

int _open(const char *path, int flags, int mode)
{
    int r;

    enter();
    r = _open_locked(path, flags, mode);
    leave();
    return r;
}

int _close(int fd)
{
    int r;

    enter();
    r = _close_locked(fd);
    leave();
    return r;
}

_ssize_t _read(int fd, void *buf, size_t n)
{
    _ssize_t r;

    enter();
    r = _read_locked(fd, buf, n);
    leave();
    return r;
}

_ssize_t _write(int fd, const void *buf, size_t n)
{
    _ssize_t r;

    enter();
    r = _write_locked(fd, buf, n);
    leave();
    return r;
}

off_t _lseek(int fd, off_t offset, int whence)
{
    off_t r;

    enter();
    r = _lseek_locked(fd, offset, whence);
    leave();
    return r;
}

int _fstat(int fd, struct stat *st)
{
    int r;

    enter();
    r = _fstat_locked(fd, st);
    leave();
    return r;
}

int _stat(const char *path, struct stat *st)
{
    int r;

    enter();
    r = _stat_locked(path, st);
    leave();
    return r;
}

ssize_t pread(int fd, void *buf, size_t n, off_t offset)
{
    ssize_t r;

    enter();
    r = pread_locked(fd, buf, n, offset);
    leave();
    return r;
}

ssize_t pwrite(int fd, const void *buf, size_t n, off_t offset)
{
    ssize_t r;

    enter();
    r = pwrite_locked(fd, buf, n, offset);
    leave();
    return r;
}

int _unlink(const char *path)
{
    int r;

    enter();
    r = _unlink_locked(path);
    leave();
    return r;
}

int _link(const char *old, const char *new)
{
    int r;

    enter();
    r = _link_locked(old, new);
    leave();
    return r;
}

int _rename(const char *old, const char *new)
{
    int r;

    enter();
    r = _rename_locked(old, new);
    leave();
    return r;
}

int mkdir(const char *path, mode_t mode)
{
    int r;

    enter();
    r = mkdir_locked(path, mode);
    leave();
    return r;
}

int rmdir(const char *path)
{
    int r;

    enter();
    r = rmdir_locked(path);
    leave();
    return r;
}

int access(const char *path, int mode)
{
    int r;

    enter();
    r = access_locked(path, mode);
    leave();
    return r;
}

int chdir(const char *path)
{
    int r;

    enter();
    r = chdir_locked(path);
    leave();
    return r;
}

char *getcwd(char *buf, size_t size)
{
    char *r;

    enter();
    r = getcwd_locked(buf, size);
    leave();
    return r;
}

char *realpath(const char *path, char *resolved)
{
    char *r;

    enter();
    r = realpath_locked(path, resolved);
    leave();
    return r;
}

ssize_t readlink(const char *path, char *buf, size_t size)
{
    ssize_t r;

    enter();
    r = readlink_locked(path, buf, size);
    leave();
    return r;
}

int chmod(const char *path, mode_t mode)
{
    int r;

    enter();
    r = chmod_locked(path, mode);
    leave();
    return r;
}

int fchmod(int fd, mode_t mode)
{
    int r;

    enter();
    r = fchmod_locked(fd, mode);
    leave();
    return r;
}

int chown(const char *path, uid_t uid, gid_t gid)
{
    int r;

    enter();
    r = chown_locked(path, uid, gid);
    leave();
    return r;
}

int fsync(int fd)
{
    int r;

    enter();
    r = fsync_locked(fd);
    leave();
    return r;
}

int ftruncate(int fd, off_t length)
{
    int r;

    enter();
    r = ftruncate_locked(fd, length);
    leave();
    return r;
}

int utimensat(int dirfd, const char *path, const struct timespec times[2], int flags)
{
    int r;

    enter();
    r = utimensat_locked(dirfd, path, times, flags);
    leave();
    return r;
}

int statvfs(const char *path, struct statvfs *sv)
{
    int r;

    enter();
    r = statvfs_locked(path, sv);
    leave();
    return r;
}

char *mkdtemp(char *tmpl)
{
    char *r;

    enter();
    r = mkdtemp_locked(tmpl);
    leave();
    return r;
}

int mkstemps(char *tmpl, int suffix)
{
    int r;

    enter();
    r = mkstemps_locked(tmpl, suffix);
    leave();
    return r;
}

DIR *opendir(const char *path)
{
    DIR *r;

    enter();
    r = opendir_locked(path);
    leave();
    return r;
}

struct dirent *readdir(DIR *dp)
{
    struct dirent *r;

    enter();
    r = readdir_locked(dp);
    leave();
    return r;
}

int closedir(DIR *dp)
{
    int r;

    enter();
    r = closedir_locked(dp);
    leave();
    return r;
}

/** @brief Set (@p value non-NULL) or remove attribute @p name of node @p n,
 *  under the layer's lock; a kept directory's change reaches the store. */
static int attr_change(rnode_t *n, const char *name, const void *value, size_t size, int flags)
{
    if (n == NULL) return -1;
    if (attr_put(n, name, value, size, flags) != 0) return -1;
    if (node_kept(n)) views_changed();
    return 0;
}

ssize_t getxattr(const char *path, const char *name, void *value, size_t size)
{
    rnode_t *n;
    ssize_t r = -1;

    enter();
    n = attr_node(path);
    if (n != NULL) r = attr_get(n, name, value, size);
    leave();
    return r;
}

ssize_t fgetxattr(int fd, const char *name, void *value, size_t size)
{
    rnode_t *n;
    ssize_t r = -1;

    enter();
    n = attr_fd_node(fd);
    if (n != NULL) r = attr_get(n, name, value, size);
    leave();
    return r;
}

ssize_t listxattr(const char *path, char *list, size_t size)
{
    rnode_t *n;
    ssize_t r = -1;

    enter();
    n = attr_node(path);
    if (n != NULL) r = attr_list(n, list, size);
    leave();
    return r;
}

int setxattr(const char *path, const char *name, const void *value, size_t size, int flags)
{
    int r;

    enter();
    r = attr_change(attr_node(path), name, value != NULL ? value : "", size, flags);
    leave();
    return r;
}

int fsetxattr(int fd, const char *name, const void *value, size_t size, int flags)
{
    int r;

    enter();
    r = attr_change(attr_fd_node(fd), name, value != NULL ? value : "", size, flags);
    leave();
    return r;
}

int removexattr(const char *path, const char *name)
{
    int r;

    enter();
    r = attr_change(attr_node(path), name, NULL, 0u, 0);
    leave();
    return r;
}

int fremovexattr(int fd, const char *name)
{
    int r;

    enter();
    r = attr_change(attr_fd_node(fd), name, NULL, 0u, 0);
    leave();
    return r;
}

int surface_desktop_files(void)
{
    int r;

    enter();
    r = surface_desktop_files_locked();
    leave();
    return r;
}

int _fcntl(int fd, int cmd, ...)
{
    va_list ap;
    int arg, r;

    va_start(ap, cmd);
    arg = va_arg(ap, int);
    va_end(ap);
    enter();
    r = _fcntl_locked(fd, cmd, arg);
    leave();
    return r;
}

void rewinddir(DIR *dp)
{
    enter();
    rewinddir_locked(dp);
    leave();
}

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
