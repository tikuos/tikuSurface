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
    static const tiku_resource_error_t errors[]={TIKU_RESOURCE_OK,TIKU_RESOURCE_UNAVAILABLE,
        TIKU_RESOURCE_UNAVAILABLE,TIKU_RESOURCE_STALE_CURSOR,TIKU_RESOURCE_UNAVAILABLE,
        TIKU_RESOURCE_INVALID,TIKU_RESOURCE_UNSUPPORTED,TIKU_RESOURCE_LIMIT};
    return (unsigned)r<sizeof errors/sizeof errors[0]?errors[r]:TIKU_RESOURCE_UNAVAILABLE;
}
/** @brief Accept the fixed local principal and canonical bounded absolute paths. */
static int scope_path(const char *path)
{
    if (path[0] != '/') return 0;
    size_t n = strlen(path);
    if (n > 64 || !n || (n > 1 && path[n - 1] == '/')) return 0;
    const char *p = path + 1;
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
#if !SURFACE_CONTENT_JOBS && SURFACE_FILE_JOBS
    if(r->operation>TIKU_STORAGE_DELETE)return TIKU_RESOURCE_UNSUPPORTED;
#elif !SURFACE_FILE_JOBS
    if(r->operation>TIKU_STORAGE_READ)return TIKU_RESOURCE_UNSUPPORTED;
#endif
#if !SURFACE_DIRECTORIES && SURFACE_CONTENT_JOBS
    if(r->operation>TIKU_STORAGE_REPLACE)return TIKU_RESOURCE_UNSUPPORTED;
#endif
    if (strcmp(r->caller.principal,"surface-local") || !scope_path(r->path)) return TIKU_RESOURCE_DENIED;
#if SURFACE_FILE_JOBS
    if (r->operation >= TIKU_STORAGE_RENAME) {
#if SURFACE_DIRECTORIES
        if(r->operation==TIKU_STORAGE_DIRECTORY_UPGRADE){
            if(!r->caller.authenticated||!(r->caller.scopes&TIKU_RESOURCE_HOST_CONTROL)||
               !(r->caller.scopes&TIKU_RESOURCE_WRITE)||strcmp(r->path,"/"))return TIKU_RESOURCE_DENIED;
            if(m->request)return TIKU_RESOURCE_BUSY;
            if(!r->epoch)return TIKU_RESOURCE_INVALID;
            m->io=(surface_media_io_t){.operation=SURFACE_MEDIA_DIRECTORY_UPGRADE,.epoch=r->epoch};
            m->owner=owner;m->request=r;m->phase=5;m->count=m->pending=m->cancelled=m->eof=0;
            m->final_status=TIKU_RESOURCE_OK;return TIKU_RESOURCE_OK;
        }
#endif
        if (!r->caller.authenticated || !(r->caller.scopes & TIKU_RESOURCE_WRITE) || !r->path[1] ||
            ((r->operation == TIKU_STORAGE_RENAME
#if SURFACE_DIRECTORIES
             || r->operation == TIKU_STORAGE_TREE_RENAME || r->operation == TIKU_STORAGE_TREE_COPY
#endif
             ) && (!scope_path(r->destination) || !r->destination[1]))) return TIKU_RESOURCE_DENIED;
#if SURFACE_DIRECTORIES
        if(r->operation==TIKU_STORAGE_TREE_COPY){
            if(!(r->caller.scopes&TIKU_RESOURCE_READ))return TIKU_RESOURCE_DENIED;
            if(!r->bytes||r->capacity!=65536)return TIKU_RESOURCE_LIMIT;
        }
#endif
        if (m->request) return TIKU_RESOURCE_BUSY;
        if (!r->epoch) return TIKU_RESOURCE_INVALID;
#if SURFACE_DIRECTORIES
        if(r->operation==TIKU_STORAGE_MKDIR){
            m->io=(surface_media_io_t){.operation=SURFACE_MEDIA_MKDIR,.epoch=r->epoch};
            memcpy(m->io.name,r->path+1,strlen(r->path));m->owner=owner;m->request=r;m->phase=5;
            m->count=m->pending=m->cancelled=m->eof=0;m->final_status=TIKU_RESOURCE_OK;return TIKU_RESOURCE_OK;
        }
#endif
#if SURFACE_CONTENT_JOBS
        if(r->operation==TIKU_STORAGE_REPLACE) {
            if(r->input_bytes>65536 || (r->input_bytes&&!r->input))return TIKU_RESOURCE_LIMIT;
            if(!r->node_id) {
                m->io=(surface_media_io_t){.operation=SURFACE_MEDIA_REPLACE,.epoch=r->epoch,
                    .bytes=(unsigned char *)r->input,.length=r->input_bytes};
                memcpy(m->io.name,r->path+1,strlen(r->path));
                m->owner=owner;m->request=r;m->phase=5;m->count=m->pending=m->cancelled=m->eof=0;
                m->final_status=TIKU_RESOURCE_OK;return TIKU_RESOURCE_OK;
            }
        } else
#endif
        if (!r->node_id) return TIKU_RESOURCE_INVALID;
        m->io=(surface_media_io_t){.operation=SURFACE_MEDIA_ENUMERATE,.epoch=r->epoch};
        memcpy(m->io.name,r->path+1,strlen(r->path));
        m->owner=owner;m->request=r;m->phase=4;m->count=m->pending=m->cancelled=m->eof=0;
        m->final_status=TIKU_RESOURCE_OK;return TIKU_RESOURCE_OK;
    }
#endif
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
#if SURFACE_DIRECTORIES
        if(n==63){memcpy(m->io.name,r->path+1,64);m->phase=6;return TIKU_RESOURCE_OK;}
#endif
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
        TIKU_RESOURCE_FEATURE_READ | TIKU_RESOURCE_FEATURE_COLLECTION | TIKU_RESOURCE_FEATURE_STREAM
#if SURFACE_FILE_JOBS
        | TIKU_RESOURCE_FEATURE_CHANGE
#endif
    };
}
/** @brief Release the matching SDK view after queued work and snapshot closure. */
static void complete(surface_media_t *m)
{
    const tiku_storage_request_t *r = m->request;
#if SURFACE_FILE_INFO
    if(r->operation==TIKU_STORAGE_READ&&!m->cancelled&&m->final_status==TIKU_RESOURCE_OK)
        (void)tiku_paged_storage_describe(m->owner,r->token,m->io.size,m->io.id);
#endif
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
    int directory=slash!=NULL;
#if SURFACE_DIRECTORIES
    directory|=m->io.directory!=0;
#endif
    size_t n = slash ? (size_t)(slash - m->io.found) : strlen(m->io.found);
    tiku_resource_entry_t *e = &r->entries[m->count++];
    memset(e, 0, sizeof *e);
    e->path[0] = '/'; memcpy(e->path + 1, m->io.found, n); e->path[n + 1] = 0;
    e->node_id = m->io.rank + 1;
    for (unsigned i = 0; i < 16; ++i) {
        e->id[i] = "0123456789abcdef"[(m->io.epoch >> ((15u - i) * 4u)) & 15u];
        e->id[16 + i] = "0123456789abcdef"[(e->node_id >> ((15u - i) * 4u)) & 15u];
    }
    e->kind = directory ? TIKU_RESOURCE_DIRECTORY : TIKU_RESOURCE_STREAM;
    memcpy(m->io.name, m->io.found, n); m->io.name[n] = 0;
    m->io.exclusive = !directory;
    if (directory&&n+1<sizeof m->io.name) { m->io.name[n] = '0'; m->io.name[n + 1] = 0; }
    else if(directory)m->io.exclusive=1;
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
        int retired=surface_os_media_poll(m->bridge);
#if SURFACE_DIRECTORIES
        if(m->request->operation==TIKU_STORAGE_TREE_COPY)
            (void)tiku_paged_storage_set_progress(m->owner,m->request->token,m->io.prepared,m->io.total);
#endif
        if (!retired) return;
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
        #if SURFACE_FILE_JOBS
        if (m->phase == 5) {
            m->final_status=result==SURFACE_MEDIA_BUSY?TIKU_RESOURCE_BUSY:status(result);
#if SURFACE_CONTENT_JOBS
            if(result==SURFACE_MEDIA_OK && m->request->operation==TIKU_STORAGE_REPLACE)m->count=m->request->input_bytes;
#endif
            m->eof=1;complete(m);return;
        }
#endif
        if (result == SURFACE_MEDIA_BUSY) return;
        if (result != SURFACE_MEDIA_OK) {
            if (result == SURFACE_MEDIA_END && m->phase == 0) m->eof = 1;
            else m->final_status = status(result);
            if (m->io.reader) { m->phase = 3; m->io.operation = SURFACE_MEDIA_CLOSE; }
            else complete(m);
            return;
        }
        #if SURFACE_FILE_JOBS
        if (m->phase == 4) {
#if SURFACE_DIRECTORIES
            if(m->request->operation==TIKU_STORAGE_TREE_RENAME||m->request->operation==TIKU_STORAGE_TREE_DELETE||m->request->operation==TIKU_STORAGE_TREE_COPY){
                const char *root=m->request->path+1;size_t n=strlen(root);
                int exact=!strcmp(m->io.found,root),child=!strncmp(m->io.found,root,n)&&m->io.found[n]=='/';
                if(!exact&&!child&&n<63&&strlen(m->io.name)==n){
                    memcpy(m->io.name,root,n);m->io.name[n++]='/';m->io.name[n]=0;return;
                }
                if(m->io.rank!=m->request->node_id-1||(!exact&&!child)||(exact&&!m->io.directory)){
                    m->final_status=TIKU_RESOURCE_STALE_CURSOR;complete(m);return;
                }
                if(!exact)m->io.id=0;
                memcpy(m->io.name,root,n+1);m->phase=5;
                m->io.operation=m->request->operation==TIKU_STORAGE_TREE_RENAME?SURFACE_MEDIA_TREE_RENAME:SURFACE_MEDIA_TREE_DELETE;
                if(m->request->operation==TIKU_STORAGE_TREE_COPY){m->io.operation=SURFACE_MEDIA_TREE_COPY;
                    m->io.bytes=m->request->bytes;m->io.length=m->request->capacity;}
                if(m->io.operation==SURFACE_MEDIA_TREE_RENAME||m->io.operation==SURFACE_MEDIA_TREE_COPY)memcpy(m->io.destination,m->request->destination+1,strlen(m->request->destination));
                return;
            }
#endif
            if (m->io.rank!=m->request->node_id-1 || strcmp(m->io.found,m->request->path+1)) {
                m->final_status=TIKU_RESOURCE_STALE_CURSOR;complete(m);return;
            }
            m->phase=5;m->io.operation=m->request->operation==TIKU_STORAGE_RENAME?SURFACE_MEDIA_RENAME:SURFACE_MEDIA_DELETE;
#if SURFACE_CONTENT_JOBS
            if(m->request->operation==TIKU_STORAGE_REPLACE) {
                m->io.operation=SURFACE_MEDIA_REPLACE;m->io.bytes=(unsigned char *)m->request->input;
                m->io.length=m->request->input_bytes;
            }
#endif
            if (m->io.operation==SURFACE_MEDIA_RENAME)
                memcpy(m->io.destination,m->request->destination+1,strlen(m->request->destination));
            return;
        }
#endif
#if SURFACE_DIRECTORIES
        if(m->phase==6){m->final_status=m->io.directory&&!strcmp(m->io.found,m->request->path+1)?TIKU_RESOURCE_OK:TIKU_RESOURCE_INVALID;m->eof=1;complete(m);return;}
#endif
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
