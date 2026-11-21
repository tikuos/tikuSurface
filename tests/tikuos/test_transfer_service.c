/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_transfer_service.c - independent service admission, scheduling and backing retirement.
 * SPDX-License-Identifier: Apache-2.0
 */
#define TIKUBENCH_HOST_PROCESS_H
#include <kernel/timers/tiku_timer.h>
#define SURFACE_TRANSFER_MAIN packet_fixture_suite
#include "test_media_transfer_async.c"
#include "transfer_tcp_fixture.h"
#define TIKU_KITS_NET_TCP_ENABLE 1
#define TIKU_KITS_NET_TCP_DEFERRED_ENABLE 1
#define TIKU_KITS_NET_TCP_MAX_CONNS 4
#define TIKU_KITS_NET_TCP_RX_BUF_SIZE 2048
#define TIKU_KITS_NET_TCP_MSS 536
#define TIKU_MEDIA_TRANSFER_SERVICE_ENABLE 1
#include <services/storage/tiku_media_transfer_control.c>
#include <services/storage/tiku_media_transfer_listener.c>
#include <services/storage/tiku_media_transfer_tcp.c>
#include <services/storage/tiku_media_transfer_service.c>
static unsigned owner_ok=1,allocation_fail,pump_fail,process_fail,reset_busy,destroy_busy,legacy_active;
static unsigned pump_count,polls,timer_active;
static tiku_shell_pump_fn pump_fn;
static uint64_t simulated_us;
volatile uint8_t tiku_s31_in_trap;
int tiku_thread_in_kernel(void){return (int)owner_ok;}
uint64_t tiku_s31_time(void){return simulated_us*16u;}
tiku_clock_time_t tiku_clock_time(void){return (tiku_clock_time_t)(simulated_us*TIKU_CLOCK_SECOND/1000000u);}
tiku_mem_err_t tiku_tier_arena_create_opts(tiku_arena_t *a,tiku_mem_tier_t tier,tiku_mem_arch_size_t size,
                                          uint8_t id,const tiku_mem_request_t *options)
{
    assert(a==&service_arena&&!a->active&&tier==TIKU_MEM_PSRAM&&id==93&&options->allocation_class==TIKU_MEM_TRANSIENT);
    if(allocation_fail)return TIKU_MEM_ERR_NOMEM;
    a->buf=malloc(size);assert(a->buf);memset(a->buf,0xa5,size);a->capacity=size;a->offset=0;a->active=1;return TIKU_MEM_OK;
}
void *tiku_arena_alloc(tiku_arena_t *a,tiku_mem_arch_size_t size)
{ assert(a->active&&!a->offset&&size==a->capacity);a->offset=size;return a->buf; }
tiku_mem_err_t tiku_arena_stats(const tiku_arena_t *a,tiku_mem_stats_t *out)
{ if(!a->active)return TIKU_MEM_ERR_INVALID;*out=(tiku_mem_stats_t){.used_bytes=a->offset};return TIKU_MEM_OK; }
tiku_mem_err_t tiku_arena_reset(tiku_arena_t *a)
{ assert(a->active);if(reset_busy){--reset_busy;return TIKU_MEM_ERR_BUSY;}a->offset=0;return TIKU_MEM_OK; }
tiku_mem_err_t tiku_arena_destroy(tiku_arena_t *a)
{
    assert(a->active&&!a->offset);if(destroy_busy){--destroy_busy;return TIKU_MEM_ERR_BUSY;}
    free(a->buf);memset(a,0,sizeof *a);return TIKU_MEM_OK;
}
int tiku_shell_add_pump(tiku_shell_pump_fn fn)
{ assert(!pump_count);if(pump_fail)return -1;pump_fn=fn;pump_count=1;return 0; }
void tiku_shell_remove_pump(tiku_shell_pump_fn fn)
{ assert(pump_count&&pump_fn==fn);pump_fn=NULL;pump_count=0; }
void tiku_process_start(struct tiku_process *p,tiku_event_data_t data)
{
    assert(p==&media_transfer_process&&!p->is_running&&!data);if(process_fail)return;
    p->is_running=1;PT_INIT(&p->pt);(void)p->thread(&p->pt,TIKU_EVENT_INIT,NULL);
}
void tiku_process_exit(struct tiku_process *p)
{ assert(p==&media_transfer_process&&p->is_running);(void)p->thread(&p->pt,TIKU_EVENT_EXIT,NULL);p->is_running=0; }
uint8_t tiku_process_is_running(struct tiku_process *p){return p->is_running;}
void tiku_process_poll(struct tiku_process *p){assert(p==&media_transfer_process&&p->is_running);++polls;}
void tiku_timer_set_event(struct tiku_timer *timer,tiku_clock_time_t ticks)
{ assert(timer==&service_timer&&ticks==1);timer_active=1; }
void tiku_timer_stop(struct tiku_timer *timer){assert(timer==&service_timer);timer_active=0;}
int tiku_timer_expired(struct tiku_timer *timer){assert(timer==&service_timer);return 1;}
tiku_media_store_t *tiku_media_card_store(void){return &transfer_store;}
size_t tiku_media_card_read_size(void){return sizeof(tiku_media_async_t);}
void tiku_media_card_read_init(void *p){tiku_media_async_init(p,&read_ops,&transport);}
int tiku_media_card_read_step(void *p,tiku_media_store_t *s){return tiku_media_async_step(p,s);}
int tiku_media_card_read_idle(const void *p){return !((const tiku_media_async_t *)p)->phase;}
int tiku_media_card_read_cancel(void *p,tiku_media_store_t *s,tiku_media_request_t *r){return tiku_media_async_cancel(p,s,r);}
size_t tiku_media_card_mutation_size(void){return sizeof(tiku_media_mutation_t)+tiku_mfs_mutation_size();}
void tiku_media_card_mutation_init(void *p)
{
    tiku_media_mutation_t *q=p;tiku_mfs_mutation_t *m=tiku_mfs_mutation_init(q+1,tiku_mfs_mutation_size());assert(m);
    tiku_media_mutation_init(q,m,&queue_ops,&transport);
}
int tiku_media_card_mutation_step(void *p,tiku_media_store_t *s){return tiku_media_mutation_step(p,s);}
int tiku_media_card_mutation_idle(const void *p){return !((const tiku_media_mutation_t *)p)->phase;}
int tiku_media_card_mutation_cancel(void *p,tiku_media_store_t *s,tiku_media_request_t *r){return tiku_media_mutation_cancel(p,s,r);}
int tiku_media_transfer_server_status(tiku_media_transfer_status_t *out)
{ *out=(tiku_media_transfer_status_t){.active=(uint8_t)legacy_active};return 0; }
/** @brief Progress the dedicated process and enforce one transport callback per turn. */
static void owner_tick(void)
{
    assert(media_transfer_process.is_running);unsigned calls=transport.calls,outputs=surface_tcp_fixture_outputs();
    (void)media_transfer_process.thread(&media_transfer_process.pt,TIKU_EVENT_POLL,NULL);
    assert(transport.calls-calls<=1&&surface_tcp_fixture_outputs()==outputs);simulated_us+=10;
}
/** @brief Complete a typed command without any desktop or shell-application owner. */
static void owner_command(tiku_mt_control_command_t command,uint64_t generation)
{
    uint64_t token;assert(!tiku_mt_service_submit(command,generation,&token));int result;
    for(unsigned i=0;tiku_mt_service_take(token,&result);++i){assert(i<10000);owner_tick();}
    assert(!result);
}
/** @brief Refuse failed provisioning without leaving callback, process or allocator ownership. */
static void admission(void)
{
    uint8_t key[32]={7};uint64_t generation=999;
    owner_ok=0;assert(tiku_mt_service_configure(7777,key,&generation)==TIKU_MT_CONTROL_OWNER);owner_ok=1;
    tiku_s31_in_trap=1;assert(tiku_mt_service_configure(7777,key,&generation)==TIKU_MT_CONTROL_OWNER);tiku_s31_in_trap=0;
    assert(tiku_mt_service_configure(0,key,&generation)==TIKU_MFS_ARG&&generation==999);
    legacy_active=1;assert(tiku_mt_service_configure(7777,key,&generation)==TIKU_MFS_BUSY);legacy_active=0;
    allocation_fail=1;assert(tiku_mt_service_configure(7777,key,&generation)==TIKU_MFS_SPACE);allocation_fail=0;
    pump_fail=1;assert(tiku_mt_service_configure(7777,key,&generation)==TIKU_MFS_BUSY);pump_fail=0;
    assert(!service_memory&&!service_arena.active&&!tcp_binding&&!pump_count);
    process_fail=1;assert(tiku_mt_service_configure(7777,key,&generation)==TIKU_MFS_BUSY);process_fail=0;
    assert(!service_memory&&!service_arena.active&&!tcp_binding&&!pump_count);
}
/** @brief Preserve profile and command ownership across Stop, shared queues and allocator refusal. */
static void lifetime(void)
{
    uint8_t key[32]={7};uint64_t generation;assert(!tiku_mt_service_configure(7777,key,&generation));
    assert(pump_count&&timer_active&&media_transfer_process.is_running);
    tiku_mt_service_snapshot_t initial;assert(!tiku_mt_service_observe(&initial));
    assert(initial.transfer.control.state==TIKU_MT_CONTROL_STOPPED&&!initial.transfer.session.ready);
    legacy_active=1;uint64_t refused=999;assert(tiku_mt_service_submit(TIKU_MT_CONTROL_ENABLE,generation,&refused)==TIKU_MFS_BUSY&&refused==999);
    legacy_active=0;uint64_t gui_token;assert(!tiku_mt_service_submit(TIKU_MT_CONTROL_ENABLE,generation,&gui_token));
    for(unsigned i=0;initial.transfer.control.state!=TIKU_MT_CONTROL_READY;++i){assert(i<3000);owner_tick();assert(!tiku_mt_service_observe(&initial));}
    for(unsigned i=0;i<8;++i)owner_tick();
    int gui_result;assert(!tiku_mt_service_take(gui_token,&gui_result)&&!gui_result);
    void *peer=surface_tcp_fixture_connect(0,7777,1);(void)peer;
    assert(tiku_mt_service_forget()==TIKU_MFS_BUSY);
    owner_command(TIKU_MT_CONTROL_STOP,generation);assert(!surface_tcp_fixture_active(0));
    tiku_mt_service_snapshot_t state;assert(!tiku_mt_service_observe(&state));
    assert(state.available&&state.last_token&&!state.last_result&&state.transfer.control.state==TIKU_MT_CONTROL_STOPPED);
    uint64_t id=create_file("file");(void)id;tiku_mfs_object_t info;
    tiku_media_request_t foreign={.op=TIKU_MEDIA_ENUMERATE,.client=99,.permissions=TIKU_MEDIA_READ,.name="",.info=&info};
    assert(!tiku_media_store_submit(&transfer_store,&foreign));
    for(unsigned i=0;service_media_idle(service_memory);++i){assert(i<8);owner_tick();}
    assert(tiku_mt_service_forget()==TIKU_MFS_BUSY&&!foreign.done);
    for(unsigned i=0;!foreign.done;++i){assert(i<3000);owner_tick();}
    assert(!foreign.result&&!strcmp(info.name,"file"));
    reset_busy=1;destroy_busy=1;assert(tiku_mt_service_forget()==TIKU_MFS_BUSY&&!pump_count&&!tcp_binding);
    assert(!media_transfer_process.is_running&&!timer_active);
    assert(tiku_mt_service_forget()==TIKU_MFS_BUSY);
    assert(tiku_mt_service_configure(7777,key,&generation)==TIKU_MFS_BUSY);
    assert(!tiku_mt_service_forget()&&!service_memory&&!service_arena.active);
}
/** @brief Consume console-owned outcomes and cap continuation bursts by turns and elapsed time. */
static void console_commands(void)
{
    uint8_t key[32]={7};assert(tiku_mt_service_start(7777,key)==TIKU_MT_CONTROL_PENDING);
    unsigned before=polls;for(unsigned i=0;i<32;++i)owner_tick();assert(polls-before<=31&&!service_memory->burst);
    owner_tick();assert(service_memory->burst);simulated_us+=2000;owner_tick();assert(!service_memory->burst);
    for(unsigned i=0;service_memory->console_enable;++i){assert(i<3000);owner_tick();}
    tiku_mt_service_snapshot_t state;assert(!tiku_mt_service_observe(&state)&&state.transfer.session.ready);
    assert(tiku_mt_service_stop()==TIKU_MT_CONTROL_PENDING);
    for(unsigned i=0;service_memory->console_stop;++i){assert(i<3000);owner_tick();}
    assert(!tiku_mt_service_stop()&&!tiku_mt_service_forget());
}
int main(void)
{
    (void)queue_drain;stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);
    void *memory=malloc(tiku_mfs_mutation_size());session=malloc(sizeof *session);packet_owner=malloc(tiku_media_transfer_async_size());
    assert(stable&&cache&&snapshot&&memory&&session&&packet_owner);mutation=tiku_mfs_mutation_init(memory,tiku_mfs_mutation_size());
    packet_reset(NULL);surface_tcp_fixture_reset();admission();lifetime();console_commands();surface_tcp_fixture_finish();
    puts("Transfer owner: admission, independent scheduling, shared queues and retained release passed");
    free(packet_owner);free(session);free(memory);free(stable);free(cache);free(snapshot);return 0;
}
