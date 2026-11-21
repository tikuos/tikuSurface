/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_host_pointer.c - X11 primary-pointer, wheel and cancellation mapping.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main surface_preview_main
#define surface_post test_post
#include "../platform/host/main.c"
#undef main
#undef surface_post
#include <assert.h>
static tiku_input_event_t received;
static unsigned posts;
/** @brief Capture translated events without opening an X11 display. */
int test_post(surface_t *shell,const tiku_input_event_t *event)
{assert(shell);received=*event;posts++;return 1;}
int main(void)
{
    surface_t *shell=(surface_t *)(uintptr_t)1;
    XEvent event={.xbutton={.type=ButtonPress,.button=Button4,.x=123,.y=234,.state=ControlMask|ShiftMask}};
    input(shell,&event);assert(posts==1&&received.type==TIKU_EVENT_WHEEL&&received.dy==-1);
    assert(received.x==123&&received.y==234&&received.modifiers==(TIKU_MOD_CMD|TIKU_MOD_SHIFT));
    event.type=ButtonRelease;input(shell,&event);assert(posts==1);
    event.type=ButtonPress;event.xbutton.button=Button5;event.xbutton.state=ShiftMask;
    input(shell,&event);assert(posts==2&&received.type==TIKU_EVENT_WHEEL&&received.dy==1&&received.modifiers==TIKU_MOD_SHIFT);
    event.type=ButtonRelease;input(shell,&event);assert(posts==2);
    event.type=ButtonPress;event.xbutton.button=Button1;input(shell,&event);
    assert(posts==3&&received.type==TIKU_EVENT_POINTER_DOWN&&received.button==1);
    event.type=ButtonRelease;input(shell,&event);assert(posts==4&&received.type==TIKU_EVENT_POINTER_UP);
    event.xbutton.button=Button2;input(shell,&event);assert(posts==4);
    event=(XEvent){.xmotion={.type=MotionNotify,.x=321,.y=432}};input(shell,&event);
    assert(posts==5&&received.type==TIKU_EVENT_POINTER_MOVE&&received.x==321&&received.y==432);
    event.type=FocusOut;input(shell,&event);assert(posts==6&&received.type==TIKU_EVENT_DEACTIVATED);
    puts("Host pointer: primary capture, wheel direction/modifiers and release mapping passed");return 0;
}
