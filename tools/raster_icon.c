/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * raster_icon.c - offline HVIF conversion to 48-pixel premultiplied ARGB.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_hvif.h"
#include <stdio.h>
#include <stdlib.h>
/** @brief Render one input icon as little-endian ARGB pixels. */
int main(int argc,char **argv) {
 if(argc!=3&&argc!=4)return 2;
 unsigned width=argc==4?(unsigned)atoi(argv[3]):48;if(width!=32&&width!=48)return 2;
 FILE *f=fopen(argv[1],"rb"); if(!f)return 3;
 unsigned char data[65536];size_t n=fread(data,1,sizeof data,f);fclose(f);
 char error[96]; tiku_hvif_t *icon=tiku_hvif_parse(data,n,error,sizeof error);
 if(!icon){fprintf(stderr,"%s\n",error);return 4;}
 uint32_t pixels[48*48];if(tiku_hvif_render(icon,pixels,width,width))return 5;
 f=fopen(argv[2],"wb");if(!f)return 6;
 for(unsigned i=0;i<width*width;++i)for(unsigned k=0;k<4;++k)fputc((pixels[i]>>(8*k))&255,f);
 fclose(f);tiku_hvif_free(icon);return 0;
}
