/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * png_fixture.h - deterministic RGBA PNG fixtures with stored DEFLATE blocks.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_TEST_PNG_FIXTURE_H
#define SURFACE_TEST_PNG_FIXTURE_H
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/** @brief Write a PNG big-endian word. */
static inline void png_word(unsigned char *p,uint32_t v)
{p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v;}
/** @brief Write the CRC after a chunk's type and data. */
static inline void png_crc(unsigned char *p,size_t n)
{
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<n;i++){crc^=p[i];for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
    png_word(p+n,~crc);
}
/** @brief Fill a complete PNG; padding extends IDAT through the requested size. */
static inline size_t png_fixture(unsigned char *out,size_t capacity,unsigned w,unsigned h,size_t padded)
{
    size_t raw=((size_t)w*4+1)*h,blocks=(raw+65534)/65535,idat=2+blocks*5+raw+4,total=57+idat;
    if(padded>total){total=padded;idat=total-57;}
    assert(w&&h&&total<=capacity);
    memset(out,0,total);memcpy(out,"\211PNG\r\n\032\n",8);
    png_word(out+8,13);memcpy(out+12,"IHDR",4);png_word(out+16,w);png_word(out+20,h);out[24]=8;out[25]=6;png_crc(out+12,17);
    png_word(out+33,(uint32_t)idat);memcpy(out+37,"IDAT",4);out[41]=0x78;out[42]=1;
    size_t offset=43,used=0;uint32_t a=1,b=0;
    while(used<raw){
        size_t n=raw-used;if(n>65535)n=65535;
        out[offset++]=used+n==raw;out[offset++]=(unsigned char)n;out[offset++]=(unsigned char)(n>>8);
        out[offset++]=(unsigned char)~n;out[offset++]=(unsigned char)(~n>>8);
        for(size_t i=0;i<n;i++){
            size_t at=used+i,column=at%((size_t)w*4+1);unsigned char value=column?(unsigned char)(at*37):0;
            out[offset++]=value;a=(a+value)%65521;b=(b+a)%65521;
        }
        used+=n;
    }
    png_word(out+offset,b<<16|a);png_crc(out+37,idat+4);
    png_word(out+45+idat,0);memcpy(out+49+idat,"IEND",4);png_crc(out+49+idat,4);
    return total;
}
#endif
