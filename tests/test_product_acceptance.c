/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_product_acceptance.c - desktop theme and explicit calibration defaults.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host.h"
#include "surface_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned saves, status = SURFACE_SETTINGS_SAVED;
static surface_touch_map_t submitted;
/** @brief Supply a saved mapping that differs from panel defaults. */
static unsigned read_map(void *context, surface_touch_map_t *map)
{ (void)context; *map=(surface_touch_map_t){12,1023,24,767,1,1,1}; return status; }
/** @brief Count explicit persistence calls without applying them. */
static int save_map(void *context, const surface_touch_map_t *map)
{ (void)context; submitted=*map; ++saves; status=SURFACE_SETTINGS_PENDING; return 1; }
/** @brief Send a complete primary pointer action through the shell queue. */
static void click(surface_t *s, int x, int y)
{
    tiku_input_event_t e={.type=TIKU_EVENT_POINTER_DOWN,.button=1,.x=x,.y=y};
    assert(surface_post(s,&e)); e.type=TIKU_EVENT_POINTER_UP; assert(surface_post(s,&e)); surface_step(s);
}
/** @brief Check full and two-row output for application and failure states. */
static void paint_states(const char *directory)
{
    static uint32_t full[800*480], sliced[800*480];
    static const char *names[]={"home","home-pressed","calculator","browser-empty",
        "browser-loading","browser-unavailable","browser-stale","browser-selected",
        "settings-edited","settings-saving","settings-failed","settings-incompatible",
        "applications-menu","close-confirmation"};
    for (unsigned n=0;n<sizeof names/sizeof names[0];++n) {
        surface_view_t v={0};
        strcpy(v.platform_name,"ESP32-S31"); strcpy(v.build_identity,"Surface test fixture / TikuOS test fixture");
        if(n==1)v.pressed=ACTION_CALCULATOR;
        if(n==2){v.foreground=SURFACE_CALCULATOR;strcpy(v.hex,"FF");strcpy(v.bases[1],"255");}
        if(n>=3 && n<=7) {
            v.foreground=SURFACE_BROWSER;strcpy(v.path,"/");
            if(n==4)v.status=TIKU_RESOURCE_PENDING;
            if(n==5)v.status=TIKU_RESOURCE_UNAVAILABLE;
            if(n==6)v.status=TIKU_RESOURCE_STALE_CURSOR;
            if(n==7){v.row_count=1;strcpy(v.rows[0],"notes.txt");v.pressed=ACTION_ROW;}
        }
        if(n==12){v.applications_menu=1;v.open_apps=14;}
        if(n==13)v.close_confirmation=1;
        if(n>=8){v.foreground=SURFACE_SETTINGS;v.settings_dirty=1;
            v.settings_status=n==8?SURFACE_SETTINGS_SAVED:n==9?SURFACE_SETTINGS_PENDING:
                n==10?SURFACE_SETTINGS_FAILED:SURFACE_SETTINGS_INCOMPATIBLE;}
        tiku_surface_t a={.px=full,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=sliced;
        memset(sliced,0xa5,sizeof sliced);surface_paint(&a,&v);
        for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}
        assert(!memcmp(full,sliced,sizeof full));
        if(n==7)assert(full[120*800+40]==0x336698);
        if(directory) {
            char path[1024];assert(snprintf(path,sizeof path,"%s/%s.ppm",directory,names[n])<(int)sizeof path);
            FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");
            for(unsigned i=0;i<800*480;++i){fputc(full[i]>>16,f);fputc(full[i]>>8,f);fputc(full[i],f);}
            assert(!fclose(f));
        }
    }
}
/** @brief Check the selected requirement through the public shell. */
int main(int argc, char **argv)
{
    assert(argc==2 || argc==3);
    if(!strcmp(argv[1],"states")){paint_states(argc==3?argv[2]:NULL);return 0;}
    surface_host_t host; surface_config_t c=surface_host_config(&host);
    c.settings=(surface_settings_provider_t){NULL,read_map,save_map};
    surface_t *s=surface_create(&c); assert(s); surface_step(s);
    if (!strcmp(argv[1],"theme")) {
        assert(surface_render(s,surface_host_presenter(&host))==TIKU_PRESENT_OK);
        assert(host.frame.pixels[0]==0x336698);
        assert(host.frame.pixels[80*800+25]==0xffcb00);
        assert(host.frame.pixels[110*800+24]==0xd8d8d8);
    } else {
        assert(surface_launch(s,SURFACE_SETTINGS)); surface_step(s);
        click(s,90,400);
        surface_view_t v; surface_view(s,&v);
        assert(v.calibration.x_min==0 && v.calibration.x_max==799);
        assert(v.calibration.y_min==0 && v.calibration.y_max==479);
        assert(!v.calibration.swap && !v.calibration.invert_x && !v.calibration.invert_y);
        assert(!saves);
        assert(v.settings_dirty);
        click(s,650,350); assert(saves==1 && submitted.x_min==0 && submitted.x_max==799);
        assert(submitted.y_min==0 && submitted.y_max==479 && !submitted.swap && !submitted.invert_x && !submitted.invert_y);
        click(s,60,350); click(s,90,400); click(s,650,350);
        surface_view(s,&v); assert(!v.calibration.swap && saves==1 && v.settings_status==SURFACE_SETTINGS_PENDING);
        status=SURFACE_SETTINGS_FAILED;surface_step(s);surface_view(s,&v);
        assert(v.settings_dirty && v.settings_status==SURFACE_SETTINGS_FAILED);
        click(s,650,350);status=SURFACE_SETTINGS_SAVED;surface_step(s);surface_view(s,&v);
        assert(!v.settings_dirty && v.settings_status==SURFACE_SETTINGS_SAVED && saves==2);
        assert(strstr(v.build_identity,"Surface "));
        for(unsigned blocked=SURFACE_SETTINGS_UNAVAILABLE;blocked<=SURFACE_SETTINGS_INCOMPATIBLE;blocked+=2) {
            assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);surface_step(s);
            status=blocked;assert(surface_launch(s,SURFACE_SETTINGS));surface_step(s);
            click(s,90,400);click(s,60,350);click(s,650,350);surface_view(s,&v);
            assert(v.calibration.x_min==12 && v.calibration.swap && saves==2 && !v.settings_dirty);
        }
    }
    host.now+=100000; surface_host_poll(&host); surface_stop(s);
    assert(surface_destroy(&s)); assert(!host.memory.live); return 0;
}
