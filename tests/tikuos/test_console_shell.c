/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_console_shell.c - copied console input through the native shell parser.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "console_io.h"
#include <shell/tiku_shell_parser.h>
#include <shell/commands/tiku_shell_cmd_echo.h>
#include <kernel/vfs/tiku_vfs.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char input[256];static size_t count,uart_count;
static _Alignas(16) unsigned char bridge[4352];
static tiku_vfs_cap_t capability;
void tiku_uart_putc(char c){(void)c;++uart_count;}
uint8_t tiku_uart_rx_ready(void){return 0;}
int tiku_uart_getc(void){return -1;}
int surface_os_in_owner(void){return 1;}
tiku_vfs_cap_t tiku_vfs_caller_cap_set(tiku_vfs_cap_t cap){tiku_vfs_cap_t old=capability;capability=cap;return old;}
size_t tiku_console_inject(const unsigned char *p,size_t n){if(n>sizeof input-count)n=sizeof input-count;memcpy(input+count,p,n);count+=n;return n;}
void tiku_console_inject_clear(void){count=0;}
int main(void)
{
    static const tiku_shell_cmd_t commands[]={{"echo","echo arguments",tiku_shell_cmd_echo},{NULL,NULL,NULL}};
    tiku_shell_parser_init(commands);tiku_shell_io_set_backend(&tiku_shell_io_uart);tiku_vfs_cap_t original=capability;
    surface_os_console_init(bridge);assert(surface_os_console_open(bridge)&&capability==original);
    const char *line="echo 'hello surface' second";size_t length=strlen(line);
    assert(surface_os_console_write(bridge,line,length)==(int)length&&!uart_count);
    input[count]=0;tiku_shell_parser_execute((char *)input);
    char output[256];unsigned lost;int n=surface_os_console_read(bridge,output,255,&lost);assert(n>0&&!lost);output[n]=0;
    assert(!strcmp(output,"hello surface second\r\n")&&uart_count==(size_t)n);
    assert(surface_os_console_close(bridge)&&count==1&&input[0]==3&&tiku_shell_io_get_backend()==&tiku_shell_io_uart&&capability==original);
    puts("Console shell: copied command, native quoting/dispatch, real echo output, UART mirror and capability restoration passed");return 0;
}
