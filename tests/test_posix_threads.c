/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_posix_threads.c - the desktop's file layer from several threads at
 * once, with a stand-in card whose requests the test holds open.  Built
 * twice: under the thread sanitizer and under the address sanitizer.
 *
 * Built with tests/posix_host/ ahead of the system headers and its rename.h
 * forced in, so these calls reach desktop/posix.c's tree.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
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
ssize_t _read(int fd, void *buf, size_t n);
ssize_t _write(int fd, const void *buf, size_t n);
int surface_desktop_files(void);

/*---------------------------------------------------------------------------*/
/* A CARD WHOSE REQUESTS WAIT FOR THE TEST                                   */
/*---------------------------------------------------------------------------*/

/* Three objects, in name order; a byte's value is its offset plus the
 * object's index. */
static const struct { const char *name; size_t size; } objects[] = {
    { "dir/a.txt", 3000u }, { "dir/b.txt", 500u }, { "dir/c.txt", 2500u },
};
#define OBJECTS (sizeof objects / sizeof objects[0])
#define READERS 8u

static pthread_mutex_t card = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t card_moved = PTHREAD_COND_INITIALIZER;
static unsigned held_ops;          /* bit per operation the test holds */
static unsigned waiting, served, closes, reads_done;
static int reader_of[READERS];     /* an open reader's object, or -1 */

static unsigned char byte_of(unsigned object, uint64_t at) { return (unsigned char)(at + object); }

/** @brief Answer one request; a request whose operation the test holds
 *  waits until it lets go. */
void surface_desktop_card(surface_media_io_t *io)
{
    pthread_mutex_lock(&card);
    waiting++;
    pthread_cond_broadcast(&card_moved);
    while (held_ops & (1u << io->operation)) pthread_cond_wait(&card_moved, &card);
    waiting--;
    io->result = SURFACE_MEDIA_OK;
    switch (io->operation) {
    case SURFACE_MEDIA_ENUMERATE: {
        unsigned i;

        for (i = 0; i < OBJECTS; i++) {
            int c = strcmp(objects[i].name, io->name);

            if (c > 0 || (c == 0 && !io->exclusive)) break;
        }
        if (i == OBJECTS) {
            io->result = SURFACE_MEDIA_END;
            break;
        }
        snprintf(io->found, sizeof io->found, "%s", objects[i].name);
        io->size = objects[i].size;
        io->epoch = 1u;
        io->rank = i;
        io->id = i + 1u;
        break;
    }
    case SURFACE_MEDIA_OPEN: {
        unsigned r;

        for (r = 0; r < READERS && reader_of[r] >= 0; r++) {
        }
        assert(r < READERS && io->rank < OBJECTS && !strcmp(io->name, objects[io->rank].name));
        reader_of[r] = (int)io->rank;
        io->reader = r + 1u;
        break;
    }
    case SURFACE_MEDIA_READ: {
        int o = reader_of[io->reader - 1u];
        size_t n = 0;

        assert(o >= 0);
        while (n < io->length && io->offset + n < objects[o].size) {
            io->bytes[n] = byte_of((unsigned)o, io->offset + n);
            n++;
        }
        io->count = n;
        reads_done++;
        break;
    }
    case SURFACE_MEDIA_DELETE:
        break;
    case SURFACE_MEDIA_CLOSE:
        assert(reader_of[io->reader - 1u] >= 0);
        reader_of[io->reader - 1u] = -1;
        closes++;
        break;
    default:
        io->result = SURFACE_MEDIA_UNSUPPORTED;
    }
    served++;
    pthread_cond_broadcast(&card_moved);
    pthread_mutex_unlock(&card);
}

static void hold(unsigned operation)
{
    pthread_mutex_lock(&card);
    held_ops |= 1u << operation;
    pthread_mutex_unlock(&card);
}

static void let_go(unsigned operation)
{
    pthread_mutex_lock(&card);
    held_ops &= ~(1u << operation);
    pthread_cond_broadcast(&card_moved);
    pthread_mutex_unlock(&card);
}

/** @brief Wait until @p n requests wait on a held operation. */
static void await_waiting(unsigned n)
{
    pthread_mutex_lock(&card);
    while (waiting < n) pthread_cond_wait(&card_moved, &card);
    pthread_mutex_unlock(&card);
}

static unsigned card_value(unsigned *what)
{
    unsigned v;

    pthread_mutex_lock(&card);
    v = *what;
    pthread_mutex_unlock(&card);
    return v;
}

/*---------------------------------------------------------------------------*/
/* THE REST OF THE DESKTOP SIDE                                              */
/*---------------------------------------------------------------------------*/

surface_desktop_bridge_t surface_desktop_bridge;
unsigned surface_desktop_card_foreign(void) { return 0; }
char **environ;
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
int surface_os_clock_state(void) { return SURFACE_CLOCK_SET; }
void tiku_sha256(const void *data, size_t n, unsigned char out[32]) { (void)data; (void)n; memset(out, 1, 32); }

/* No /Board here; /data is the fake store, whose publications wait while
 * the test holds STORE_OP, as the library's wait for the flash would. */
#include "fake_store.h"
#define STORE_OP 31u
static void store_waits(void)
{
    pthread_mutex_lock(&card);
    waiting++;
    pthread_cond_broadcast(&card_moved);
    while (held_ops & (1u << STORE_OP)) pthread_cond_wait(&card_moved, &card);
    waiting--;
    pthread_mutex_unlock(&card);
}
int tiku_posix_open(const char *path, int flags)
{ if (fake_path(path)) return fake_open(path, flags); errno = ENOENT; return -1; }
ssize_t tiku_posix_read(int fd, void *buf, size_t n) { return fake_read(fd, buf, n); }
off_t tiku_posix_lseek(int fd, off_t offset, int whence) { return fake_lseek(fd, offset, whence); }
int tiku_posix_close(int fd) { return fake_close(fd); }
int tiku_posix_fcntl(int fd, int cmd) { (void)fd; (void)cmd; errno = EBADF; return -1; }
int tiku_posix_fstat(int fd, struct stat *st) { return fake_fstat(fd, st); }
int tiku_posix_stat(const char *path, struct stat *st)
{ if (fake_path(path)) return fake_stat(path, st); errno = ENOENT; return -1; }
int tiku_posix_is_open(int fd) { return fake_slot(fd) != NULL; }
ssize_t tiku_posix_write(int fd, const void *buf, size_t n) { return fake_write(fd, buf, n); }
int tiku_posix_fsync(int fd) { return fake_fsync(fd); }
int tiku_posix_rename(const char *from, const char *to) { return fake_rename(from, to); }
int tiku_posix_unlink(const char *path) { return fake_unlink(path); }
DIR *tiku_posix_opendir(const char *path) { (void)path; errno = ENOENT; return NULL; }
struct dirent *tiku_posix_readdir(DIR *dir) { (void)dir; return NULL; }
void tiku_posix_rewinddir(DIR *dir) { (void)dir; }
int tiku_posix_closedir(DIR *dir) { (void)dir; errno = EBADF; return -1; }
int tiku_posix_is_dir_stream(const DIR *dir) { (void)dir; return 0; }
uint8_t tiku_posix_authority_set(uint8_t cap) { (void)cap; return 0; }

/*---------------------------------------------------------------------------*/
/* THREADS                                                                   */
/*---------------------------------------------------------------------------*/

typedef struct {
    const char *path;
    int fd;
    off_t at;               /* where pread starts; -1 for read */
    size_t want;
    unsigned char buf[3000];
    ssize_t got;
    int err, done;
    unsigned names;         /* list: entries seen */
} job_t;

static void *reads(void *arg)
{
    job_t *j = arg;

    if (j->fd < 0) j->fd = _open(j->path, O_RDONLY, 0);
    j->got = j->at < 0 ? _read(j->fd, j->buf, j->want) : pread(j->fd, j->buf, j->want, j->at);
    j->err = errno;
    __atomic_store_n(&j->done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

static void *closes_fd(void *arg)
{
    job_t *j = arg;

    j->got = _close(j->fd);
    j->err = errno;
    __atomic_store_n(&j->done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

static void *lists(void *arg)
{
    job_t *j = arg;
    DIR *d = opendir(j->path);
    struct dirent *e;

    assert(d != NULL);
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) j->names++;
    }
    assert(closedir(d) == 0);
    __atomic_store_n(&j->done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

static void *writes(void *arg)
{
    job_t *j = arg;

    j->fd = _open(j->path, O_WRONLY | O_TRUNC, 0);
    j->err = errno;
    if (j->fd >= 0) j->got = _close(j->fd);
    __atomic_store_n(&j->done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

static int done(const job_t *j) { return __atomic_load_n(&j->done, __ATOMIC_SEQ_CST); }

/* rename.h hands nanosleep() to the layer, whose sleep returns at once;
 * the test sleeps on the host's. */
#undef nanosleep
int nanosleep(const struct timespec *request, struct timespec *remain);

static void pause_ms(unsigned ms)
{
    struct timespec t = { 0, (long)ms * 1000000L };

    nanosleep(&t, NULL);
}

/** @brief @p n bytes of object @p o from @p at, as the card holds them. */
static int bytes_are(const unsigned char *buf, size_t n, unsigned o, uint64_t at)
{
    for (size_t i = 0; i < n; i++) {
        if (buf[i] != byte_of(o, at + i)) return 0;
    }
    return 1;
}

int mkstemp(char *tmpl);
int _rename(const char *old, const char *new);
int _unlink(const char *path);
int _stat(const char *path, struct stat *st);

static void *unlinks(void *arg)
{
    job_t *j = arg;

    j->got = _unlink(j->path);
    j->err = errno;
    __atomic_store_n(&j->done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

/* A save of a settings file through a temporary, as Stomme's is. */
static void *saves(void *arg)
{
    job_t *j = arg;
    char tmp[64] = "/home/.config/TrackerSettings.tmp-XXXXXX";
    int fd = mkstemp(tmp);

    j->got = fd >= 0 && _write(fd, "DarkMode 1\n", 11) == 11 && fsync(fd) == 0 &&
             _close(fd) == 0 && _rename(tmp, "/home/.config/TrackerSettings") == 0;
    __atomic_store_n(&j->done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

int main(void)
{
    pthread_t a, b;

    for (unsigned r = 0; r < READERS; r++) reader_of[r] = -1;
    alarm(30);              /* a lock held across a card request hangs here */
    surface_desktop_bridge.card_mounted = 1;
    assert(surface_desktop_files() == 0);

    /* Two threads list one card directory while its requests wait: the
     * second waits for the first's listing and both see all of it. */
    {
        static job_t one = { .path = "/Card/dir" }, two = { .path = "/Card/dir" };

        hold(SURFACE_MEDIA_ENUMERATE);
        pthread_create(&a, NULL, lists, &one);
        await_waiting(1);
        pthread_create(&b, NULL, lists, &two);
        pause_ms(50);
        let_go(SURFACE_MEDIA_ENUMERATE);
        pthread_join(a, NULL);
        pthread_join(b, NULL);
        assert(one.names == OBJECTS && two.names == OBJECTS);
    }

    /* While one thread waits on a card read, another uses the tree. */
    {
        static job_t r = { .path = "/Card/dir/a.txt", .fd = -1, .at = -1, .want = 3000u };
        int fd;

        hold(SURFACE_MEDIA_READ);
        pthread_create(&a, NULL, reads, &r);
        await_waiting(1);
        fd = _open("/tmp/during", O_WRONLY | O_CREAT, 0644);
        assert(fd >= 0 && _write(fd, "x", 1) == 1 && _close(fd) == 0);
        assert(!done(&r));
        let_go(SURFACE_MEDIA_READ);
        pthread_join(a, NULL);
        /* The first read of a card file gives the bytes it keeps. */
        assert(r.got == 1024 && bytes_are(r.buf, 1024u, 0u, 0u));

        /* A close waits for the read outstanding on its descriptor, and the
         * descriptor is closed for both after. */
        {
            static job_t c;
            unsigned before = card_value(&closes);

            r.done = 0;
            r.want = 1000u;
            hold(SURFACE_MEDIA_READ);
            pthread_create(&a, NULL, reads, &r);
            await_waiting(1);
            c.fd = r.fd;
            pthread_create(&b, NULL, closes_fd, &c);
            pause_ms(50);
            assert(!done(&r) && !done(&c) && card_value(&closes) == before);
            let_go(SURFACE_MEDIA_READ);
            pthread_join(a, NULL);
            pthread_join(b, NULL);
            assert(r.got == 1000 && bytes_are(r.buf, 1000u, 0u, 1024u));
            assert(c.got == 0 && card_value(&closes) == before + 1u);
            assert(_read(r.fd, r.buf, 1) == -1 && errno == EBADF);
        }
    }

    /* Two descriptors of one file read its first bytes at once: each gets
     * them, and the layer keeps one copy (the address sanitizer's leak
     * check fails on a second). */
    {
        static job_t one = { .path = "/Card/dir/c.txt", .fd = -1, .at = 0, .want = 2500u };
        static job_t two = { .path = "/Card/dir/c.txt", .fd = -1, .at = 0, .want = 2500u };

        hold(SURFACE_MEDIA_READ);
        pthread_create(&a, NULL, reads, &one);
        pthread_create(&b, NULL, reads, &two);
        await_waiting(2);
        let_go(SURFACE_MEDIA_READ);
        pthread_join(a, NULL);
        pthread_join(b, NULL);
        assert(one.got == 1024 && bytes_are(one.buf, 1024u, 2u, 0u));
        assert(two.got == 1024 && bytes_are(two.buf, 1024u, 2u, 0u));
        assert(_close(one.fd) == 0 && _close(two.fd) == 0);
    }
    /* While one thread's save of a settings file waits for the store,
     * another reads that file through the layer. */
    {
        static job_t s;
        unsigned char buf[64];
        int fd;
        ssize_t n;

        fake_reset();
        fd = _open("/home/.config/TrackerSettings", O_RDONLY, 0);
        assert(fd >= 0 && _close(fd) == 0);
        fake_during = store_waits;
        hold(STORE_OP);
        pthread_create(&a, NULL, saves, &s);
        await_waiting(1);
        fd = _open("/home/.config/TrackerSettings", O_RDONLY, 0);
        n = fd >= 0 ? _read(fd, buf, sizeof buf) : -1;
        assert(n == 28 && _close(fd) == 0 && !done(&s));
        let_go(STORE_OP);
        pthread_join(a, NULL);
        assert(s.got == 1);
        fake_during = NULL;
        assert(fake_holds("ts/settings", "DarkMode 1\n"));
    }
    /* While one thread's delete of a card file waits for the card, another
     * asks to delete it too: the second finds it gone. */
    {
        static job_t one = { .path = "/Card/dir/b.txt" }, two = { .path = "/Card/dir/b.txt" };
        struct stat st;

        assert(_stat("/Card/dir/b.txt", &st) == 0);
        hold(SURFACE_MEDIA_DELETE);
        pthread_create(&a, NULL, unlinks, &one);
        await_waiting(1);
        pthread_create(&b, NULL, unlinks, &two);
        pause_ms(50);
        assert(!done(&two));
        let_go(SURFACE_MEDIA_DELETE);
        pthread_join(a, NULL);
        pthread_join(b, NULL);
        assert(one.got == 0 && two.got == -1 && two.err == ENOENT);
        assert(_stat("/Card/dir/b.txt", &st) == -1 && errno == ENOENT);
    }
    /* While one thread's delete of a card file waits for the card, another
     * opens it to write: the open finds it gone. */
    {
        static job_t one = { .path = "/Card/dir/c.txt" }, two = { .path = "/Card/dir/c.txt", .fd = -1 };

        hold(SURFACE_MEDIA_DELETE);
        pthread_create(&a, NULL, unlinks, &one);
        await_waiting(1);
        pthread_create(&b, NULL, writes, &two);
        pause_ms(50);
        assert(!done(&two));
        let_go(SURFACE_MEDIA_DELETE);
        pthread_join(a, NULL);
        pthread_join(b, NULL);
        assert(one.got == 0 && two.fd == -1 && two.err == ENOENT);
    }
    assert(card_value(&waiting) == 0u);
    printf("posix threads: a listing is shared, the tree is free during a card read, "
           "a close waits for its descriptor's read, a file's first bytes are kept once, "
           "a settings file reads while its save waits, a card file two threads delete is deleted once, a card file opened to write while it is deleted is gone\n");
    return 0;
}
