/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * board.c - the board's own namespace as a Tracker device.
 *
 * Stomme's backend calls answered from the kernel's VFS through TikuOS's
 * compat/posix: the manifest (/sys/vfs/manifest) names the static nodes
 * with their descriptors, capabilities and identities; /data's files come
 * from the file store.  A descriptor on /sys/vfs/events is this backend's
 * own reader of the kernel's change ring, so the console's reader keeps
 * every record.  A write is checked against the policy below before the
 * kernel checks the library's authority.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "board.h"
#include "tiku_model.h"
#include "tiku_state.h"
#include "tiku_posix.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BOARD_PATH_MAX 64u        /* the kernel's TIKU_VFS_PATH_MAX */
#define BOARD_VALUE_MAX 512u      /* a value one read or write carries */
#define BOARD_STORE "/data"
#define BOARD_ROOT_ID 1u          /* the device root's id; no node's address */

typedef struct {
    char path[BOARD_PATH_MAX];
    char meta[TIKU_META_MAX];
    char cap[24];
    uint32_t id;
    unsigned char dir, perm;      /* perm: TIKU_P_READ, TIKU_P_WRITE */
} board_node_t;

typedef struct {
    pthread_mutex_t lock;          /* the table, the reader and the store */
    board_node_t *nodes;
    int count;
    int events;                    /* the change reader, a library descriptor */
    unsigned long lost;            /* its lost count at the last ask */
    char name[64];
    struct tiku_store *store;
} board_t;

/* The settings the desktop changes: each is a reviewed value with a typed
 * descriptor the editor checks.  Other kernel nodes are read-only here. */
static const char *const settings[] = {
    "/sys/device/name", "/sys/cpu/freq_target", "/sys/crypto/mode",
    "/sys/radio/txpower", "/sys/time",
};

/** @brief Wi-Fi operations exposed by the local board device. */
static int wifi_control(const char *path)
{
    static const char prefix[] = "/sys/net/wifi/profiles/";
    if (!strcmp(path, "/sys/net/wifi/scan") ||
        !strcmp(path, "/sys/net/wifi/disconnect")) return 1;
    if (strncmp(path, prefix, sizeof prefix - 1) != 0) return 0;
    const char *row = path + sizeof prefix - 1;
    return (row[0] == '0' || row[0] == '1') &&
           (!strcmp(row + 1, "/ssid") || !strcmp(row + 1, "/auth") ||
            !strcmp(row + 1, "/connect"));
}

/*---------------------------------------------------------------------------*/
/* THE KERNEL'S FILES                                                        */
/*---------------------------------------------------------------------------*/

/** @brief Read the whole of kernel file @p path into a new buffer.
 *  @return its length, or -1 with errno; *@p out is NUL-terminated */
static long slurp(const char *path, char **out)
{
    size_t cap = 1024u, len = 0u;
    char *buf = malloc(cap);
    int fd = tiku_posix_open(path, O_RDONLY);

    if (buf == NULL || fd < 0) {
        int e = buf == NULL ? ENOMEM : errno;
        free(buf);
        if (fd >= 0) (void)tiku_posix_close(fd);
        errno = e;
        return -1;
    }
    for (;;) {
        ssize_t r;

        if (cap - len < 257u) {
            char *grown = realloc(buf, cap * 2u);
            if (grown == NULL) {
                free(buf);
                (void)tiku_posix_close(fd);
                errno = ENOMEM;
                return -1;
            }
            buf = grown;
            cap *= 2u;
        }
        r = tiku_posix_read(fd, buf + len, cap - len - 1u);
        if (r < 0) {
            int e = errno;
            free(buf);
            (void)tiku_posix_close(fd);
            errno = e;
            return -1;
        }
        if (r == 0) break;
        len += (size_t)r;
    }
    (void)tiku_posix_close(fd);
    buf[len] = '\0';
    *out = buf;
    return (long)len;
}

/** @brief Copy the tab- or line-ended field at @p p into @p out.
 *  @return the text after the field */
static const char *field(const char *p, char *out, size_t max)
{
    size_t n = 0u;

    while (*p != '\0' && *p != '\t' && *p != '\n') {
        if (n + 1u < max) out[n++] = *p;
        p++;
    }
    out[n] = '\0';
    return *p == '\t' ? p + 1 : p;
}

/** @brief Read the manifest into the node table.  @return 0, or -1 */
static int board_load(board_t *b)
{
    char *text;
    const char *p;
    board_node_t *nodes = NULL;
    int count = 0, cap = 0;

    if (slurp("/sys/vfs/manifest", &text) < 0) return -1;
    for (p = text; *p != '\0';) {
        board_node_t n;
        char kind[4], perm[4], id[12];

        if (*p == '#' || *p == '\n') {
            while (*p != '\0' && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }
        memset(&n, 0, sizeof n);
        p = field(p, n.path, sizeof n.path);
        p = field(p, kind, sizeof kind);
        p = field(p, perm, sizeof perm);
        p = field(p, n.meta, sizeof n.meta);
        p = field(p, n.cap, sizeof n.cap);
        p = field(p, id, sizeof id);
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++;
        if (n.cap[0] == '\0' || id[0] == '\0') continue;   /* a cut line */
        n.dir = kind[0] == 'd';
        n.perm = (unsigned char)((perm[0] == 'r' ? TIKU_P_READ : 0u) |
                                 (perm[1] == 'w' ? TIKU_P_WRITE : 0u));
        n.id = (uint32_t)strtoul(id, NULL, 16);
        if (count == cap) {
            board_node_t *grown = realloc(nodes, (size_t)(cap ? cap * 2 : 64) * sizeof *nodes);
            if (grown == NULL) {
                free(nodes);
                free(text);
                return -1;
            }
            nodes = grown;
            cap = cap ? cap * 2 : 64;
        }
        nodes[count++] = n;
    }
    free(text);
    free(b->nodes);
    b->nodes = nodes;
    b->count = count;
    return 0;
}

static const board_node_t *board_find(const board_t *b, const char *path)
{
    for (int i = 0; i < b->count; i++) {
        if (strcmp(b->nodes[i].path, path) == 0) return &b->nodes[i];
    }
    return NULL;
}

/** @brief Whether @p path is the file store or a file in it. */
static int in_store(const char *path)
{
    size_t n = strlen(BOARD_STORE);

    return strncmp(path, BOARD_STORE, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

/** @brief The last component of @p path; "" for the root. */
static const char *leaf(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash != NULL ? slash + 1 : path;
}

/*---------------------------------------------------------------------------*/
/* MODELS                                                                    */
/*---------------------------------------------------------------------------*/

static void model_named(tiku_backend_t *be, const char *path, tiku_model_t *m)
{
    memset(m, 0, sizeof *m);
    snprintf(m->path, sizeof m->path, "%s", path);
    snprintf(m->name, sizeof m->name, "%s", leaf(path));
    m->backend = be;
}

/** @brief The model of manifest node @p n: a store folder or file, a
 *  namespace folder or a live value, with its descriptor and capability. */
static void model_of_node(tiku_backend_t *be, const board_node_t *n, tiku_model_t *m)
{
    model_named(be, n->path, m);
    if (in_store(n->path)) {
        m->kind = n->dir ? TIKU_KIND_DIRECTORY : TIKU_KIND_FILE;
    } else {
        m->kind = n->dir ? TIKU_KIND_VIRTUAL_DIR : TIKU_KIND_DEVICE_NODE;
    }
    m->facts.perm = n->perm;
    if (!strncmp(n->path, "/sys/net/wifi/", 14) && !wifi_control(n->path))
        m->facts.perm &= ~TIKU_P_WRITE;
    if (m->kind == TIKU_KIND_DIRECTORY) m->facts.perm |= TIKU_P_READ | TIKU_P_WRITE;
    else if (m->kind == TIKU_KIND_VIRTUAL_DIR) m->facts.perm |= TIKU_P_READ;
    snprintf(m->facts.req_cap, sizeof m->facts.req_cap, "%s", n->cap);
    snprintf(m->facts.meta, sizeof m->facts.meta, "%s", n->meta);
    m->facts.cap_known = 1;
    snprintf(m->type, sizeof m->type, "%s",
             m->kind == TIKU_KIND_FILE ? "tiku-file/data"
             : m->kind == TIKU_KIND_DIRECTORY ? "tiku-dir/store"
             : n->dir ? "tiku-dir/namespace" : "tiku-node/value");
    m->node_id = n->id;
}

/** @brief The model of file @p path in the store, from the store's stat.
 *  @return 0, or -1 */
static int model_of_stored(tiku_backend_t *be, const char *path, tiku_model_t *m)
{
    struct stat st;

    if (tiku_posix_stat(path, &st) != 0) return -1;
    model_named(be, path, m);
    m->kind = S_ISDIR(st.st_mode) ? TIKU_KIND_DIRECTORY : TIKU_KIND_FILE;
    m->facts.perm = TIKU_P_READ | TIKU_P_WRITE;
    snprintf(m->facts.req_cap, sizeof m->facts.req_cap, "fs");
    snprintf(m->facts.meta, sizeof m->facts.meta, "-");
    m->facts.cap_known = 1;
    m->facts.size = (int64_t)st.st_size;
    m->facts.mtime = (int64_t)st.st_mtime;
    snprintf(m->type, sizeof m->type, "%s",
             m->kind == TIKU_KIND_FILE ? "tiku-file/data" : "tiku-dir/store");
    m->node_id = (uint64_t)st.st_ino;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* THE BACKEND                                                               */
/*---------------------------------------------------------------------------*/

static int form_path(const char *path)
{
    return strcmp(path, "/gui/demo") == 0 || strncmp(path, "/gui/demo/", 10) == 0;
}

static int bd_stat(tiku_backend_t *be, const char *path, tiku_model_t *out)
{
    board_t *b = be->impl;
    const board_node_t *n;
    int r = 0;

    if (strcmp(path, "/") == 0) {
        model_named(be, "/", out);
        snprintf(out->name, sizeof out->name, "%s", b->name);
        out->kind = TIKU_KIND_VOLUME;
        out->facts.perm = TIKU_P_READ;
        snprintf(out->facts.req_cap, sizeof out->facts.req_cap, "-");
        snprintf(out->facts.meta, sizeof out->facts.meta, "-");
        out->facts.cap_known = 1;
        snprintf(out->type, sizeof out->type, "tiku-dir/device");
        out->node_id = BOARD_ROOT_ID;
        return 0;
    }
    pthread_mutex_lock(&b->lock);
    n = board_find(b, path);
    if (n != NULL) model_of_node(be, n, out);
    pthread_mutex_unlock(&b->lock);
    if (n == NULL) {
        r = in_store(path) || form_path(path) ? model_of_stored(be, path, out) : -1;
        if (r == 0 && form_path(path)) {
            int access = surface_os_form_access(path);
            if (access < 0) return -1;
            int dir = out->kind == TIKU_KIND_DIRECTORY;
            out->kind = dir ? TIKU_KIND_VIRTUAL_DIR : TIKU_KIND_DEVICE_NODE;
            out->facts.perm = TIKU_P_READ | (access > 0 ? TIKU_P_WRITE : 0);
            snprintf(out->type, sizeof out->type, "%s", dir ? "tiku-dir/namespace" : "tiku-node/value");
        }
    }
    return r;
}

static int bd_list(tiku_backend_t *be, const char *path, tiku_entry_fn fn, void *ctx)
{
    board_t *b = be->impl;
    size_t len = strcmp(path, "/") == 0 ? 0u : strlen(path);
    tiku_model_t *m = malloc(sizeof *m);
    int count = 0, stop = 0;

    if (m == NULL) return -1;
    pthread_mutex_lock(&b->lock);
    for (int i = 0; i < b->count && !stop; i++) {
        const char *p = b->nodes[i].path;

        if (strncmp(p, path, len) != 0 || p[len] != '/' || strchr(p + len + 1, '/') != NULL) {
            continue;
        }
        model_of_node(be, &b->nodes[i], m);
        count++;
        stop = fn != NULL && fn(m, ctx) != 0;
    }
    pthread_mutex_unlock(&b->lock);
    if (!stop && (strcmp(path, BOARD_STORE) == 0 || strcmp(path, "/gui") == 0 ||
                  strcmp(path, "/gui/demo") == 0)) {
        DIR *d = tiku_posix_opendir(path);
        struct dirent *e;

        while (d != NULL && !stop && (e = tiku_posix_readdir(d)) != NULL) {
            char child[BOARD_PATH_MAX];

            if (e->d_name[0] == '.') continue;
            if ((size_t)snprintf(child, sizeof child, "%s/%s", path, e->d_name) >=
                sizeof child) {
                continue;
            }
            if (bd_stat(be, child, m) != 0) continue;
            count++;
            stop = fn != NULL && fn(m, ctx) != 0;
        }
        if (d != NULL) (void)tiku_posix_closedir(d);
    }
    free(m);
    return count;
}

/** @brief Whether the descriptor @p meta marks a secret. */
static int secret(const char *meta)
{
    return strstr(meta, ";secret") != NULL;
}

static int bd_read(tiku_backend_t *be, const char *path, void *buf, size_t max)
{
    board_t *b = be->impl;
    const board_node_t *n;
    int fd, hidden;
    size_t got = 0u;

    pthread_mutex_lock(&b->lock);
    n = board_find(b, path);
    hidden = n != NULL && secret(n->meta);
    pthread_mutex_unlock(&b->lock);
    if (hidden) {
        errno = EACCES;
        return -1;
    }
    fd = tiku_posix_open(path, O_RDONLY);
    if (fd < 0) return -1;
    while (got < max) {
        ssize_t r = tiku_posix_read(fd, (char *)buf + got, max - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    (void)tiku_posix_close(fd);
    return (int)got;
}

/** @brief Whether the desktop changes @p path: a file in the store, or one
 *  of the reviewed settings. */
static int changeable(board_t *board, const char *path)
{
    if (wifi_control(path)) {
        pthread_mutex_lock(&board->lock);
        const board_node_t *node = board_find(board, path);
        int allowed = node && (node->perm & TIKU_P_WRITE) && !secret(node->meta);
        pthread_mutex_unlock(&board->lock);
        return allowed;
    }
    if (in_store(path) && strcmp(path, BOARD_STORE) != 0) return 1;
    for (unsigned i = 0; i < sizeof settings / sizeof settings[0]; i++) {
        if (strcmp(path, settings[i]) == 0) return 1;
    }
    return 0;
}

/** @brief Write @p len bytes of @p buf to @p path: a store file is replaced
 *  whole and published at the close; a setting takes the value in one write.
 *  @return 0, or -1 with @p err filled */
static int bd_write(tiku_backend_t *be, const char *path, const void *buf, size_t len,
                    char *err, size_t errmax)
{
    int store = in_store(path), fd, r = 0, e = 0;
    size_t put = 0u;

    if (form_path(path)) {
        int result = surface_os_form_write(path, buf, len);
        if (result < 0) snprintf(err, errmax, "The form refused this value.");
        return result;
    }
    if (!changeable(be->impl, path)) {
        snprintf(err, errmax, "%s is not changed from the desktop.", path);
        return -1;
    }
    if (len > BOARD_VALUE_MAX) {
        snprintf(err, errmax, "A value holds at most %u bytes.", BOARD_VALUE_MAX);
        return -1;
    }
    if (len == 0u && !store) {
        snprintf(err, errmax, "A setting needs a value.");
        return -1;
    }
    fd = tiku_posix_open(path, store ? (O_WRONLY | O_CREAT | O_TRUNC) : O_WRONLY);
    if (fd < 0) {
        snprintf(err, errmax, "%s", strerror(errno));
        return -1;
    }
    while (put < len) {
        ssize_t w = tiku_posix_write(fd, (const char *)buf + put, len - put);
        if (w <= 0) {
            e = w < 0 ? errno : EIO;
            r = -1;
            break;
        }
        put += (size_t)w;
        if (!store && put < len) {
            e = EIO;               /* a setting's value is one write */
            r = -1;
            break;
        }
    }
    if (tiku_posix_close(fd) != 0 && r == 0) {
        e = errno;
        r = -1;
    }
    if (r != 0) {
        snprintf(err, errmax, "%s",
                 e == EPERM ? "The desktop does not have the authority to change this."
                            : strerror(e));
    }
    return r;
}

static int bd_pump(tiku_backend_t *be, int timeout_ms)
{
    (void)be;
    (void)timeout_ms;
    return 0;
}

/** @brief The folder a record for node @p id changes: a folder's own id, a
 *  file's folder's, and the root's for a node the manifest does not name. */
static uint64_t folder_of(const board_t *b, uint32_t id)
{
    char parent[BOARD_PATH_MAX];
    const board_node_t *node = NULL, *dir;
    char *slash;

    for (int i = 0; i < b->count && node == NULL; i++) {
        if (b->nodes[i].id == id) node = &b->nodes[i];
    }
    if (node == NULL) return BOARD_ROOT_ID;
    if (node->dir) return node->id;
    snprintf(parent, sizeof parent, "%s", node->path);
    slash = strrchr(parent, '/');
    if (slash == NULL || slash == parent) return BOARD_ROOT_ID;
    *slash = '\0';
    dir = board_find(b, parent);
    return dir != NULL ? dir->id : BOARD_ROOT_ID;
}

/* Records the desktop's readers of the change ring lost, for the status line. */
static volatile unsigned long board_lost;

unsigned long surface_board_lost(void) { return board_lost; }

/** @brief The records since the last ask, from this backend's own reader of
 *  the kernel's change ring, each a change under the folder it touches, so
 *  only views at or below that folder re-list; a record other than
 *  "changed" reloads the manifest. */
static int bd_changes(tiku_backend_t *be, tiku_model_change_t *out, int max, int *complete)
{
    board_t *b = be->impl;
    char text[256];
    int w = 0, reload = 0, more = 1;

    if (complete != NULL) *complete = 1;
    pthread_mutex_lock(&b->lock);
    if (b->events < 0) b->events = tiku_posix_open("/sys/vfs/events", O_RDONLY);
    while (b->events >= 0 && more) {
        ssize_t r = tiku_posix_read(b->events, text, sizeof text - 1u);
        const char *p = text;

        more = 0;
        if (r <= 0) break;
        text[r] = '\0';
        while (*p != '\0') {
            char op[12];
            unsigned long id, n, lost;

            if (sscanf(p, "# %lu read, %lu lost", &n, &lost) == 2) {
                if (lost != b->lost) {
                    if (lost > b->lost) board_lost += lost - b->lost;
                    b->lost = lost;
                    if (complete != NULL) *complete = 0;
                }
                more = n > 0u;
            } else if (sscanf(p, "%11s %lx", op, &id) == 2) {
                if (strcmp(op, "changed") != 0) reload = 1;
                if (w < max) {
                    out[w].id = folder_of(b, (uint32_t)id);
                    out[w].op = TIKU_CH_SUBTREE;
                    w++;
                } else if (complete != NULL) {
                    *complete = 0;
                }
            }
            while (*p != '\0' && *p != '\n') p++;
            if (*p == '\n') p++;
        }
    }
    if (reload) (void)board_load(b);
    pthread_mutex_unlock(&b->lock);
    return b->events < 0 ? -1 : w;
}

static int bd_path_of_id(tiku_backend_t *be, uint64_t id, char *out, size_t max)
{
    board_t *b = be->impl;
    int r = -1;

    if (id == BOARD_ROOT_ID) {
        snprintf(out, max, "/");
        return 0;
    }
    pthread_mutex_lock(&b->lock);
    for (int i = 0; i < b->count && r != 0; i++) {
        if (b->nodes[i].id == (uint32_t)id) {
            snprintf(out, max, "%s", b->nodes[i].path);
            r = 0;
        }
    }
    pthread_mutex_unlock(&b->lock);
    return r;
}

static struct tiku_store *bd_store(tiku_backend_t *be)
{
    board_t *b = be->impl;

    if (b->store == NULL) b->store = tiku_store_sidecar_open(be->devid);
    return b->store;
}

static void board_free(board_t *b)
{
    if (b == NULL) return;
    if (b->events >= 0) (void)tiku_posix_close(b->events);
    tiku_store_free(b->store);
    free(b->nodes);
    pthread_mutex_destroy(&b->lock);
    free(b);
}

static void bd_close(tiku_backend_t *be)
{
    board_free(be->impl);
    free(be);
}

static const tiku_backend_ops_t BOARD_OPS = {
    "tikuos-local", bd_stat, bd_list, bd_read, bd_write, bd_pump,
    bd_changes, bd_path_of_id, NULL, bd_store, bd_close, NULL};

tiku_backend_t *surface_board_open(char *name, size_t max)
{
    tiku_backend_t *be = calloc(1, sizeof *be);
    board_t *b = calloc(1, sizeof *b);
    char *text = NULL;

    if (be == NULL || b == NULL) {
        free(be);
        free(b);
        return NULL;
    }
    pthread_mutex_init(&b->lock, NULL);
    b->events = tiku_posix_open("/sys/vfs/events", O_RDONLY);
    if (board_load(b) != 0) {
        board_free(b);
        free(be);
        return NULL;
    }
    snprintf(b->name, sizeof b->name, "board");
    if (slurp("/sys/device/name", &text) > 0) {
        text[strcspn(text, "\r\n")] = '\0';
        if (text[0] != '\0' && strchr(text, '/') == NULL) {
            snprintf(b->name, sizeof b->name, "%s", text);
        }
    }
    free(text);
    be->ops = &BOARD_OPS;
    be->impl = b;
    snprintf(be->devid, sizeof be->devid, "board");
    snprintf(name, max, "%s", b->name);
    return be;
}

/** @brief Count the entries a listing passes, noting one named @p ctx. */
static int selftest_entry(const tiku_model_t *m, void *ctx)
{
    struct { const char *want; int found, count; uint64_t ids[16]; int distinct; } *t = ctx;

    if (strcmp(leaf(m->path), t->want) == 0) t->found = 1;
    for (int i = 0; i < t->count && i < 16; i++) {
        if (t->ids[i] == m->node_id) t->distinct = 0;
    }
    if (t->count < 16) t->ids[t->count] = m->node_id;
    t->count++;
    return 0;
}

void surface_board_selftest(void)
{
    struct { const char *want; int found, count; uint64_t ids[16]; int distinct; } t;
    char name[64], text[24] = "", err[96];
    struct timespec t0, t1;
    unsigned long value;
    tiku_backend_t *be = surface_board_open(name, sizeof name);
    int n, ok;

    fprintf(stderr, "board: open=%d name=%s\n", be != NULL, be != NULL ? name : "-");
    if (be == NULL) return;
    (void)clock_gettime(CLOCK_MONOTONIC, &t0);
    n = be->ops->read(be, "/sys/uptime", text, sizeof text - 1u);
    (void)clock_gettime(CLOCK_MONOTONIC, &t1);
    if (n > 0) text[n] = '\0';
    value = n > 0 ? strtoul(text, NULL, 10) : 0ul;
    ok = n > 0 && value + 1u >= (unsigned long)t0.tv_sec && value <= (unsigned long)t1.tv_sec + 1u;
    fprintf(stderr, "board: uptime=%d value=%lu mono=%lu-%lu\n", ok, value,
            (unsigned long)t0.tv_sec, (unsigned long)t1.tv_sec);
    memset(&t, 0, sizeof t);
    t.want = "uptime";
    t.distinct = 1;
    fprintf(stderr, "board: list-sys=%d\n", be->ops->list(be, "/sys", selftest_entry, &t) > 0 && t.found);
    memset(&t, 0, sizeof t);
    t.want = "sys";
    t.distinct = 1;
    ok = be->ops->list(be, "/", selftest_entry, &t) > 1 && t.found && t.distinct;
    fprintf(stderr, "board: root=%d entries=%d\n", ok, t.count);
    ok = be->ops->write(be, "/sys/uptime", "1", 1u, err, sizeof err) == -1;
    fprintf(stderr, "board: refused=%d\n", ok);
    be->ops->close(be);
}
