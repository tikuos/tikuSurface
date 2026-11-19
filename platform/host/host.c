/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * host.c - tracked allocations, fake time and serialized fixture completions.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

volatile int surface_host_allocating;

/** @brief Allocate with failure injection and record the exact release contract. */
static void *allocate(void *context, size_t bytes, size_t alignment)
{
    surface_host_memory_t *memory = context;
    ++memory->calls;
    if (memory->fail_at == memory->calls || alignment > _Alignof(max_align_t)) return NULL;
    unsigned slot;
    for (slot = 0; slot < 64 && memory->records[slot].pointer; ++slot) { }
    if (slot == 64) return NULL;
    surface_host_allocating = 1;
    void *pointer = malloc(bytes);
    surface_host_allocating = 0;
    if (!pointer) return NULL;
    memory->records[slot].pointer = pointer;
    memory->records[slot].bytes = bytes;
    memory->records[slot].alignment = alignment;
    ++memory->live;
    memory->bytes += bytes;
    if (memory->bytes > memory->peak) memory->peak = memory->bytes;
    return pointer;
}

/** @brief Check provider ownership, byte count and alignment before release. */
static void release(void *context, void *pointer, size_t bytes, size_t alignment)
{
    surface_host_memory_t *memory = context;
    unsigned slot;
    for (slot = 0; slot < 64 && memory->records[slot].pointer != pointer; ++slot) { }
    assert(slot < 64 && pointer);
    assert(memory->records[slot].bytes == bytes && memory->records[slot].alignment == alignment);
    memory->records[slot].pointer = NULL;
    --memory->live;
    memory->bytes -= bytes;
    surface_host_allocating = 1;
    free(pointer);
    surface_host_allocating = 0;
}

/** @brief Return caller-controlled microseconds or an injected clock failure. */
static int clock_read(void *context, uint64_t *now)
{
    surface_host_t *host = context;
    if (host->clock_error) return 0;
    *now = host->now;
    return 1;
}

/** @brief Retain one immutable request for a later owner-thread completion. */
static tiku_resource_error_t submit(void *context, tiku_paged_storage_t *owner,
                                     const tiku_storage_request_t *request)
{
    surface_host_t *host = context;
    if (host->unavailable) return TIKU_RESOURCE_UNAVAILABLE;
    if (host->request.owner) return TIKU_RESOURCE_BUSY;
    if (strcmp(request->caller.principal, "surface-host") || !request->caller.authenticated ||
        !(request->caller.scopes & TIKU_RESOURCE_READ) ||
        (request->operation == TIKU_STORAGE_READ && !(request->caller.scopes & TIKU_RESOURCE_CONTENT)))
        return TIKU_RESOURCE_DENIED;
    host->request.owner = owner;
    host->request.request = request;
    host->request.due = host->now + host->storage_delay;
    host->request.cancelled = 0;
    return TIKU_RESOURCE_OK;
}

/** @brief Mark cancellation without releasing the provider-owned request. */
static void cancel(void *context, tiku_paged_storage_t *owner, uint64_t token)
{
    surface_host_t *host = context;
    assert(host->request.owner == owner && host->request.request->token == token);
    host->request.cancelled = 1;
    ++host->cancellations;
}

/** @brief Retain pixels until a later poll; reject a second outstanding frame. */
static tiku_present_result_t present(void *context, const tiku_present_view_t *view)
{
    surface_host_t *host = context;
    if (host->present_error) return TIKU_PRESENT_ERROR;
    if (host->present_busy || host->frame.owner) return TIKU_PRESENT_BUSY;
    host->frame = *view;
    host->frame_due = host->now + host->present_delay;
    ++host->submissions;
    return TIKU_PRESENT_OK;
}

surface_config_t surface_host_config(surface_host_t *host)
{
    static const tiku_allocator_ops_t allocator = {allocate, release};
    static const tiku_storage_provider_ops_t storage = {submit, cancel};
    *host = (surface_host_t){.storage_delay = 20000, .present_delay = 16000, .epoch = 1};
    surface_config_t config = {
        .platform_name = "TikuSurface host preview",
        .media_description = "Media: deterministic read-only fixture",
        .services_description = "Wi-Fi, touch calibration and persistent settings: unavailable on this host",
        .allocator = {&allocator, &host->memory}, .clock = {clock_read, host, 0},
        .storage = {&storage, host, TIKU_RESOURCE_FEATURE_READ | TIKU_RESOURCE_FEATURE_COLLECTION | TIKU_RESOURCE_FEATURE_STREAM}
    };
    memcpy(config.caller.principal, "surface-host", sizeof "surface-host");
    config.caller.scopes = TIKU_RESOURCE_READ | TIKU_RESOURCE_CONTENT;
    config.caller.authenticated = 1;
    return config;
}

tiku_presenter_t surface_host_presenter(surface_host_t *host)
{ return (tiku_presenter_t){present, host}; }

/** @brief Enumerate fixed byte-ordered fixture entries and stable epoch IDs. */
static size_t entries(const tiku_storage_request_t *request, int *eof)
{
    size_t count = 0;
    unsigned total = !strcmp(request->path, "/") ? 22 : !strcmp(request->path, "/docs") ? 2 : 0;
    unsigned start = !strcmp(request->path, "/docs") ? 100 : 0;
    for (unsigned i = 1; i <= total; ++i) {
        uint64_t id = start + i;
        if (id <= request->after) continue;
        if (count == request->capacity) { *eof = 0; return count; }
        tiku_resource_entry_t *entry = &request->entries[count++];
        *entry = (tiku_resource_entry_t){.node_id = id, .kind = TIKU_RESOURCE_STREAM};
        if (!start && i == 1) {
            memcpy(entry->path, "/docs", 6);
            entry->kind = TIKU_RESOURCE_DIRECTORY;
        } else if (start) snprintf(entry->path, sizeof entry->path, "/docs/%s", i == 1 ? "guide.txt" : "notes.txt");
        else snprintf(entry->path, sizeof entry->path, "/sample-%02u.txt", i - 1);
        snprintf(entry->id, sizeof entry->id, "fixture-%u", (unsigned)id);
    }
    *eof = 1;
    return count;
}

/** @brief Validate a read identity against its listed fixture path. */
static int readable(const tiku_storage_request_t *request)
{
    char path[TIKU_RESOURCE_PATH_MAX];
    if (request->node_id >= 2 && request->node_id <= 22)
        snprintf(path, sizeof path, "/sample-%02u.txt", (unsigned)request->node_id - 1);
    else if (request->node_id == 101 || request->node_id == 102)
        snprintf(path, sizeof path, "/docs/%s", request->node_id == 101 ? "guide.txt" : "notes.txt");
    else return 0;
    return !strcmp(path, request->path);
}

unsigned surface_host_poll(surface_host_t *host)
{
    unsigned count = 0;
    if (host->request.owner && host->now >= host->request.due) {
        const tiku_storage_request_t *request = host->request.request;
        tiku_resource_error_t status = host->request.cancelled ? TIKU_RESOURCE_CANCELLED :
            host->unavailable ? TIKU_RESOURCE_UNAVAILABLE :
            request->epoch && request->epoch != host->epoch ? TIKU_RESOURCE_STALE_CURSOR : TIKU_RESOURCE_OK;
        size_t bytes = 0;
        int eof = 1;
        if (status == TIKU_RESOURCE_OK && request->operation == TIKU_STORAGE_LIST) bytes = entries(request, &eof);
        else if (status == TIKU_RESOURCE_OK) {
            static const char contents[] = "TikuSurface fixture file. Storage requests complete on the owner thread. Home remains available while a request retires.";
            if (!readable(request)) status = TIKU_RESOURCE_UNAVAILABLE;
            else if (request->offset < sizeof contents - 1) {
                bytes = sizeof contents - 1 - (size_t)request->offset;
                if (bytes > request->capacity) bytes = request->capacity;
                memcpy(request->bytes, contents + request->offset, bytes);
                eof = request->offset + bytes == sizeof contents - 1;
            }
        }
        assert(tiku_paged_storage_complete(host->request.owner, request->token, status, bytes, host->epoch, eof));
        host->request.owner = NULL;
        host->request.request = NULL;
        ++count;
        ++host->completions;
    }
    if (host->frame.owner && host->now >= host->frame_due) {
        assert(tiku_framebuffer_complete(host->frame.owner, host->frame.token, TIKU_PRESENT_OK));
        host->frame = (tiku_present_view_t){0};
        ++count;
    }
    return count;
}

int surface_host_capture(surface_host_t *host, const char *path)
{
    if (!host->frame.owner) return 0;
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    fprintf(file, "P6\n800 480\n255\n");
    for (size_t i = 0; i < 800 * 480; ++i) {
        uint32_t pixel = host->frame.pixels[i];
        unsigned char rgb[3] = {(unsigned char)(pixel >> 16), (unsigned char)(pixel >> 8), (unsigned char)pixel};
        if (fwrite(rgb, 1, 3, file) != 3) { fclose(file); return 0; }
    }
    return fclose(file) == 0;
}
