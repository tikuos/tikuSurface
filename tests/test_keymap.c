/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_keymap.c - host editing/navigation symbols and shifted command admission.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "keymap.h"
#include "tiku_event.h"
#include <X11/keysym.h>
#include <assert.h>
#include <stdio.h>
/** @brief Keep byte-control keys separate from page movement and tab navigation. */
int main(void)
{
    assert(surface_host_key(XK_Page_Down,0)==TIKU_KEY_PAGE_DOWN);
    assert(surface_host_key(XK_BackSpace,0)==TIKU_KEY_BACKSPACE);
    assert(surface_host_key(XK_Tab,0)==TIKU_KEY_TAB);
    assert(surface_host_key(XK_Return,0)==TIKU_KEY_RETURN);
    assert(surface_host_key(XK_Escape,0)==TIKU_KEY_ESCAPE);
    assert(surface_host_key(XK_Up,0)==TIKU_KEY_UP&&surface_host_key(XK_Down,0)==TIKU_KEY_DOWN);
    assert(surface_host_key(XK_Page_Up,0)==TIKU_KEY_PAGE_UP);
    assert(surface_host_key(XK_S,TIKU_MOD_CMD|TIKU_MOD_SHIFT)=='s');
    assert(surface_host_key(XK_S,TIKU_MOD_SHIFT)=='S');assert(!surface_host_key(XK_F12,0));
    puts("Host keymap: logical navigation/edit keys and shifted commands passed");return 0;
}
