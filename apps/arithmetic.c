/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * arithmetic.c - touch adaptation of the shared arithmetic calculator.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_ui.h"
#include "tiku_font.h"
#include <string.h>
/** @brief Desktop calculator keys in touch keypad order. */
static const char pad[]="cn%/789*456-123+0 .=";
/** @brief Return the touch rectangle for a logical keypad character. */
static tiku_rect_t rect(unsigned key)
{
    if(key=='b')return (tiku_rect_t){548,146,220,46};
    for(unsigned i=0;i<20;++i)if((unsigned char)pad[i]==key && key!=' ')
        return (tiku_rect_t){32+(int)(i%4)*124,146+(int)(i/4)*54,key=='0'?236:112,46};
    return (tiku_rect_t){0};
}
/** @brief Check one point against a half-open rectangle. */
static int inside(tiku_rect_t r,int x,int y)
{ return x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h; }
int surface_arithmetic_hit(int x,int y)
{
    if(inside((tiku_rect_t){548,208,220,46},x,y))return ACTION_ARITHMETIC_COPY;
    if(inside(rect('b'),x,y))return ACTION_ARITHMETIC_KEY+'b';
    for(unsigned i=0;i<20;++i)if(inside(rect((unsigned)pad[i]),x,y))return ACTION_ARITHMETIC_KEY+pad[i];
    return 0;
}
void surface_arithmetic_key(surface_app_state_t *state,unsigned key,unsigned modifiers)
{
    if(modifiers&TIKU_MOD_OPTION)return;
    if(modifiers&TIKU_MOD_CMD){if(key=='c'||key=='C')surface_arithmetic_action(state,ACTION_ARITHMETIC_COPY);return;}
    if(key==TIKU_KEY_RETURN)key='=';
    if(key==TIKU_KEY_DELETE || key==TIKU_KEY_BACKSPACE)key='b';
    if(key==TIKU_KEY_ESCAPE || key=='C')key='c';
    if(key>127)return;
    tiku_calc_key(&state->arithmetic,(char)key);state->arithmetic_copied=0;state->shell->dirty=1;
}
void surface_arithmetic_action(surface_app_state_t *state,int action)
{
    if(action==ACTION_ARITHMETIC_COPY){memcpy(state->shell->clipboard,state->arithmetic.entry,strlen(state->arithmetic.entry)+1);state->arithmetic_copied=1;state->shell->dirty=1;}
    else if(action>ACTION_ARITHMETIC_KEY && action<ACTION_ARITHMETIC_KEY+128)
        surface_arithmetic_key(state,(unsigned)(action-ACTION_ARITHMETIC_KEY),0);
}
void surface_arithmetic_paint(tiku_surface_t *s,const surface_view_t *v)
{
    tiku_ui_sunken(s,(tiku_rect_t){32,90,480,44},TIKU_RGB(236,240,232));
    tiku_text(s,tiku_font_bold(),48,118,v->arithmetic,TIKU_C_TEXT);
    for(unsigned i=0;i<20;++i){
        char key=pad[i];if(key==' ')continue;char label[2]={key,0};
        tiku_ui_button(s,rect((unsigned)key),key=='c'?"C":key=='n'?"+/-":label,
            v->pressed==ACTION_ARITHMETIC_KEY+key?TIKU_S_PRESSED:0);
    }
    tiku_ui_button(s,rect('b'),"Backspace",v->pressed==ACTION_ARITHMETIC_KEY+'b'?TIKU_S_PRESSED:0);
    tiku_ui_button(s,(tiku_rect_t){548,208,220,46},"Copy result",v->pressed==ACTION_ARITHMETIC_COPY?TIKU_S_PRESSED:0);
    tiku_text(s,tiku_font_plain(),548,290,"Operations run left to right.",TIKU_C_TEXT);
    tiku_text(s,tiku_font_plain(),548,316,"C clears a result or error.",TIKU_C_TEXT);
    if(v->arithmetic_copied)tiku_text(s,tiku_font_plain(),548,350,"Result copied.",TIKU_C_TEXT);
}
