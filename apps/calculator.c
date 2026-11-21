/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * calculator.c - bounded word input, width/radix selection and byte order.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_word_model.h"
#include "tiku_event.h"
#include <string.h>

#if SURFACE_TEXT_INPUT
const char *surface_calculator_draft(const surface_app_state_t *s)
{return s->checksum_text?s->checksum_literal:s->checksum_input;}
int surface_calculator_checksum_accept(surface_app_state_t *s,const char *text)
{
    unsigned char bytes[255];size_t count;
    if(!tiku_word_checksum_bytes(text,!s->checksum_text,bytes,&count))return 0;
    strcpy(s->checksum_text?s->checksum_literal:s->checksum_input,text);
    s->word_copied=0;s->shell->dirty=1;return 1;
}
/** @brief Format a CRC with the desktop's fixed hexadecimal digit count. */
static void crc_format(uint64_t value,unsigned digits,char out[65])
{
    char raw[65];(void)tiku_word_format(value,16,raw);size_t n=strlen(raw),pad=digits-n;
    memset(out,'0',pad);memcpy(out+pad,raw,n+1);
}
/** @brief Compute named checksums over the selected retained input bytes. */
static int checksum_result(const surface_app_state_t *s,char crc32[65],char crc16[65],size_t *n)
{
    unsigned char bytes[255];
    if(!tiku_word_checksum_bytes(surface_calculator_draft(s),!s->checksum_text,bytes,n))return 0;
    crc_format(tiku_word_crc32(bytes,*n),8,crc32);crc_format(tiku_word_crc16(bytes,*n),4,crc16);return 1;
}
void surface_calculator_checksums(const surface_app_state_t *s,surface_view_t *v)
{
    strcpy(v->checksum_input,surface_calculator_draft(s));v->checksum_text=s->checksum_text;v->word_copied=s->word_copied;
    (void)checksum_result(s,v->crc32,v->crc16,&v->checksum_bytes);
}
/** @brief Append a bounded model string and return its new terminating position. */
static char *append(char *at,const char *text)
{size_t n=strlen(text);memcpy(at,text,n+1);return at+n;}
/** @brief Publish the desktop's conversion or checksum format to the shell clipboard. */
static void copy_result(surface_app_state_t *s,int checksum)
{
    char result[256],number[65];char *at=result;
    if(checksum){
        char crc32[65],crc16[65];size_t n;
        if(!checksum_result(s,crc32,crc16,&n))return;
        (void)tiku_word_format(n,10,number);at=append(at,number);at=append(at," bytes | CRC-32/ISO-HDLC: ");
        at=append(at,crc32);at=append(at," | CRC-16/CCITT-FALSE: ");(void)append(at,crc16);
    }else{
        uint64_t word;if(!tiku_word_parse(s->hex[0]?s->hex:"0",s->word_base,(int)s->word_width,&word))return;
        static const unsigned bases[]={16,10,8,2};static const char *labels[]={"Hex: ","\nDecimal: ","\nOctal: ","\nBinary: "};
        for(unsigned i=0;i<4;++i){at=append(at,labels[i]);(void)tiku_word_format(word,bases[i],number);at=append(at,number);}
    }
    strcpy(s->shell->clipboard,result);s->word_copied=checksum?2:1;s->shell->dirty=1;
}
#endif

void surface_calculator_key(surface_app_state_t *state, unsigned key)
{
#if SURFACE_TEXT_INPUT
    if (state->word_mode) return;
    state->range_copied = 0;state->word_copied=0;
#endif
    if(key<32&&key!=TIKU_KEY_BACKSPACE&&key!=TIKU_KEY_DELETE)return;
    size_t length = strlen(state->hex);
    state->status = TIKU_RESOURCE_OK;
    if (key == TIKU_KEY_BACKSPACE || key == TIKU_KEY_DELETE) { if (length) state->hex[length - 1] = 0; }
    else if (key == 'x' || key == 'X') state->hex[0] = 0;
    else {
        if (key >= 'a' && key <= 'f') key -= 'a' - 'A';
        unsigned digit = key >= '0' && key <= '9' ? key - '0' :
            key >= 'A' && key <= 'F' ? key - 'A' + 10 : 16;
        unsigned width = state->word_width, base = state->word_base;
        unsigned limit = base == 2 ? width : base == 8 ? (width + 2) / 3 :
            base == 16 ? width / 4 : width == 8 ? 3 : width == 16 ? 5 : width == 32 ? 10 : 20;
        if (digit >= base) state->status = TIKU_RESOURCE_INVALID;
        else if (length >= limit) state->status = TIKU_RESOURCE_LIMIT;
        else {
            char candidate[65]; uint64_t word;
            memcpy(candidate, state->hex, length);
            candidate[length] = (char)key;
            candidate[length + 1] = 0;
            if (!tiku_word_parse(candidate, base, (int)width, &word)) state->status = TIKU_RESOURCE_LIMIT;
            else memcpy(state->hex, candidate, length + 2);
        }
    }
    state->shell->dirty = 1;
}

void surface_calculator_action(surface_app_state_t *state, int action)
{
    uint64_t word = 0;
#if SURFACE_TEXT_INPUT
    if(action==ACTION_WORD_COPY||action==ACTION_CHECKSUM_COPY){copy_result(state,action==ACTION_CHECKSUM_COPY);return;}
    state->word_copied=0;
    if(action==ACTION_CHECKSUM_MODE){state->checksum_text^=1;state->shell->dirty=1;return;}
    if (action >= ACTION_WORD_TAB && action <= ACTION_CHECKSUM_TAB) {
        state->word_mode = (unsigned)(action - ACTION_WORD_TAB);
        state->shell->dirty = 1;
        return;
    }
    if (action == ACTION_RANGE_SELECT) {
        state->range_select^=1;state->range_pending=state->range_copied=0;
        state->shell->dirty=1;return;
    }
    if (action == ACTION_RANGE_COPY) {
        (void)tiku_word_parse(state->hex[0]?state->hex:"0",state->word_base,(int)state->word_width,&word);
        (void)tiku_word_format(tiku_word_extract(word,state->range_high,state->range_low),16,state->shell->clipboard);
        state->range_copied=1;state->shell->dirty=1;return;
    }
    if (action == ACTION_BIT_PAGE) {
        if (state->word_width == 64) state->bit_page ^= 1;
        state->shell->dirty = 1;
        return;
    }
    if (action >= ACTION_BIT && action < ACTION_BIT + 32) {
        unsigned bit = state->bit_page * 32 + 31 - (unsigned)(action - ACTION_BIT);
        if (bit >= state->word_width) return;
        state->range_copied=0;
        if (state->range_select) {
            if (!state->range_pending) state->range_high=state->range_low=bit;
            else if (bit>state->range_high) state->range_high=bit;
            else state->range_low=bit;
            state->range_pending^=1;state->shell->dirty=1;return;
        }
        (void)tiku_word_parse(state->hex[0] ? state->hex : "0", state->word_base, (int)state->word_width, &word);
        word ^= UINT64_C(1) << bit;
        (void)tiku_word_format(word, state->word_base, state->hex);
        state->status = TIKU_RESOURCE_OK;
        state->shell->dirty = 1;
        return;
    }
#endif
    if (action < ACTION_WORD_WIDTH || action > ACTION_WORD_SWAP) return;
#if SURFACE_TEXT_INPUT
    state->range_copied = 0;state->word_copied=0;
#endif
    state->status = TIKU_RESOURCE_OK;
    (void)tiku_word_parse(state->hex[0] ? state->hex : "0",
        state->word_base, (int)state->word_width, &word);
    if (action == ACTION_WORD_WIDTH) {
        unsigned width = state->word_width == 64 ? 8 : state->word_width * 2;
        if (width < 64 && word >= (UINT64_C(1) << width)) state->status = TIKU_RESOURCE_LIMIT;
        else {
            state->word_width = width;
#if SURFACE_TEXT_INPUT
            state->bit_page = 0;
            if (state->range_high>=width) state->range_high=width-1;
            if (state->range_low>state->range_high) state->range_low=state->range_high;
            state->range_pending=state->range_copied=0;
#endif
            if (state->hex[0]) (void)tiku_word_format(word, state->word_base, state->hex);
        }
    } else if (action == ACTION_WORD_BASE) {
        state->word_base = state->word_base == 16 ? 10 : state->word_base == 10 ? 8 :
            state->word_base == 8 ? 2 : 16;
        if (state->hex[0]) (void)tiku_word_format(word, state->word_base, state->hex);
    } else {
        word = tiku_word_swap(word, (int)state->word_width);
        (void)tiku_word_format(word, state->word_base, state->hex);
    }
    state->shell->dirty = 1;
}
