/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * transfer_tcp_fixture.c - real TCP framing and connection lifetimes over a private link.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_TCP_MAIN deferred_tcp_fixture_suite
#include "test_tcp_deferred.c"
#include "transfer_tcp_fixture.h"
tiku_mem_err_t tiku_pool_create(tiku_pool_t *pool,uint8_t *buffer,tiku_mem_arch_size_t block_size,
                                tiku_mem_arch_size_t count,uint8_t id)
{
    assert(pool==&seg_pool&&buffer==tcp_tx_pool_buf&&block_size==TIKU_KITS_NET_TCP_TX_SEG_BLOCK&&
        count==TIKU_KITS_NET_TCP_TX_POOL_COUNT&&id==0x0c&&unlocked);
    return TIKU_MEM_OK;
}
void surface_tcp_fixture_reset(void)
{ reset();memset(listeners,0,sizeof listeners); }
void *surface_tcp_fixture_connect(unsigned index,uint16_t port,unsigned connected)
{
    assert(index<TIKU_KITS_NET_TCP_MAX_CONNS&&!conn_table[index].active);
    tiku_kits_net_tcp_conn_t *c=connection(index);c->local_port=port;
    unsigned i=0;while(i<TIKU_KITS_NET_TCP_MAX_LISTENERS&&listeners[i].port!=port)++i;
    assert(i<TIKU_KITS_NET_TCP_MAX_LISTENERS);
    c->recv_cb=listeners[i].recv_cb;c->event_cb=listeners[i].event_cb;
    if(connected)c->event_cb(c,TIKU_KITS_NET_TCP_EVT_CONNECTED);
    else {c->state=TIKU_KITS_NET_TCP_STATE_SYN_RCVD;assert(tcp_tx_enqueue(c,c->snd_nxt,2,NULL,0));}
    return c;
}
void *surface_tcp_fixture_other(unsigned index)
{ assert(index<TIKU_KITS_NET_TCP_MAX_CONNS&&!conn_table[index].active);return connection(index); }
void surface_tcp_fixture_input(void *conn,const void *data,size_t size)
{
    tiku_kits_net_tcp_conn_t *c=conn;assert(size<2048&&c->active==1&&rx_free(c)>=size);
    tcp_process(c,c->rcv_nxt,c->snd_nxt,TIKU_KITS_NET_TCP_FLAG_ACK|TIKU_KITS_NET_TCP_FLAG_PSH,4096,data,(uint16_t)size);
}
void surface_tcp_fixture_zero_window(void *conn)
{ ((tiku_kits_net_tcp_conn_t *)conn)->rcv_wnd=0; }
size_t surface_tcp_fixture_output(void *conn,void *out,size_t capacity)
{
    tiku_kits_net_tcp_conn_t *c=conn;if(!c||c->active!=1||!c->tx_head)return 0;
    unsigned before=outputs;tiku_kits_net_tcp_deferred_poll();if(outputs==before)return 0;
    size_t n=sent_size-40;assert(n&&n<=capacity);memcpy(out,network+40,n);
    tcp_process(c,c->rcv_nxt,c->snd_nxt,TIKU_KITS_NET_TCP_FLAG_ACK,4096,NULL,0);return n;
}
unsigned surface_tcp_fixture_outputs(void){return outputs;}
unsigned surface_tcp_fixture_active(unsigned index){assert(index<4);return conn_table[index].active;}
void surface_tcp_fixture_disconnect(void *conn,unsigned link_loss)
{
    if(link_loss)tiku_kits_net_tcp_link_down();
    else tcp_process(conn,0,0,TIKU_KITS_NET_TCP_FLAG_RST,0,NULL,0);
}
void surface_tcp_fixture_finish(void)
{
    for(unsigned i=0;i<4;++i)while(tiku_kits_net_tcp_discard_step(&conn_table[i])){}
    assert(allocated==released);
}
