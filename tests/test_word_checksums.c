/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_word_checksums.c - exact checksum inputs, retained modes and clipboard publication.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#define main field_fixture_main
#include "test_text_entry.c"
#undef main
/** @brief Replace the selected checksum field through clipboard admission. */
static void input(surface_t *s,const char *text)
{strcpy(s->clipboard,text);click(s,100,150);key(s,'v',TIKU_MOD_CMD);key(s,TIKU_KEY_RETURN,0);}
/** @brief Compare complete and two-row painting and optionally capture the full frame. */
static void painting(surface_t *s,const char *name)
{
    static uint32_t full[800*480],rows[800*480];surface_view_t v=view(s);
    tiku_surface_t a={.px=full,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=rows;
    surface_paint(&a,&v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}assert(!memcmp(full,rows,sizeof full));
    const char *dir=getenv("SURFACE_CAPTURE_DIR");if(!dir)return;char path[1024];int n=snprintf(path,sizeof path,"%s/%s.ppm",dir,name);assert(n>0&&(size_t)n<sizeof path);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");for(size_t i=0;i<800*480;++i){fputc(full[i]>>16,f);fputc(full[i]>>8,f);fputc(full[i],f);}assert(!fclose(f));
}
int main(void)
{
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_CALCULATOR));surface_step(s);
    click(s,290,102);surface_view_t v=view(s);assert(!v.checksum_text&&!v.checksum_bytes&&!strcmp(v.crc32,"00000000")&&!strcmp(v.crc16,"FFFF"));
    input(s,"313233343536373839");v=view(s);assert(v.checksum_bytes==9&&!strcmp(v.crc32,"CBF43926")&&!strcmp(v.crc16,"29B1"));
    click(s,550,390);assert(!strcmp(s->clipboard,"9 bytes | CRC-32/ISO-HDLC: CBF43926 | CRC-16/CCITT-FALSE: 29B1"));assert(view(s).word_copied==2);
    click(s,550,105);v=view(s);assert(v.checksum_text&&!v.checksum_bytes&&!v.checksum_input[0]);input(s,"123456789");assert(!strcmp(view(s).crc32,"CBF43926"));
    input(s,"é123456789");v=view(s);assert(v.checksum_bytes==11&&!strcmp(v.checksum_input,"é123456789")&&!strcmp(v.crc32,"2B7CF70E")&&!strcmp(v.crc16,"A87E"));painting(s,"literal-checksum");
    strcpy(s->clipboard,"a\nb");click(s,100,150);key(s,'v',TIKU_MOD_CMD);v=view(s);assert(v.text_error&&!strcmp(v.text_value,"é123456789"));key(s,TIKU_KEY_ESCAPE,0);
    click(s,550,105);assert(!strcmp(view(s).checksum_input,"313233343536373839"));click(s,550,105);assert(!strcmp(view(s).checksum_input,"é123456789"));
    char large[257];memset(large,'A',255);large[255]=0;input(s,large);v=view(s);assert(v.checksum_bytes==255&&strlen(v.checksum_input)==255&&!strcmp(v.crc32,"F1762E81")&&!strcmp(v.crc16,"2C57"));painting(s,"maximum-literal");
    click(s,550,105);large[254]=0;input(s,large);v=view(s);assert(v.checksum_bytes==127&&strlen(v.checksum_input)==254&&!strcmp(v.crc32,"7A4F68B7")&&!strcmp(v.crc16,"EA0E"));painting(s,"maximum-hex");
    large[254]='A';large[255]=0;input(s,large);v=view(s);assert(v.text_entry&&v.text_error&&v.checksum_bytes==127);key(s,TIKU_KEY_ESCAPE,0);
    click(s,100,150);key(s,TIKU_KEY_END,0);key(s,'A',0);key(s,'A',0);v=view(s);assert(v.text_error&&strlen(v.text_value)==255);key(s,TIKU_KEY_ESCAPE,0);
    input(s,"0 G");assert(view(s).text_error&&view(s).checksum_bytes==127);key(s,TIKU_KEY_ESCAPE,0);
    assert(surface_launch(s,SURFACE_HOME));surface_step(s);assert(surface_launch(s,SURFACE_CALCULATOR));surface_step(s);assert(view(s).checksum_bytes==127);click(s,550,105);assert(view(s).checksum_bytes==255);
    click(s,60,102);for(unsigned i=0;i<16;++i)key(s,'F',0);key(s,'c',TIKU_MOD_CMD);
    const char *converted="Hex: FFFFFFFFFFFFFFFF\nDecimal: 18446744073709551615\nOctal: 1777777777777777777777\nBinary: 1111111111111111111111111111111111111111111111111111111111111111";
    assert(!strcmp(s->clipboard,converted));s->clipboard[0]=0;click(s,680,390);assert(!strcmp(s->clipboard,converted));painting(s,"copied-conversions");
#if SURFACE_EDITOR
    assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);key(s,'v',TIKU_MOD_CMD);assert(view(s).editor_bytes==strlen(converted));assert(surface_launch(s,SURFACE_CALCULATOR));surface_step(s);
#endif
    surface_stop(s);assert(surface_destroy(&s)&&!host.memory.live);puts("Developer Calculator: literal/hex limits, mode retention, CRCs, clipboard, field rejection and clipped painting passed");return 0;
}
