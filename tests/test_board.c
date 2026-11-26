/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_board.c - desktop/board.c over a fake kernel namespace: the manifest,
 * values, a change reader, the file store and the library's authority, as
 * TikuOS compat/posix presents them.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "tiku_model.h"
#include "tiku_posix.h"

/*---------------------------------------------------------------------------*/
/* THE FAKE NAMESPACE                                                        */
/*---------------------------------------------------------------------------*/

static char manifest[2048] =
    "# path\ttype\tperms\tmeta(vtype,unit,fresh,cost[,lo..hi])\tcap\tid\n"
    "/sys\td\t--\t-\t-\t00000010\n"
    "/sys/uptime\tf\tr-\tu32,s,live,free;read=poll\t-\t00000011\n"
    "/sys/device\td\t--\t-\t-\t00000012\n"
    "/sys/device/name\tf\trw\tstr,none,static,free;read=poll\tsys\t00000013\n"
    "/sys/led\tf\trw\tbool,none,live,free;read=poll\thw\t00000014\n"
    "/sys/key\tf\tr-\tstr,none,static,free;read=poll;secret\tnet\t00000015\n"
    "/data\td\t--\t-\tfs\t00000020\n"
    "/sys/net/wifi/scan\tf\t-w\t-\tnet\t00000030\n"
    "/sys/cut\tf\tr-\tu32";
static struct { const char *path; char value[64]; } values[] = {
    { "/sys/uptime", "42\n" }, { "/sys/device/name", "panel\n" },
    { "/sys/led", "0\n" }, { "/sys/key", "hunter2\n" },
    { "/sys/net/wifi/scan", "" }, { "/sys/net/wifi/disconnect", "" },
};
#define VALUES (sizeof values / sizeof values[0])
/* Change records waiting for the reader, and its lost count. */
static char records[16][32];
static unsigned record_count;
static unsigned long lost;
static int refuse_authority, events_opened;
static struct { int used; char name[32]; char data[600]; size_t len; } files[8];

enum { NONE, MANIFEST, VALUE, EVENTS, STORE_READ, STORE_WRITE, SETTING };
static struct { int kind; char path[64]; size_t off; char data[600]; size_t len; } fds[8];

static int fd_new(int kind, const char *path)
{
    for (int i = 0; i < 8; i++) {
        if (fds[i].kind == NONE) {
            memset(&fds[i], 0, sizeof fds[i]);
            fds[i].kind = kind;
            snprintf(fds[i].path, sizeof fds[i].path, "%s", path);
            return i + 3;
        }
    }
    errno = EMFILE;
    return -1;
}

static int value_of(const char *path)
{
    for (unsigned i = 0; i < VALUES; i++) {
        if (strcmp(values[i].path, path) == 0) return (int)i;
    }
    return -1;
}

static int file_of(const char *path)
{
    if (strncmp(path, "/data/", 6) != 0) return -1;
    for (int i = 0; i < 8; i++) {
        if (files[i].used && strcmp(files[i].name, path + 6) == 0) return i;
    }
    return -1;
}

int tiku_posix_open(const char *path, int flags)
{
    int writing = (flags & O_ACCMODE) != O_RDONLY;

    if (strcmp(path, "/sys/vfs/manifest") == 0) return fd_new(MANIFEST, path);
    if (strcmp(path, "/sys/vfs/events") == 0) {
        events_opened++;
        return fd_new(EVENTS, path);
    }
    if (strncmp(path, "/data/", 6) == 0) {
        if (writing) return fd_new(STORE_WRITE, path);
        if (file_of(path) < 0) {
            errno = ENOENT;
            return -1;
        }
        return fd_new(STORE_READ, path);
    }
    if (value_of(path) < 0) {
        errno = ENOENT;
        return -1;
    }
    return fd_new(writing ? SETTING : VALUE, path);
}

ssize_t tiku_posix_read(int fd, void *buf, size_t n)
{
    char text[600];
    size_t len, m;
    int i = fd - 3;

    if (i < 0 || i >= 8 || fds[i].kind == NONE) {
        errno = EBADF;
        return -1;
    }
    if (fds[i].kind == EVENTS) {
        /* At most three records a read, as a small read buffer gives. */
        unsigned k = record_count < 3u ? record_count : 3u, at = 0u;

        for (unsigned r = 0; r < k; r++) at += (unsigned)snprintf(text + at, sizeof text - at, "%s\n", records[r]);
        at += (unsigned)snprintf(text + at, sizeof text - at, "# %u read, %lu lost\n", k, lost);
        memmove(records, records + k, (record_count - k) * sizeof records[0]);
        record_count -= k;
        m = at < n ? at : n;
        memcpy(buf, text, m);
        return (ssize_t)m;
    }
    if (fds[i].kind == MANIFEST) {
        len = strlen(manifest);
        m = len - fds[i].off < n ? len - fds[i].off : n;
        if (m > 255u) m = 255u;           /* the library's read limit */
        memcpy(buf, manifest + fds[i].off, m);
        fds[i].off += m;
        return (ssize_t)m;
    }
    if (fds[i].kind == VALUE) {
        const char *v = values[value_of(fds[i].path)].value;
        len = strlen(v);
        m = len - fds[i].off < n ? len - fds[i].off : n;
        memcpy(buf, v + fds[i].off, m);
        fds[i].off += m;
        return (ssize_t)m;
    }
    if (fds[i].kind == STORE_READ) {
        int f = file_of(fds[i].path);
        m = files[f].len - fds[i].off < n ? files[f].len - fds[i].off : n;
        memcpy(buf, files[f].data + fds[i].off, m);
        fds[i].off += m;
        return (ssize_t)m;
    }
    errno = EBADF;
    return -1;
}

ssize_t tiku_posix_write(int fd, const void *buf, size_t n)
{
    int i = fd - 3;

    if (fds[i].kind == SETTING) {
        if (refuse_authority) {
            errno = EPERM;
            return -1;
        }
        int v = value_of(fds[i].path);
        snprintf(values[v].value, sizeof values[v].value, "%.*s", (int)n, (const char *)buf);
        return (ssize_t)n;
    }
    if (fds[i].kind == STORE_WRITE && fds[i].len + n <= sizeof fds[i].data) {
        memcpy(fds[i].data + fds[i].len, buf, n);
        fds[i].len += n;
        return (ssize_t)n;
    }
    errno = EBADF;
    return -1;
}

int tiku_posix_close(int fd)
{
    int i = fd - 3;

    if (fds[i].kind == STORE_WRITE) {
        int f = file_of(fds[i].path);
        for (int k = 0; f < 0 && k < 8; k++) {
            if (!files[k].used) f = k;
        }
        files[f].used = 1;
        snprintf(files[f].name, sizeof files[f].name, "%s", fds[i].path + 6);
        memcpy(files[f].data, fds[i].data, fds[i].len);
        files[f].len = fds[i].len;
    }
    fds[i].kind = NONE;
    return 0;
}

int surface_os_form_access(const char *path)
{
    if (!strcmp(path, "/gui/demo/lamp")) return 1;
    if (!strcmp(path, "/gui/demo") || !strcmp(path, "/gui/demo/status")) return 0;
    return -1;
}

int surface_os_form_write(const char *path, const void *data, size_t len)
{
    return surface_os_form_access(path) == 1 && !refuse_authority &&
           len == 1 && !memcmp(data, "1", 1) ? 0 : -1;
}

int tiku_posix_stat(const char *path, struct stat *st)
{
    int f = file_of(path);

    memset(st, 0, sizeof *st);
    if (surface_os_form_access(path) >= 0) {
        st->st_mode = strcmp(path, "/gui/demo") ? S_IFREG | 0444 : S_IFDIR | 0555;
        st->st_ino = 0x40;
        return 0;
    }
    if (strcmp(path, "/data") == 0) {
        st->st_mode = S_IFDIR | 0755;
        return 0;
    }
    if (f < 0) {
        errno = ENOENT;
        return -1;
    }
    st->st_mode = S_IFREG | 0644;
    st->st_size = (off_t)files[f].len;
    st->st_ino = (ino_t)(100 + f);
    return 0;
}

static DIR *const store_dir = (DIR *)&files;
static int dir_at;
static struct dirent entry;

DIR *tiku_posix_opendir(const char *path)
{
    if (strcmp(path, "/data") != 0) {
        errno = ENOENT;
        return NULL;
    }
    dir_at = 0;
    return store_dir;
}

struct dirent *tiku_posix_readdir(DIR *dir)
{
    (void)dir;
    while (dir_at < 8 && !files[dir_at].used) dir_at++;
    if (dir_at >= 8) return NULL;
    snprintf(entry.d_name, sizeof entry.d_name, "%s", files[dir_at++].name);
    return &entry;
}

int tiku_posix_closedir(DIR *dir) { (void)dir; return 0; }

/* The state store is not this test's: Stomme's sidecar is never opened. */
struct tiku_store *tiku_store_sidecar_open(const char *devid) { (void)devid; return NULL; }
void tiku_store_free(struct tiku_store *s) { (void)s; }

/*---------------------------------------------------------------------------*/
/* THE CHECKS                                                                */
/*---------------------------------------------------------------------------*/

static int counted;
static char seen[8][TIKU_PATH_MAX];

static int count_entry(const tiku_model_t *m, void *ctx)
{
    (void)ctx;
    if (counted < 8) snprintf(seen[counted], sizeof seen[counted], "%s", m->path);
    counted++;
    return 0;
}

static int listed(tiku_backend_t *b, const char *path)
{
    counted = 0;
    return b->ops->list(b, path, count_entry, NULL) == counted ? counted : -1;
}

static void record(const char *line)
{
    snprintf(records[record_count++], sizeof records[0], "%s", line);
}

int main(void)
{
    char name[64], buf[600], err[128];
    tiku_model_t m;
    tiku_model_change_t ch[4];
    tiku_backend_t *b = surface_board_open(name, sizeof name);
    int complete;

    /* The manifest's nodes, a cut last line left out; the board's name. */
    assert(b != NULL && strcmp(name, "panel") == 0 && events_opened == 1);
    assert(b->ops->stat(b, "/", &m) == 0 && m.kind == TIKU_KIND_VOLUME && !strcmp(m.name, "panel"));
    assert(listed(b, "/") == 2 && !strcmp(seen[0], "/sys") && !strcmp(seen[1], "/data"));
    assert(listed(b, "/sys") == 4);
    assert(b->ops->stat(b, "/sys/cut", &m) != 0);
    assert(b->ops->stat(b, "/sys/device/name", &m) == 0 && m.kind == TIKU_KIND_DEVICE_NODE &&
           m.facts.perm == (TIKU_P_READ | TIKU_P_WRITE) && !strcmp(m.facts.req_cap, "sys") &&
           !strcmp(m.facts.meta, "str,none,static,free;read=poll") && m.node_id == 0x13u &&
           !strcmp(m.name, "name"));
    assert(b->ops->stat(b, "/sys", &m) == 0 && m.kind == TIKU_KIND_VIRTUAL_DIR &&
           m.facts.perm == TIKU_P_READ);
    assert(b->ops->stat(b, "/data", &m) == 0 && m.kind == TIKU_KIND_DIRECTORY &&
           (m.facts.perm & TIKU_P_WRITE));

    assert(b->ops->stat(b, "/gui/demo", &m) == 0 && m.kind == TIKU_KIND_VIRTUAL_DIR);
    assert(b->ops->stat(b, "/gui/demo/lamp", &m) == 0 &&
           m.kind == TIKU_KIND_DEVICE_NODE && (m.facts.perm & TIKU_P_WRITE));
    assert(b->ops->stat(b, "/gui/demo/missing", &m) < 0);
    assert(b->ops->stat(b, "/gui/demo/status", &m) == 0 && !(m.facts.perm & TIKU_P_WRITE));
    assert(b->ops->write(b, "/gui/demo/lamp", "1", 1, err, sizeof err) == 0);
    assert(b->ops->write(b, "/gui/demo/status", "1", 1, err, sizeof err) < 0);
    refuse_authority = 1;
    assert(b->ops->write(b, "/gui/demo/lamp", "1", 1, err, sizeof err) < 0);
    refuse_authority = 0;
    /* Values; a secret is not read. */
    assert(b->ops->read(b, "/sys/uptime", buf, sizeof buf) == 3 && !memcmp(buf, "42\n", 3));
    errno = 0;
    assert(b->ops->read(b, "/sys/key", buf, sizeof buf) == -1 && errno == EACCES);

    /* A store file is written whole, listed and read back; a reviewed
     * setting takes its value; other nodes and empty or long values are
     * refused before the kernel; the kernel's refusal names authority. */
    assert(b->ops->write(b, "/data/note", "hello", 5, err, sizeof err) == 0);
    assert(listed(b, "/data") == 1 && !strcmp(seen[0], "/data/note"));
    assert(b->ops->stat(b, "/data/note", &m) == 0 && m.kind == TIKU_KIND_FILE && m.facts.size == 5);
    assert(b->ops->read(b, "/data/note", buf, sizeof buf) == 5 && !memcmp(buf, "hello", 5));
    assert(b->ops->write(b, "/sys/device/name", "desk", 4, err, sizeof err) == 0 &&
           !strcmp(values[1].value, "desk"));
    assert(b->ops->write(b, "/sys/led", "1", 1, err, sizeof err) == -1 &&
           strstr(err, "not changed from the desktop") && !strcmp(values[2].value, "0\n"));
    assert(b->ops->write(b, "/data", "x", 1, err, sizeof err) == -1);
    assert(b->ops->write(b, "/sys/device/name", "", 0, err, sizeof err) == -1);
    memset(buf, 'a', sizeof buf);
    assert(b->ops->write(b, "/data/long", buf, 513, err, sizeof err) == -1 && file_of("/data/long") < 0);
    refuse_authority = 1;
    assert(b->ops->write(b, "/sys/device/name", "x", 1, err, sizeof err) == -1 &&
           strstr(err, "authority") && !strcmp(values[1].value, "desk"));
    refuse_authority = 0;

    assert(b->ops->write(b, "/sys/net/wifi/scan", "start", 5, err, sizeof err) == 0);
    assert(!strcmp(values[value_of("/sys/net/wifi/scan")].value, "start"));
    assert(b->ops->write(b, "/sys/net/wifi/disconnect", "disconnect", 10, err, sizeof err) < 0);
    assert(b->ops->write(b, "/sys/net/wifi/saved", "forget", 6, err, sizeof err) < 0);
    assert(b->ops->write(b, "/sys/net/wifi/profiles/0/password", "secret", 6, err, sizeof err) < 0);
    refuse_authority = 1;
    assert(b->ops->write(b, "/sys/net/wifi/scan", "start", 5, err, sizeof err) < 0 &&
           strstr(err, "authority"));
    refuse_authority = 0;

    /* Changes, each under the folder it touches: a value's folder, a
     * folder itself, and the root for a node the manifest does not name;
     * a created node reloads the manifest. */
    complete = -1;
    assert(b->ops->changes(b, ch, 4, &complete) == 0 && complete == 1);
    record("changed\t00000011\t1");
    record("changed\t00000020\t2");
    record("changed\t00000013\t3");
    assert(b->ops->changes(b, ch, 4, &complete) == 3 && complete == 1);
    assert(ch[0].op == TIKU_CH_SUBTREE && ch[0].id == 0x10u);
    assert(ch[1].op == TIKU_CH_SUBTREE && ch[1].id == 0x20u);
    assert(ch[2].op == TIKU_CH_SUBTREE && ch[2].id == 0x12u);
    assert(b->ops->stat(b, "/", &m) == 0 && b->ops->path_of_id(b, m.node_id, buf, sizeof buf) == 0 &&
           !strcmp(buf, "/"));
    assert(b->ops->path_of_id(b, 0x13u, buf, sizeof buf) == 0 && !strcmp(buf, "/sys/device/name"));
    assert(b->ops->path_of_id(b, 0x99u, buf, sizeof buf) == -1);
    strcat(manifest, "\n/sys/new\tf\tr-\t-\t-\t00000016\n");
    record("created\t00000016\t3");
    assert(b->ops->changes(b, ch, 4, &complete) == 1 && ch[0].op == TIKU_CH_SUBTREE &&
           b->ops->path_of_id(b, ch[0].id, buf, sizeof buf) == 0 && !strcmp(buf, "/"));
    assert(b->ops->stat(b, "/sys/new", &m) == 0 && m.node_id == 0x16u);

    /* More records than room, or records the reader lost, are partial. */
    for (int i = 0; i < 5; i++) record("changed\t00000011\t4");
    assert(b->ops->changes(b, ch, 4, &complete) == 4 && complete == 0);
    lost = 3;
    record("changed\t00000011\t9");
    {
        unsigned long before = surface_board_lost();

        assert(b->ops->changes(b, ch, 4, &complete) == 1 && complete == 0);
        assert(surface_board_lost() == before + 3u);
    }
    assert(b->ops->changes(b, ch, 4, &complete) == 0 && complete == 1);

    b->ops->close(b);
    puts("board: the manifest's nodes and the store's files as a device, values with "
         "secrets withheld, writes by policy and authority, changes from its own reader");
    return 0;
}
