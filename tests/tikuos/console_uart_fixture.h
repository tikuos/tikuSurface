/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * console_uart_fixture.h - UART callback declarations for the host shell binding.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_CONSOLE_UART_FIXTURE_H
#define SURFACE_CONSOLE_UART_FIXTURE_H
#include <stdint.h>
#include <stddef.h>
void tiku_uart_putc(char);
uint8_t tiku_uart_rx_ready(void);
int tiku_uart_getc(void);
#endif
