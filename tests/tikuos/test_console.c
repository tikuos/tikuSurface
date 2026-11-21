/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_console.c - console ownership, bounded queues and UART restoration.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "console_io.h"
#include <shell/tiku_shell_io.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int owner=1;static unsigned echoed;
static unsigned char injected[256];static size_t count;
static _Alignas(16) unsigned char bridge[4352],other[4352];
static void uart(char c){(void)c;++echoed;}
const tiku_shell_io_t tiku_shell_io_uart={uart,NULL,NULL,3,7};
static const tiku_shell_io_t *backend=&tiku_shell_io_uart;
int surface_os_in_owner(void){return owner;}
const tiku_shell_io_t *tiku_shell_io_get_backend(void){return backend;}
void tiku_shell_io_set_backend(const tiku_shell_io_t *p){backend=p;}
size_t tiku_console_inject(const unsigned char *p,size_t n)
{if(n>sizeof injected-count)n=sizeof injected-count;memcpy(injected+count,p,n);count+=n;return n;}
void tiku_console_inject_clear(void){count=0;}
int main(void)
{
    assert(surface_os_console_size()<=sizeof bridge);surface_os_console_init(bridge);surface_os_console_init(other);
    owner=0;assert(!surface_os_console_open(bridge));owner=1;assert(surface_os_console_open(bridge));
    assert(backend->cap==7&&backend->flags==3);assert(!surface_os_console_open(other));
    assert(surface_os_console_write(bridge,"echo hi\n",8)==8&&count==8&&!memcmp(injected,"echo hi\n",8));
    char output[256];unsigned lost;
    for(unsigned i=0;i<5000;++i)backend->putc((char)('a'+i%26));
    assert(echoed==5000&&surface_os_console_read(bridge,output,256,&lost)==256&&lost==905);
    for(unsigned i=0;i<256;++i)assert(output[i]==(char)('a'+i%26));
    assert(surface_os_console_read(bridge,output,256,&lost)==256&&!lost);
    assert(surface_os_console_interrupt(bridge)&&count==1&&injected[0]==3);
    owner=0;assert(surface_os_console_write(bridge,"x",1)==-1&&!surface_os_console_close(bridge));owner=1;
    assert(surface_os_console_close(bridge)&&backend==&tiku_shell_io_uart);
    assert(surface_os_console_write(bridge,"x",1)==-1);assert(surface_os_console_open(other));
    static const tiku_shell_io_t foreign={uart,NULL,NULL,0,0};backend=&foreign;
    assert(surface_os_console_read(other,output,1,&lost)==-1&&surface_os_console_close(other)&&backend==&foreign);
    puts("Console bridge: owner admission, UART mirror, bounded loss, copied input, cancellation and backend restoration passed");return 0;
}
