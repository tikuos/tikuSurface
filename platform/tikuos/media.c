/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * media.c - name-based pagination and retained snapshot reads.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "media.h"
#include <string.h>

/** @brief Map firmware result classes into SDK resource status. */
static tiku_resource_error_t status(int r)
{
    if (r == SURFACE_MEDIA_OK) return TIKU_RESOURCE_OK;
    if (r == SURFACE_MEDIA_STALE) return TIKU_RESOURCE_STALE_CURSOR;
    if (r == SURFACE_MEDIA_INVALID) return TIKU_RESOURCE_INVALID;
    return TIKU_RESOURCE_UNAVAILABLE;
}
/** @brief Accept the fixed local principal and canonical bounded absolute paths. */
static int scope(const tiku_storage_request_t *r)
{
    if (strcmp(r->caller.principal, "surface-local") || r->path[0] != '/') return 0;
    size_t n = strlen(r->path);
    if (n > 64 || !n || (n > 1 && r->path[n - 1] == '/')) return 0;
    const char *p = r->path + 1;
    while (*p) {
        const char *end = strchr(p, '/');
        size_t length = end ? (size_t)(end - p) : strlen(p);
        if (!length || (length == 1 && p[0] == '.') || (length == 2 && p[0] == '.' && p[1] == '.')) return 0;
        p += length; if (*p) ++p;
    }
    return 1;
}
/** @brief Retain one request after validating caller, bounds and continuation identity. */
static tiku_resource_error_t submit(void *context, tiku_paged_storage_t *owner, const tiku_storage_request_t *r)
{
    surface_media_t *m = context;
    if (!scope(r)) return TIKU_RESOURCE_DENIED;
    if (m->request) return TIKU_RESOURCE_BUSY;
    if (!r->capacity || (r->operation == TIKU_STORAGE_LIST ? r->capacity > 16 : r->capacity > 1024))
        return TIKU_RESOURCE_LIMIT;
    unsigned position = 16;
    if (r->operation == TIKU_STORAGE_LIST && r->after) {
        for (unsigned i = 0; i < 16; ++i)
            if (r->epoch == m->positions[i].epoch && r->after == m->positions[i].after &&
                !strcmp(r->path, m->positions[i].path)) { position = i; break; }
        if (position == 16) return TIKU_RESOURCE_STALE_CURSOR;
    }
    m->io = (surface_media_io_t){.epoch = r->epoch};
    m->owner = owner; m->request = r;
    m->count = m->pending = m->cancelled = m->eof = 0;
    m->final_status = TIKU_RESOURCE_OK;
    if (r->operation == TIKU_STORAGE_READ) {
        if (!r->epoch || !r->node_id) { m->request = NULL; return TIKU_RESOURCE_INVALID; }
        m->phase = 1; m->io.operation = SURFACE_MEDIA_OPEN;
        memcpy(m->io.name, r->path + 1, strlen(r->path)); m->io.rank = r->node_id - 1;
    } else {
        m->phase = 0; m->io.operation = SURFACE_MEDIA_ENUMERATE;
        size_t n = strlen(r->path + 1);
        if (n >= 63) { m->request = NULL; return TIKU_RESOURCE_LIMIT; }
        memcpy(m->prefix, r->path + 1, n);
        if (n) m->prefix[n++] = '/';
        m->prefix[n] = 0;
        if (r->after) { memcpy(m->io.name, m->positions[position].name, 64); m->io.exclusive = m->positions[position].exclusive; }
        else memcpy(m->io.name, m->prefix, n + 1);
    }
    return TIKU_RESOURCE_OK;
}
/** @brief Request retirement while queued operations and reader snapshots remain retained. */
static void cancel(void *context, tiku_paged_storage_t *owner, uint64_t token)
{
    surface_media_t *m = context;
    if (m->request && m->owner == owner && m->request->token == token) m->cancelled = 1;
}

tiku_storage_provider_t surface_media_init(surface_media_t *m, void *bridge)
{
    static const tiku_storage_provider_ops_t ops = {submit, cancel};
    *m = (surface_media_t){.bridge = bridge};
    return (tiku_storage_provider_t){&ops, m,
        TIKU_RESOURCE_FEATURE_READ | TIKU_RESOURCE_FEATURE_COLLECTION | TIKU_RESOURCE_FEATURE_STREAM};
}
/** @brief Release the matching SDK view after queued work and snapshot closure. */
static void complete(surface_media_t *m)
{
    const tiku_storage_request_t *r = m->request;
    (void)tiku_paged_storage_complete(m->owner, r->token,
        m->cancelled ? TIKU_RESOURCE_CANCELLED : (tiku_resource_error_t)m->final_status,
        m->count, m->io.epoch, m->eof);
    m->request = NULL; m->owner = NULL;
}
/** @brief Convert one ranked object into a child and seek beyond a directory's descendants. */
static void entry(surface_media_t *m)
{
    const tiku_storage_request_t *r = m->request;
    size_t prefix = strlen(m->prefix);
    if (strncmp(m->io.found, m->prefix, prefix)) { m->eof = 1; complete(m); return; }
    const char *name = m->io.found + prefix;
    const char *slash = strchr(name, '/');
    size_t n = slash ? (size_t)(slash - m->io.found) : strlen(m->io.found);
    tiku_resource_entry_t *e = &r->entries[m->count++];
    memset(e, 0, sizeof *e);
    e->path[0] = '/'; memcpy(e->path + 1, m->io.found, n); e->path[n + 1] = 0;
    e->node_id = m->io.rank + 1;
    for (unsigned i = 0; i < 16; ++i) {
        e->id[i] = "0123456789abcdef"[(m->io.epoch >> ((15u - i) * 4u)) & 15u];
        e->id[16 + i] = "0123456789abcdef"[(e->node_id >> ((15u - i) * 4u)) & 15u];
    }
    e->kind = slash ? TIKU_RESOURCE_DIRECTORY : TIKU_RESOURCE_STREAM;
    memcpy(m->io.name, m->io.found, n); m->io.name[n] = 0;
    m->io.exclusive = !slash;
    if (slash) { m->io.name[n] = '0'; m->io.name[n + 1] = 0; }
    memcpy(m->resume, m->io.name, 64);
    memcpy(m->resume_path, r->path, strlen(r->path) + 1);
    m->resume_epoch = m->io.epoch; m->resume_after = e->node_id; m->exclusive = m->io.exclusive;
    if (m->count == r->capacity) {
        unsigned slot = 16;
        for (unsigned i = 0; i < 16; ++i)
            if (m->positions[i].epoch == m->resume_epoch && m->positions[i].after == m->resume_after &&
                !strcmp(m->positions[i].path, m->resume_path)) { slot = i; break; }
        if (slot == 16) slot = m->position_next++ % 16;
        memcpy(m->positions[slot].name, m->resume, 64);
        memcpy(m->positions[slot].path, m->resume_path, 65);
        m->positions[slot].epoch = m->resume_epoch;
        m->positions[slot].after = m->resume_after;
        m->positions[slot].exclusive = m->exclusive;
        complete(m);
    }
}

void surface_media_poll(surface_media_t *m)
{
    if (!m || !m->request) return;
    if (m->cancelled && !surface_os_media_cancel(m->bridge)) return;
    if (m->pending) {
        if (!surface_os_media_poll(m->bridge)) return;
        m->pending = 0;
        int result = m->io.result;
        if (m->phase == 3) {
            if (result == SURFACE_MEDIA_BUSY) return;
            if (result != SURFACE_MEDIA_OK && result != SURFACE_MEDIA_STALE) {
                m->final_status = status(result);
                return;
            }
            m->io.reader = 0; complete(m); return;
        }
        if (result == SURFACE_MEDIA_BUSY) return;
        if (result != SURFACE_MEDIA_OK) {
            if (result == SURFACE_MEDIA_END && m->phase == 0) m->eof = 1;
            else m->final_status = status(result);
            if (m->io.reader) { m->phase = 3; m->io.operation = SURFACE_MEDIA_CLOSE; }
            else complete(m);
            return;
        }
        if (m->phase == 0) {
            if (m->cancelled) complete(m);
            else entry(m);
            return;
        }
        if (m->phase == 1) {
            m->phase = 2; m->io.operation = SURFACE_MEDIA_READ;
            m->io.offset = m->request->offset; m->io.bytes = m->request->bytes;
            m->io.length = m->request->capacity;
        } else if (m->phase == 2) {
            m->count = m->io.count;
            m->eof = m->request->offset >= m->io.size || m->count >= m->io.size - m->request->offset;
            m->phase = 3; m->io.operation = SURFACE_MEDIA_CLOSE;
        }
        return;
    }
    if (m->cancelled && m->phase != 3) {
        if (!m->io.reader) { complete(m); return; }
        m->phase = 3; m->io.operation = SURFACE_MEDIA_CLOSE;
    }
    int result = surface_os_media_submit(m->bridge, &m->io);
    if (result == SURFACE_MEDIA_OK) m->pending = 1;
    else if (result == SURFACE_MEDIA_BUSY) (void)surface_os_media_poll(m->bridge);
    else {
        m->final_status = status(result);
        if (!m->io.reader) complete(m);
    }
}
