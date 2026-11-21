/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * transfer_tcp_fixture.h - isolated TCP peer controls for transfer-service checks.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_TRANSFER_TCP_FIXTURE_H
#define SURFACE_TRANSFER_TCP_FIXTURE_H
#include <stddef.h>
#include <stdint.h>
/** @brief Reset the network fixture after owned TX segments retire. */
void surface_tcp_fixture_reset(void);
/** @brief Establish or stage a passive handshake inheriting the selected listener callbacks. */
void *surface_tcp_fixture_connect(unsigned index,uint16_t port,unsigned connected);
/** @brief Populate an unrelated established slot. */
void *surface_tcp_fixture_other(unsigned index);
/** @brief Inject acknowledged peer data into the real TCP receive state machine. */
void surface_tcp_fixture_input(void *,const void *,size_t);
/** @brief Force the next consumed receive byte to reopen an advertised zero window. */
void surface_tcp_fixture_zero_window(void *);
/** @brief Send one queued segment through the network owner and acknowledge it. */
size_t surface_tcp_fixture_output(void *,void *,size_t);
/** @brief Report the number of network-output callbacks. */
unsigned surface_tcp_fixture_outputs(void);
/** @brief Return a table slot's active/reserved state. */
unsigned surface_tcp_fixture_active(unsigned);
/** @brief Trigger a peer reset or local link loss through the real TCP implementation. */
void surface_tcp_fixture_disconnect(void *,unsigned link_loss);
/** @brief Release remaining unrelated connections. */
void surface_tcp_fixture_finish(void);
#endif
