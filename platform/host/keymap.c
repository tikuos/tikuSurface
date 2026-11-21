/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * keymap.c - distinct editing, navigation and command keys at the host boundary.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "keymap.h"
#include "tiku_event.h"
#include <X11/keysym.h>
unsigned surface_host_key(unsigned long symbol,unsigned modifiers)
{
    if(symbol==XK_Escape)return TIKU_KEY_ESCAPE;
    if(symbol==XK_BackSpace)return TIKU_KEY_BACKSPACE;
    if(symbol==XK_Return||symbol==XK_KP_Enter)return TIKU_KEY_RETURN;
    if(symbol==XK_Delete)return TIKU_KEY_DELETE;
    if(symbol==XK_Tab||symbol==XK_ISO_Left_Tab)return TIKU_KEY_TAB;
    if(symbol==XK_Left)return TIKU_KEY_LEFT;
    if(symbol==XK_Right)return TIKU_KEY_RIGHT;
    if(symbol==XK_Up)return TIKU_KEY_UP;
    if(symbol==XK_Down)return TIKU_KEY_DOWN;
    if(symbol==XK_Home)return TIKU_KEY_HOME;
    if(symbol==XK_End)return TIKU_KEY_END;
    if(symbol==XK_Page_Up)return TIKU_KEY_PAGE_UP;
    if(symbol==XK_Page_Down)return TIKU_KEY_PAGE_DOWN;
    if(symbol<XK_space||symbol>XK_asciitilde)return 0;
    if((modifiers&TIKU_MOD_CMD)&&symbol>='A'&&symbol<='Z')symbol+='a'-'A';
    return (unsigned)symbol;
}
