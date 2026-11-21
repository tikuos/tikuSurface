/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_desktop_runtime.c - core desktop admission, touch and display ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main desktop_runtime_fixture_main
#include "test_runtime_lcd.c"
#undef main
/** @brief Retain both desktop tasks across completed display frames and shutdown. */
static void core_lifecycles(void)
{
    unsigned peak=0;
    for(unsigned cycle=0;cycle<40;++cycle){
        assert(surface_runtime_start());await_pending();
        assert(stats().allocations==10&&!stats().allocation_failures);
        poll_result=SURFACE_SCANOUT_OK;
        assert(surface_runtime_poll());
        assert(surface_runtime_launch(2));for(unsigned i=0;i<2000;++i)assert(surface_runtime_poll());
        assert(stats().foreground==2&&!stats().allocation_failures);
        assert(surface_runtime_launch(3));for(unsigned i=0;i<2000;++i)assert(surface_runtime_poll());
        assert(stats().foreground==3&&stats().frames>=3&&!stats().allocation_failures);
        if(stats().peak_bytes>peak)peak=stats().peak_bytes;
        assert(stats().peak_bytes<sizeof region);drain();poll_result=SURFACE_SCANOUT_BUSY;
    }
    printf("Core desktop: peak %u of %zu PSRAM bytes; 40 lifecycles\n",peak,sizeof region);
}
/** @brief Route one raw touch into the desktop volume icon with mapped release. */
static void core_touch(void)
{
    input_failure=1;assert(!surface_runtime_start());assert(!reserved&&!touch_bus_open);input_failure=0;
    assert(surface_runtime_start());touch_report=(surface_touch_report_t){.count=1,.points={{50,70,4}}};
    now+=10000;assert(surface_runtime_poll());touch_report.count=0;now+=10000;assert(surface_runtime_poll());
    for(unsigned i=0;i<40;++i)assert(surface_runtime_poll());
    assert(stats().foreground==2);surface_input_stats_t input;
    assert(surface_runtime_input_stats(&input)&&input.reports&&input.id==4);
    drain();
}
/** @brief Complete core frames within 500 ms with clock cost and timer fallback. */
static void core_frame_cadence(void)
{
    clock_step=100;poll_result=SURFACE_SCANOUT_OK;
    assert(surface_runtime_start());
    const unsigned apps[]={0,2,3,2};
    for(unsigned task=0;task<sizeof apps/sizeof apps[0];++task){
        unsigned completed=stats().completed;
        uint64_t started=now;
        if(apps[task])assert(surface_runtime_launch(apps[task]));
        unsigned turns=0;
        while(stats().completed==completed&&now-started<=500000){
            assert(++turns<2000&&surface_runtime_poll());
            if(!surface_runtime_continue()&&stats().completed==completed)now+=7813;
        }
        printf("Core frame %u: %llu us, %u turns\n",apps[task],
            (unsigned long long)(now-started),turns);fflush(stdout);
        assert(stats().completed>completed&&now-started<=500000);
    }
    drain();poll_result=SURFACE_SCANOUT_BUSY;
}

/** @brief The first frame after an idle Settings launch contains its window. */
static void core_first_frame(void)
{
    poll_result=SURFACE_SCANOUT_OK;clock_step=100;touch_report=(surface_touch_report_t){0};
    assert(surface_runtime_start());
    for(unsigned i=0;!stats().completed;++i)assert(i<2000&&surface_runtime_poll());
    unsigned completed=stats().completed;
    uint16_t home=buffers[front][380*800+250];
    assert(surface_runtime_launch(3));
    for(unsigned i=0;stats().completed==completed;++i)assert(i<2000&&surface_runtime_poll());
    assert(stats().foreground==3);
    assert(buffers[front][380*800+250]!=home);
    surface_frame_stats_t frame;
    assert(surface_runtime_frame_stats(&frame)&&frame.foreground==3&&frame.completed==completed+1);
    assert(frame.frame_us&&frame.max_frame_us>=frame.frame_us);
    assert(frame.requested==2&&frame.presented_request==2);
    assert(frame.request_us>=frame.frame_us&&frame.max_request_us>=frame.request_us);
    drain();poll_result=SURFACE_SCANOUT_BUSY;
    assert(!surface_runtime_frame_stats(&frame));
}

/** @brief A retained Settings frame keeps its request identity after a Tracker launch. */
static void core_frame_request_identity(void)
{
    poll_result=SURFACE_SCANOUT_OK;clock_step=100;
    assert(surface_runtime_start());
    for(unsigned i=0;!stats().completed;++i)assert(i<2000&&surface_runtime_poll());
    assert(surface_runtime_launch(3));poll_result=SURFACE_SCANOUT_BUSY;
    for(unsigned i=0;!pending;++i)assert(i<2000&&surface_runtime_poll());
    assert(surface_runtime_launch(2));poll_result=SURFACE_SCANOUT_OK;
    unsigned before=stats().completed;
    for(unsigned i=0;stats().completed==before;++i)assert(i<2000&&surface_runtime_poll());
    surface_frame_stats_t frame;assert(surface_runtime_frame_stats(&frame));
    assert(frame.requested==3&&frame.presented_request==2&&frame.foreground==3);
    for(unsigned i=0;frame.presented_request!=3;++i){
        assert(i<2000&&surface_runtime_poll());assert(surface_runtime_frame_stats(&frame));
    }
    assert(frame.foreground==2&&frame.request_us>=frame.frame_us);
    drain();poll_result=SURFACE_SCANOUT_BUSY;
}

int main(void)
{
    core_first_frame();core_frame_request_identity();startup_failures();
    for(unsigned fail=8;fail<=10;++fail){surface_runtime_fail_at(fail);assert(!surface_runtime_start());assert(!reserved&&!opened);}
    surface_runtime_fail_at(0);core_lifecycles();core_touch();
    cancellation();faults();stop_timing();frame_cadence();core_frame_cadence();media_continuations();
    calibration_persistence();
    puts("PASS: core runtime, touch, calibration, faults and delayed release");return 0;
}
