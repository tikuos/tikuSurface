/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * host.c - tracked allocations, fake time and serialized fixture completions.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "build_identity.h"
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
    for (slot = 0; slot < sizeof memory->records/sizeof *memory->records && memory->records[slot].pointer; ++slot) { }
    if (slot == sizeof memory->records/sizeof *memory->records) return NULL;
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
    for (slot = 0; slot < sizeof memory->records/sizeof *memory->records && memory->records[slot].pointer != pointer; ++slot) { }
    assert(slot < sizeof memory->records/sizeof *memory->records && pointer);
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

#if SURFACE_TERMINAL
/** @brief Acquire a deterministic console fixture without launching a host process. */
static int terminal_open(void *p){surface_host_t *h=p;if(h->terminal_open||h->terminal_error)return 0;h->terminal_open=1;return 1;}
/** @brief Retain a console while the fixture delays close completion. */
static int terminal_close(void *p){surface_host_t *h=p;if(h->terminal_close_delay){--h->terminal_close_delay;return 0;}h->terminal_open=0;
#if SURFACE_BASIC
if(h->program_state==SURFACE_PROGRAM_RUNNING){h->program_state=SURFACE_PROGRAM_STOPPED;++h->program_stops;}
#endif
return 1;}
/** @brief Capture accepted command bytes under the fixture's backpressure limit. */
static int terminal_write(void *p,const void *bytes,size_t n)
{
    surface_host_t *h=p;if(!h->terminal_open||h->terminal_error)return -1;
    if(n>(size_t)h->terminal_write_limit)n=(size_t)h->terminal_write_limit;
    if(n>sizeof h->terminal_input-h->terminal_in_size)n=sizeof h->terminal_input-h->terminal_in_size;
    memcpy(h->terminal_input+h->terminal_in_size,bytes,n);h->terminal_in_size+=n;return (int)n;
}
/** @brief Copy fixture output into the application without retaining its buffer. */
static int terminal_read(void *p,void *bytes,size_t n,unsigned *lost)
{
    surface_host_t *h=p;if(!h->terminal_open||h->terminal_error)return -1;
    if(n>h->terminal_out_size)n=h->terminal_out_size;
    memcpy(bytes,h->terminal_output,n);h->terminal_out_size-=n;memmove(h->terminal_output,h->terminal_output+n,h->terminal_out_size);*lost=0;return (int)n;
}
/** @brief Replace captured pending input with the fixture's cancellation byte. */
static int terminal_interrupt(void *p){surface_host_t *h=p;if(!h->terminal_open)return 0;h->terminal_input[0]=3;h->terminal_in_size=1;return 1;}
#if SURFACE_BASIC
/** @brief Copy a program snapshot into the deterministic host job fixture. */
static int program_start(void *p,const char *source,size_t length)
{
    surface_host_t *h=p;if(!h->terminal_open||h->program_busy||h->program_state==SURFACE_PROGRAM_RUNNING)return 0;
    if(!source||!length||length>8192||memchr(source,0,length)||source[length])return -1;
    memcpy(h->program_source,source,length+1);h->program_state=SURFACE_PROGRAM_RUNNING;return 1;
}
/** @brief Report this fixture's program result. */
static int program_state(void *p){return ((surface_host_t *)p)->program_state;}
/** @brief Stop the job admitted by this fixture. */
static int program_stop(void *p){surface_host_t *h=p;if(h->program_state==SURFACE_PROGRAM_RUNNING){h->program_state=SURFACE_PROGRAM_STOPPED;++h->program_stops;}return 1;}
#endif
static const tiku_terminal_provider_ops_t terminal_ops={terminal_open,terminal_close,terminal_write,terminal_read,terminal_interrupt};
#endif
#if SURFACE_DEVICE
/** @brief Copy a deterministic process slot without host process access. */
static int process_read(void *context,unsigned index,surface_process_info_t *out)
{
    surface_host_t *h=context;++h->process_reads;memset(out,0,sizeof *out);
    if(h->process_error)return -1;
    if(index>=8||h->process_missing)return 0;
    snprintf(out->name,sizeof out->name,"process-%u",index);strcpy(out->state,"RUNNING");
    out->sram=128+index;out->nvm=32;out->uptime=(uint32_t)(h->now/1000000);out->wakes=index*10;out->events=index;out->restarts=index%2;return 1;
}
/** @brief Return deterministic local telemetry without a host device side effect. */
static int device_read(void *context,const char *path,char *out,size_t capacity)
{
    surface_host_t *h=context;++h->device_reads;if(h->device_failed)return -1;
    unsigned value=!strcmp(path,"/sys/mem/used")?h->device_used:!strcmp(path,"/sys/cpu/freq")?320000000:
        !strcmp(path,"/sys/mem/sram")?524288:!strcmp(path,"/sys/mem/free")?65536:
        !strcmp(path,"/sys/mem/stack_free")?2048:!strcmp(path,"/proc/count")?9:(unsigned)(h->now/1000000);
    int n=snprintf(out,capacity,"%u",value);return n<0||(size_t)n>=capacity?-1:n;
}
#endif
#if SURFACE_VFS
/** @brief Copy metadata from a deterministic namespace without reading values. */
static int vfs_stat(void *context,const char *path,surface_vfs_node_t *out)
{
    surface_host_t *h=context;++h->vfs_stats;memset(out,0,sizeof *out);
    const char *name=strrchr(path,'/');if(!name||strlen(name+1)>63)return -1;
    strcpy(out->name,name+1);out->id=1;
    if(!strcmp(path,"/")||!strcmp(path,"/sys")||!strcmp(path,"/sys/device"))out->flags=SURFACE_NODE_DIRECTORY;
    else if(!strcmp(path,"/data"))out->flags=SURFACE_NODE_DIRECTORY|SURFACE_NODE_DYNAMIC;
    else if(!strcmp(path,"/sys/device/name")){out->id=2;out->flags=SURFACE_NODE_READ|(h->vfs_readonly?0:SURFACE_NODE_EDIT|SURFACE_NODE_WRITE);out->policy=SURFACE_READ_POLL;}
    else if(!strncmp(path,"/sys/value",10)&&path[10]>='1'&&path[10]<='8'&&!path[11]){out->id=3;out->flags=SURFACE_NODE_READ;out->policy=SURFACE_READ_EFFECT;}
    else return -1;
    return 0;
}
/** @brief Return up to six copied children and explicit dynamic-directory metadata. */
static int vfs_list(void *context,const char *path,unsigned offset,surface_vfs_node_t *out,size_t cap,size_t *count,int *more,int *dynamic)
{
    surface_host_t *h=context;++h->vfs_lists;*count=0;*more=*dynamic=0;
    unsigned total=!strcmp(path,"/")?2:!strcmp(path,"/sys")?9:!strcmp(path,"/sys/device")?1:0;
    *dynamic=!strcmp(path,"/data");
    for(unsigned i=offset;i<total&&*count<cap;++i){char child[64];
        if(!strcmp(path,"/"))strcpy(child,i?"/data":"/sys");
        else if(!strcmp(path,"/sys/device"))strcpy(child,"/sys/device/name");
        else if(!i)strcpy(child,"/sys/device");else snprintf(child,sizeof child,"/sys/value%u",i);
        if(vfs_stat(context,child,&out[*count]))return -1;
        ++*count;
    }
    *more=offset+*count<total;return 0;
}
/** @brief Read one fixture value when the application explicitly requests it. */
static int vfs_read(void *context,const char *path,uint32_t id,char *out,size_t cap,size_t *total)
{
    surface_host_t *h=context;(void)path;++h->vfs_reads;if(h->vfs_fail_read||id<2)return -1;
    const char *value=id==2?h->vfs_value:"explicit effectful fixture read";*total=strlen(value);
    int n=snprintf(out,cap,"%s",value);return n<0?-1:(size_t)n>=cap?(int)cap-1:n;
}
/** @brief Replace the fixture setting after identity, permission and value checks. */
static int vfs_write(void *context,const char *path,uint32_t id,const char *value,size_t bytes,const char *before,size_t before_bytes)
{
    surface_host_t *h=context;
    if(h->vfs_fail_write||h->vfs_readonly||id!=2||strcmp(path,"/sys/device/name")||bytes>=sizeof h->vfs_value)return -1;
    if(strlen(h->vfs_value)!=before_bytes||memcmp(h->vfs_value,before,before_bytes))return -11;
    memcpy(h->vfs_value,value,bytes);h->vfs_value[bytes]=0;++h->vfs_writes;return 0;
}
#endif
surface_config_t surface_host_config(surface_host_t *host)
{
    static const tiku_allocator_ops_t allocator = {allocate, release};
    static const tiku_storage_provider_ops_t storage = {submit, cancel};
    *host = (surface_host_t){.storage_delay = 20000, .present_delay = 16000, .epoch = 1};
#if SURFACE_TERMINAL
    host->terminal_write_limit=256;
#endif
#if SURFACE_VFS
    strcpy(host->vfs_value,"preview-device");
#endif
    surface_config_t config = {
#if SURFACE_VFS
        .vfs={host,vfs_list,vfs_stat,vfs_read,vfs_write},
#endif
#if SURFACE_TERMINAL
        .terminal={&terminal_ops,host},
#if SURFACE_DEVICE
        .device={host,device_read,process_read},
#endif
#if SURFACE_BASIC
        .program={host,program_start,program_state,program_stop},
#endif
#endif
        .build_identity = SURFACE_BUILD_ID,
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
#if SURFACE_FILE_INFO
            if(readable(request))(void)tiku_paged_storage_describe(host->request.owner,request->token,sizeof contents-1,request->node_id);
#endif
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
