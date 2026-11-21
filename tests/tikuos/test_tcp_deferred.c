/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_tcp_deferred.c - application I/O without output and paced slot retirement.
 * SPDX-License-Identifier: Apache-2.0
 */
#define TIKU_KITS_NET_TCP_ENABLE 1
#define TIKU_KITS_NET_TCP_DEFERRED_ENABLE 1
#define TIKU_KITS_NET_TCP_BUF_PERSIST 0
#define TIKU_KITS_NET_TCP_MAX_CONNS 4
#define TIKU_KITS_NET_TCP_RX_BUF_SIZE 2048
#define TIKU_KITS_NET_TCP_MSS 536
#define TIKU_KITS_NET_MTU 1500
#include <tikukits/net/ipv4/tiku_kits_net_tcp.h>
#undef TIKU_KITS_NET_TCP_TX_SEG_HDR
#define TIKU_KITS_NET_TCP_TX_SEG_HDR 64U
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <tikukits/net/ipv4/tiku_kits_net_tcp.c>
static union {max_align_t align;uint8_t bytes[64+536];} blocks[8];
static uint8_t occupied[8],network[1500],address[4]={10,0,0,1};
static unsigned released,allocated,outputs,fail_output,notifications,unlocked;
static uint16_t sent_size;
uint16_t tiku_mpu_unlock_nvm(void){assert(!unlocked);unlocked=1;return 0;}
void tiku_mpu_lock_nvm(uint16_t saved){(void)saved;assert(unlocked);unlocked=0;}
void *tiku_pool_alloc(tiku_pool_t *pool)
{
    assert(pool==&seg_pool&&unlocked);
    for(unsigned i=0;i<8;++i)if(!occupied[i]){occupied[i]=1;++allocated;return blocks[i].bytes;}
    return NULL;
}
tiku_mem_err_t tiku_pool_free(tiku_pool_t *pool,void *block)
{
    assert(pool==&seg_pool&&unlocked);
    for(unsigned i=0;i<8;++i)if(block==blocks[i].bytes){assert(occupied[i]);occupied[i]=0;++released;return TIKU_MEM_OK;}
    assert(0);return TIKU_MEM_ERR_INVALID;
}
uint8_t *tiku_kits_net_ipv4_get_buf(uint16_t *size){if(size)*size=sizeof network;return network;}
const uint8_t *tiku_kits_net_ipv4_get_addr(void){return address;}
int8_t tiku_kits_net_ipv4_output(uint8_t *p,uint16_t size)
{ assert(p==network&&size<=sizeof network);++outputs;sent_size=size;return fail_output?TIKU_KITS_NET_ERR_NOLINK:0; }
/** @brief Count notifications to prove local retirement does not invoke application callbacks. */
static void notified(tiku_kits_net_tcp_conn_t *c,uint8_t event)
{ (void)c;(void)event;++notifications; }
/** @brief Start an established connection with empty queues and a usable window. */
static tiku_kits_net_tcp_conn_t *connection(unsigned i)
{
    tiku_kits_net_tcp_conn_t *c=&conn_table[i];memset(c,0,sizeof *c);
    c->active=1;c->state=TIKU_KITS_NET_TCP_STATE_ESTABLISHED;c->snd_nxt=c->snd_una=12345;
    c->snd_mss=536;c->snd_wnd=4096;c->rcv_wnd=2047;c->rto_ticks=3;c->rx_buf=tcp_rx_bufs[i];
    c->remote_addr.b[0]=10;c->remote_addr.b[3]=(uint8_t)(i+2);c->local_port=(uint16_t)(7331+i);c->remote_port=54321;
    c->event_cb=notified;return c;
}
/** @brief Reset isolated counters after each connection has released its segments. */
static void reset(void)
{
    assert(allocated==released);memset(conn_table,0,sizeof conn_table);memset(occupied,0,sizeof occupied);
    allocated=released=outputs=fail_output=notifications=unlocked=0;deferred_cursor=0;
}
/** @brief Queue copies, window limits, failed output and acknowledgment ownership. */
static void sends(void)
{
    reset();tiku_kits_net_tcp_conn_t *c=connection(0);uint8_t data[513];memset(data,0x6b,sizeof data);
    assert(tiku_kits_net_tcp_send_deferred(c,data,513)==TIKU_KITS_NET_ERR_OVERFLOW&&!allocated);
    c->snd_mss=64;assert(tiku_kits_net_tcp_send_deferred(c,data,65)==TIKU_KITS_NET_ERR_OVERFLOW&&!allocated);c->snd_mss=536;
    memset(occupied,1,sizeof occupied);assert(tiku_kits_net_tcp_send_deferred(c,data,1)==TIKU_KITS_NET_ERR_OVERFLOW&&!outputs&&c->snd_nxt==12345);
    memset(occupied,0,sizeof occupied);
    c->snd_wnd=0;assert(tiku_kits_net_tcp_send_deferred(c,data,1)==TIKU_KITS_NET_ERR_OVERFLOW&&!allocated);c->snd_wnd=4096;
    assert(!tiku_kits_net_tcp_send_deferred(c,data,512)&&!outputs&&allocated==1&&c->snd_nxt==12857);
    memset(data,0,sizeof data);assert(tiku_kits_net_tcp_send_deferred(c,data,1)==TIKU_KITS_NET_ERR_OVERFLOW&&allocated==1);
    fail_output=1;tiku_kits_net_tcp_deferred_poll();assert(outputs==1&&c->deferred&&c->tx_count==1);
    for(unsigned i=0;i<8;++i)tiku_kits_net_tcp_periodic();
    assert(c->retries==0&&c->rto_counter==0&&c->active&&c->deferred);
    fail_output=0;unsigned before=outputs;tiku_kits_net_tcp_deferred_poll();
    assert(outputs==before+1&&!c->deferred&&sent_size==552);
    assert(TIKU_KITS_NET_TCP_SEQ(network+20)==12345&&tcp_chksum(network,network+20,532)==0);
    for(unsigned i=40;i<552;++i)assert(network[i]==0x6b);
    before=outputs;tiku_kits_net_tcp_deferred_poll();assert(outputs==before&&c->tx_count==1);
    tcp_process(c,c->rcv_nxt,c->snd_nxt,TIKU_KITS_NET_TCP_FLAG_ACK,4096,NULL,0);
    assert(!c->tx_count&&released==1&&c->snd_una==c->snd_nxt);
    assert(!tiku_kits_net_tcp_send_deferred(c,data,1));before=outputs;
    assert(tiku_kits_net_tcp_discard_step(c)==1&&outputs==before);
    assert(tiku_kits_net_tcp_discard_step(c)==1&&released==2);
    assert(!tiku_kits_net_tcp_discard_step(c)&&!c->active&&outputs==before&&!notifications);
}
/** @brief Read a wrapped full ring in bounded slices without a synchronous window update. */
static void reads_deferred(void)
{
    reset();tiku_kits_net_tcp_conn_t *c=connection(0);c->rx_tail=1800;c->rx_head=1799;c->rcv_wnd=0;
    for(unsigned i=0;i<2047;++i)c->rx_buf[(1800+i)%2048]=(uint8_t)(i*19+3);
    uint8_t readback[2048];memset(readback,0xa5,sizeof readback);unsigned at=0;
    while(at<2047){unsigned n=tiku_kits_net_tcp_read_deferred(c,readback+at,(uint16_t)(2048-at));
        assert(n&&n<=512&&!outputs);at+=n;}
    assert(c->deferred==2&&c->rcv_wnd==2047&&readback[2047]==0xa5);
    for(unsigned i=0;i<2047;++i)assert(readback[i]==(uint8_t)(i*19+3));
    tiku_kits_net_tcp_deferred_poll();assert(outputs==1&&!c->deferred&&sent_size==40);
    assert(TIKU_KITS_NET_TCP_FLAGS(network+20)==TIKU_KITS_NET_TCP_FLAG_ACK);
    assert(!tiku_kits_net_tcp_read_deferred(c,readback,2048));
    assert(tiku_kits_net_tcp_discard_step(c)==1&&!tiku_kits_net_tcp_discard_step(c));
}
/** @brief Poll one queued connection at a time without starving later slots after output failure. */
static void fairness(void)
{
    reset();uint8_t byte=7;
    for(unsigned i=0;i<4;++i)assert(!tiku_kits_net_tcp_send_deferred(connection(i),&byte,1));
    fail_output=1;
    for(unsigned i=0;i<4;++i){unsigned before=outputs;tiku_kits_net_tcp_deferred_poll();
        assert(outputs==before+1&&TIKU_KITS_NET_TCP_SPORT(network+20)==7331+i);}
    fail_output=0;
    for(unsigned i=0;i<4;++i){unsigned before=outputs;tiku_kits_net_tcp_deferred_poll();
        assert(outputs==before+1&&TIKU_KITS_NET_TCP_SPORT(network+20)==7331+i);}
    for(unsigned i=0;i<4;++i)while(tiku_kits_net_tcp_discard_step(&conn_table[i])){}
    assert(released==4&&!notifications);
}
/** @brief Reserve a quiesced slot through link-down and release at most one segment per step. */
static void retirement(void)
{
    reset();tiku_kits_net_tcp_conn_t *c=connection(0);uint8_t byte=4;
    for(unsigned i=0;i<5;++i)assert(tcp_tx_enqueue(c,12345+i,0,&byte,1));
    for(unsigned i=1;i<4;++i)(void)connection(i);
    assert(tiku_kits_net_tcp_discard_step(c)==1&&c->active==2&&!released&&!outputs);
    uint8_t slot,byte_out=0xa5;c->rx_head=1;
    assert(!tiku_kits_net_tcp_read_deferred(c,&byte_out,1)&&byte_out==0xa5);
    assert(tiku_kits_net_tcp_send_deferred(c,&byte,1)==TIKU_KITS_NET_ERR_PARAM);
    assert(!tcp_alloc_conn(&slot));
    unsigned before=outputs;tcp_process(c,0,0,TIKU_KITS_NET_TCP_FLAG_FIN|TIKU_KITS_NET_TCP_FLAG_RST,0,NULL,0);
    tcp_process(c,0,0,TIKU_KITS_NET_TCP_FLAG_FIN,0,NULL,0);
    tiku_kits_net_tcp_abort(c);tiku_kits_net_tcp_periodic();assert(outputs==before&&c->active==2&&c->tx_count==5&&!released);
    tiku_kits_net_tcp_link_down();assert(c->active==2&&c->tx_count==5&&!released&&notifications==3);
    tiku_kits_net_tcp_conn_t *other=tcp_alloc_conn(&slot);assert(other&&other!=c);other->active=0;
    for(unsigned i=0;i<5;++i){unsigned old=released;assert(tiku_kits_net_tcp_discard_step(c)==1&&released==old+1&&c->active==2);}
    assert(!tiku_kits_net_tcp_discard_step(c)&&!c->active&&!c->tx_head&&!c->tx_count);
    assert(tcp_alloc_conn(&slot)==c);c->active=0;assert(outputs==before&&notifications==3);
}
/** @brief Reserve the connection during a network abort notification before its cleanup returns. */
static void notified_retire(tiku_kits_net_tcp_conn_t *c,uint8_t event)
{
    assert(event==TIKU_KITS_NET_TCP_EVT_ABORTED&&c->active==1);
    ++notifications;assert(tiku_kits_net_tcp_discard_step(c)==1&&c->active==2);
}
/** @brief Keep a callback-owned retirement lease when input subsequently frees the connection. */
static void callback_retirement(void)
{
    reset();tiku_kits_net_tcp_conn_t *c=connection(0);uint8_t byte=8;
    for(unsigned i=0;i<3;++i)assert(tcp_tx_enqueue(c,12345+i,0,&byte,1));
    c->event_cb=notified_retire;
    tcp_process(c,0,0,TIKU_KITS_NET_TCP_FLAG_RST,0,NULL,0);
    assert(notifications==1&&c->active==2&&c->tx_count==3&&!released&&!outputs);
    tiku_kits_net_tcp_link_down();assert(c->active==2&&!released&&notifications==1);
    for(unsigned i=0;i<3;++i){unsigned before=released;assert(tiku_kits_net_tcp_discard_step(c)==1&&released==before+1);}
    assert(!tiku_kits_net_tcp_discard_step(c)&&!c->active&&notifications==1);
}
/** @brief Find owned handshakes without exposing unrelated or retired slots. */
static void listener_connections(void)
{
    reset();tiku_kits_net_tcp_conn_t *c=connection(0);c->state=TIKU_KITS_NET_TCP_STATE_SYN_RCVD;
    assert(tiku_kits_net_tcp_listener_connection(c->local_port,notified,0)==c);
    assert(!tiku_kits_net_tcp_listener_connection(c->local_port,notified,4));
    assert(!tiku_kits_net_tcp_listener_connection(c->local_port,NULL,0));
    assert(!tiku_kits_net_tcp_listener_connection(c->local_port,notified_retire,0));
    assert(!tiku_kits_net_tcp_listener_connection(c->local_port+1,notified,0));
    assert(tiku_kits_net_tcp_discard_step(c)==1);
    assert(!tiku_kits_net_tcp_listener_connection(c->local_port,notified,0));
    assert(!tiku_kits_net_tcp_discard_step(c)&&!outputs);
}
#ifndef SURFACE_TCP_MAIN
#define SURFACE_TCP_MAIN main
#endif
int SURFACE_TCP_MAIN(void)
{
    sends();reads_deferred();fairness();retirement();callback_retirement();listener_connections();
    printf("Deferred TCP: queue copies, output fairness, window updates and paced retirement passed (connection %zu bytes)\n",sizeof(tiku_kits_net_tcp_conn_t));return 0;
}
