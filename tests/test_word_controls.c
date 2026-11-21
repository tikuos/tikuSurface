/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_word_controls.c - width, radix and byte-order interaction contracts.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_event.h"
#include "host.h"
#include "surface_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/** @brief Return the owner's copied calculator state. */
static surface_view_t view(surface_t *shell)
{ surface_view_t result; surface_view(shell,&result); return result; }
/** @brief Deliver a physical keyboard character to the product. */
static void key(surface_t *shell, unsigned key)
{
    assert(surface_post(shell,&(tiku_input_event_t){.type=TIKU_EVENT_KEY_DOWN,.key=key}));
    surface_step(shell);
}
/** @brief Enter a sequence of digits through the public input queue. */
static void type(surface_t *shell, const char *text)
{ while(*text) key(shell,(unsigned char)*text++); }
/** @brief Activate one independent touch coordinate. */
static void click(surface_t *shell, int x, int y)
{
    assert(surface_post(shell,&(tiku_input_event_t){.type=TIKU_EVENT_POINTER_DOWN,.button=1,.x=x,.y=y}));
    assert(surface_post(shell,&(tiku_input_event_t){.type=TIKU_EVENT_POINTER_UP,.button=1,.x=x,.y=y}));
    surface_step(shell);
}
/** @brief Exercise full-width input, refused narrowing, radix preservation and swaps. */
int main(int argc, char **argv)
{
    surface_host_t host; surface_config_t config=surface_host_config(&host);
    surface_t *shell=surface_create(&config);
    assert(shell && surface_launch(shell,SURFACE_CALCULATOR)); surface_step(shell);
    assert(view(shell).word_width==64 && view(shell).word_base==16);
    type(shell,"0123456789ABCDEF");
    click(shell,690,330); assert(!strcmp(view(shell).hex,"EFCDAB8967452301"));
    click(shell,690,330); assert(!strcmp(view(shell).hex,"123456789ABCDEF"));
    click(shell,470,330);
    assert(view(shell).word_width==64 && view(shell).status==TIKU_RESOURCE_LIMIT);
    assert(!strcmp(view(shell).hex,"123456789ABCDEF"));
    key(shell,'X'); type(shell,"FF"); click(shell,470,330);
    assert(view(shell).word_width==8 && !strcmp(view(shell).hex,"FF"));
    click(shell,580,330); assert(view(shell).word_base==10 && !strcmp(view(shell).hex,"255"));
    key(shell,TIKU_KEY_BACKSPACE); key(shell,'6');
    assert(view(shell).status==TIKU_RESOURCE_LIMIT && !strcmp(view(shell).hex,"25"));
    key(shell,'5'); assert(!strcmp(view(shell).bases[1],"255"));
    key(shell,'A'); assert(view(shell).status==TIKU_RESOURCE_INVALID);
    surface_view_t v=view(shell);
    assert(surface_hit(&v,240,328)==0);
    click(shell,580,330); assert(view(shell).word_base==8 && !strcmp(view(shell).hex,"377"));
    click(shell,580,330); assert(view(shell).word_base==2 && !strcmp(view(shell).hex,"11111111"));
    key(shell,'1'); assert(view(shell).status==TIKU_RESOURCE_LIMIT && !strcmp(view(shell).hex,"11111111"));
    key(shell,'2'); assert(view(shell).status==TIKU_RESOURCE_INVALID);
    click(shell,580,330); assert(view(shell).word_base==16 && !strcmp(view(shell).hex,"FF"));
    click(shell,470,330); assert(view(shell).word_width==16);
    key(shell,'X'); type(shell,"1234"); click(shell,690,330);
    assert(!strcmp(view(shell).hex,"3412"));
    click(shell,470,330); assert(view(shell).word_width==32);
    click(shell,690,330); assert(!strcmp(view(shell).hex,"12340000"));
    click(shell,470,330); assert(view(shell).word_width==64);
    key(shell,'X'); type(shell,"FFFFFFFFFFFFFFFF");
    click(shell,580,330); assert(!strcmp(view(shell).hex,"18446744073709551615"));
    key(shell,TIKU_KEY_BACKSPACE); key(shell,'6');
    assert(view(shell).status==TIKU_RESOURCE_LIMIT && !strcmp(view(shell).hex,"1844674407370955161"));
    key(shell,'5'); click(shell,580,330); click(shell,580,330);
    assert(view(shell).word_base==2 && strlen(view(shell).hex)==64);
    for(unsigned i=0;i<64;++i) assert(view(shell).hex[i]=='1');
    assert(surface_launch(shell,SURFACE_HOME));surface_step(shell);
    assert(surface_launch(shell,SURFACE_CALCULATOR));surface_step(shell);
    assert(view(shell).word_width==64 && view(shell).word_base==2 && strlen(view(shell).hex)==64);
    static uint32_t full[800*480], rows[800*480];
    v=view(shell); tiku_surface_t a={.px=full,.w=800,.h=480,.clip={0,0,800,480}}, b=a;b.px=rows;
    surface_paint(&a,&v);
    for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}
    assert(!memcmp(full,rows,sizeof full));
    if(argc==2) {
        FILE *file=fopen(argv[1],"wb"); assert(file);
        assert(fprintf(file,"P6\n800 480\n255\n")>0);
        for(unsigned i=0;i<800*480;++i) {
            assert(fputc((full[i]>>16)&255,file)!=EOF);
            assert(fputc((full[i]>>8)&255,file)!=EOF);
            assert(fputc(full[i]&255,file)!=EOF);
        }
        assert(!fclose(file));
    }
    assert(surface_close(shell)==SURFACE_CLOSE_ACCEPTED);
    assert(surface_launch(shell,SURFACE_CALCULATOR)); surface_step(shell);
    assert(view(shell).word_width==64 && view(shell).word_base==16 && !view(shell).hex[0]);
    surface_stop(shell); assert(surface_destroy(&shell) && !host.memory.live);
    puts("Word controls: width, radix, overflow, byte order, retained state and sliced painting passed");
    return 0;
}
