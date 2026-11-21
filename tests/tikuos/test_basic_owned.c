/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_basic_owned.c - caller-owned completion and cancellation in the real VM.
 * SPDX-License-Identifier: Apache-2.0
 */
#define TIKU_BASIC_HOST_NO_MAIN 1
#include "test_basic_smoke.c"
#include <assert.h>
int main(void)
{
    (void)g_failed;(void)g_passed;
    static const char token;const struct tiku_shell_io *io=(const void *)&token;
    tiku_basic_job_t first={0},second={0};char source[]="10 PRINT 42\n20 END\n";
    assert(!tiku_basic_job_start_owned(source,io,&first));assert(first.state==TIKU_BASIC_JOB_RUNNING);
    memset(source,'?',sizeof source-1);assert(tiku_basic_job_start_owned("10 END\n",io,&second)==-1&&!second.state);
    tiku_basic_job_stop_owned(&second);assert(first.state==TIKU_BASIC_JOB_RUNNING);
    for(unsigned n=0;n<10&&tiku_basic_mode_active();++n)tiku_basic_mode_tick();
    assert(first.state==TIKU_BASIC_JOB_DONE&&out_contains("42"));
    assert(!tiku_basic_job_start_owned("10 GOTO 10\n",io,&second));tiku_basic_job_stop_owned(&first);
    assert(second.state==TIKU_BASIC_JOB_RUNNING&&first.state==TIKU_BASIC_JOB_DONE);
    tiku_basic_job_stop_owned(&second);assert(second.state==TIKU_BASIC_JOB_STOPPED&&!tiku_basic_mode_active());
    assert(!tiku_basic_job_start("10 GOTO 10\n",io));tiku_basic_job_stop_owned(&second);
    assert(tiku_basic_mode_active()&&second.state==TIKU_BASIC_JOB_STOPPED);tiku_basic_job_stop();
    assert(!tiku_basic_job_start_owned("10 PRINT 1 / 0\n",io,&first));
    for(unsigned n=0;n<10&&tiku_basic_mode_active();++n)tiku_basic_mode_tick();
    assert(first.state==TIKU_BASIC_JOB_ERROR);assert(tiku_basic_job_start_owned("PRINT 42\n",io,&first)==-1&&first.state==TIKU_BASIC_JOB_ERROR);
    assert(tiku_basic_job_start_owned(NULL,io,&first)==-1);assert(tiku_basic_job_start_owned("10 END\n",io,NULL)==-1);
    for(unsigned n=0;n<100;++n){assert(!tiku_basic_job_start_owned("10 GOTO 10\n",io,&first));tiku_basic_job_stop_owned(&first);assert(first.state==TIKU_BASIC_JOB_STOPPED);}
    puts("Owned BASIC VM: copied source, completion, runtime/load errors, busy admission, foreign ownership and 100 stops passed");return 0;
}
