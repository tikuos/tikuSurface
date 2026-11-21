/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_console_basic.c - real BASIC execution through the retained console stream.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main console_fixture_main
#include "test_console.c"
#undef main
#include <stdarg.h>
/** @brief Mirror the VM fixture's formatted output through its selected stream. */
static int bridge_vsnprintf(char *out,size_t size,const char *format,va_list args)
{
    int n=vsnprintf(out,size,format,args);
    if(n>0&&size){size_t copied=(size_t)n<size?(size_t)n:size-1;for(size_t i=0;i<copied;++i)backend->putc(out[i]);}
    return n;
}
#undef SHELL_PRINTF
#define TIKU_BASIC_HOST_NO_MAIN 1
#define vsnprintf bridge_vsnprintf
#define tiku_shell_io_rx_ready fixture_rx_ready
#define tiku_shell_io_getc fixture_getc
#define tiku_shell_io_has_echo fixture_has_echo
#include "test_basic_smoke.c"
#undef vsnprintf
#undef tiku_shell_io_rx_ready
#undef tiku_shell_io_getc
#undef tiku_shell_io_has_echo
uint8_t tiku_shell_io_cap(void){return backend->cap;}
int main(void)
{
    (void)g_failed;(void)g_passed;
    surface_os_console_init(bridge);assert(surface_os_console_open(bridge));
    char source[]="10 PRINT 42\n20 END\n";unsigned lost;char result[256]={0};
    assert(surface_os_program_start(bridge,source,strlen(source))==1);memset(source,'x',sizeof source-1);
    for(unsigned n=0;n<10&&tiku_basic_mode_active();++n)tiku_basic_mode_tick();
    assert(surface_os_program_state(bridge)==TIKU_BASIC_JOB_DONE);
    int n=surface_os_console_read(bridge,result,255,&lost);assert(n>0&&!lost&&strstr(result,"42"));
    assert(backend->cap==7&&backend->flags==3);
    static const tiku_shell_io_t foreign={uart,NULL,NULL,0,2};tiku_basic_job_t other_job={0};
    assert(!tiku_basic_job_start_owned("10 GOTO 10\n",&foreign,&other_job));
    assert(surface_os_program_start(bridge,"10 END\n",7)==0);assert(surface_os_program_stop(bridge));
    assert(other_job.state==TIKU_BASIC_JOB_RUNNING&&surface_os_program_state(bridge)==TIKU_BASIC_JOB_DONE);
    assert(surface_os_console_close(bridge)&&other_job.state==TIKU_BASIC_JOB_RUNNING&&backend==&tiku_shell_io_uart);
    tiku_basic_job_stop_owned(&other_job);
    assert(surface_os_console_open(bridge));
    assert(surface_os_program_start(bridge,"10 GOTO 10\n",11)==1);assert(surface_os_console_close(bridge));
    assert(!tiku_basic_mode_active()&&backend==&tiku_shell_io_uart);
    assert(surface_os_console_open(bridge));
    assert(surface_os_program_start(bridge,"10 PRINT 1 / 0\n",15)==1);tiku_basic_mode_tick();
    assert(surface_os_program_state(bridge)==TIKU_BASIC_JOB_ERROR);
    assert(surface_os_program_start(bridge,"PRINT 42\n",9)==-1);
    assert(surface_os_program_start(bridge,"10\0END",6)==-1);
    owner=0;assert(surface_os_program_stop(bridge)==0&&surface_os_program_state(bridge)==-1);owner=1;
    assert(surface_os_console_close(bridge));
    puts("Console BASIC: real VM output, copied source, owned results, foreign jobs, errors and close lifetime passed");return 0;
}
