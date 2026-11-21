/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_sessions.c - retained tasks, modal close and asynchronous shutdown.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_event.h"
#include "host.h"
#include "surface_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/** @brief Advance one owner turn and return independent visible state. */
static surface_view_t view(surface_t *shell)
{ surface_view_t v; surface_step(shell); surface_view(shell, &v); return v; }
/** @brief Deliver an input key to the owner. */
static void key(surface_t *shell, unsigned key)
{
    assert(surface_post(shell, &(tiku_input_event_t){.type=TIKU_EVENT_KEY_DOWN,.key=key}));
    surface_step(shell);
}
/** @brief Activate a control through pointer press and release. */
static void click(surface_t *shell, int x, int y)
{
    assert(surface_post(shell, &(tiku_input_event_t){.type=TIKU_EVENT_POINTER_DOWN,.button=1,.x=x,.y=y}));
    assert(surface_post(shell, &(tiku_input_event_t){.type=TIKU_EVENT_POINTER_UP,.button=1,.x=x,.y=y}));
    surface_step(shell);
}
/** @brief Complete fixture providers without invoking an unrelated owner. */
static void complete(surface_host_t *host)
{ host->now += 100000; (void)surface_host_poll(host); }
/** @brief Stop and retire owned tasks and check allocator restoration. */
static void finish(surface_host_t *host, surface_t **shell)
{
    surface_stop(*shell); complete(host); surface_step(*shell);
    assert(surface_destroy(shell)); assert(!host->memory.live && !host->memory.bytes);
}
/** @brief Menu selection preserves input and does not forward modal keys. */
static void menu(void)
{
    surface_host_t host; surface_config_t config=surface_host_config(&host);
    surface_t *shell=surface_create(&config);
    assert(surface_launch(shell,SURFACE_CALCULATOR)); (void)view(shell); key(shell,'A');
    size_t calls=host.memory.calls;
    click(shell,590,30); assert(view(shell).applications_menu);
    key(shell,'F'); assert(!strcmp(view(shell).hex,"A"));
    click(shell,50,30); assert(view(shell).applications_menu);
    key(shell,'3'); assert(view(shell).foreground==SURFACE_SETTINGS);
    assert(view(shell).open_apps==((1u<<SURFACE_CALCULATOR)|(1u<<SURFACE_SETTINGS)));
    click(shell,590,30); click(shell,300,SURFACE_TERMINAL?122:SURFACE_EDITOR?138:165);
    assert(view(shell).foreground==SURFACE_CALCULATOR && !strcmp(view(shell).hex,"A"));
    size_t warmed=host.memory.calls; assert(warmed>calls);
    assert(surface_launch(shell,SURFACE_CALCULATOR)); (void)view(shell);
    assert(host.memory.calls==warmed && !strcmp(view(shell).hex,"A"));
    click(shell,590,30); key(shell,TIKU_KEY_ESCAPE);
    assert(!view(shell).applications_menu && view(shell).foreground==SURFACE_CALCULATOR);
    click(shell,720,30); assert(view(shell).foreground==SURFACE_HOME);
    assert(!(view(shell).open_apps & (1u<<SURFACE_CALCULATOR)));
    assert(surface_launch(shell,SURFACE_CALCULATOR)); assert(!view(shell).hex[0]);
    finish(&host,&shell);
}

/** @brief Keep browser requests alive while another task owns input. */
static void browser(void)
{
    surface_host_t host; surface_config_t config=surface_host_config(&host);
    surface_t *shell=surface_create(&config);
    assert(surface_launch(shell,SURFACE_BROWSER)); (void)view(shell);
    assert(host.request.owner);
    assert(surface_launch(shell,SURFACE_CALCULATOR)); (void)view(shell); key(shell,'F');
    assert(!host.cancellations && host.request.owner);
    complete(&host); (void)view(shell);
    assert(shell->background[SURFACE_BROWSER].state->status==TIKU_RESOURCE_OK);
    assert(surface_launch(shell,SURFACE_BROWSER)); assert(view(shell).row_count==6);
    click(shell,70,130); complete(&host); (void)view(shell);
    assert(!strcmp(view(shell).path,"/docs"));
    click(shell,70,130); assert(host.request.owner);
    assert(surface_launch(shell,SURFACE_HOME)); (void)view(shell);
    complete(&host); (void)view(shell);
    assert(surface_launch(shell,SURFACE_BROWSER));
    surface_view_t v=view(shell);
    assert(v.reading && v.status==TIKU_RESOURCE_OK && v.preview_bytes);
    assert(!strcmp(v.path,"/docs"));
    assert(!host.cancellations);
    assert(surface_launch(shell,SURFACE_CALCULATOR)); assert(!strcmp(view(shell).hex,"F"));
    finish(&host,&shell);
}

/** @brief Fixture storage for acknowledged settings writes. */
static struct { surface_touch_map_t map; unsigned status, saves; } settings;
/** @brief Return the fixture's active record and completion state. */
static unsigned read_settings(void *context, surface_touch_map_t *map)
{ (void)context; *map=settings.map; return settings.status; }
/** @brief Admit the submitted draft for asynchronous acknowledgement. */
static int save_settings(void *context, const surface_touch_map_t *map)
{ (void)context; settings.map=*map; settings.status=SURFACE_SETTINGS_PENDING; ++settings.saves; return 1; }

/** @brief Preserve drafts on switching; close confirmation cannot leak input. */
static void drafts(void)
{
    surface_host_t host; surface_config_t config=surface_host_config(&host);
    settings.map=(surface_touch_map_t){0,799,0,479,0,0,0};
    settings.status=SURFACE_SETTINGS_SAVED; settings.saves=0;
    config.settings=(surface_settings_provider_t){NULL,read_settings,save_settings};
    surface_t *shell=surface_create(&config);
    assert(surface_launch(shell,SURFACE_SETTINGS)); (void)view(shell);
    click(shell,80,350); assert(view(shell).calibration.swap && view(shell).settings_dirty);
    assert(surface_launch(shell,SURFACE_CALCULATOR)); (void)view(shell);
    assert(surface_launch(shell,SURFACE_SETTINGS));
    assert(view(shell).calibration.swap && view(shell).settings_dirty && !settings.saves);
    click(shell,720,30); assert(view(shell).close_confirmation);
    assert(!surface_launch(shell,SURFACE_HOME));
    click(shell,80,350); click(shell,640,350); click(shell,590,30);
    assert(view(shell).calibration.swap && !settings.saves && view(shell).close_confirmation);
    key(shell,TIKU_KEY_RETURN); assert(view(shell).close_confirmation);
    key(shell,TIKU_KEY_ESCAPE); assert(!view(shell).close_confirmation && view(shell).settings_dirty);
    assert(surface_close(shell)==SURFACE_CLOSE_CONFIRM);
    click(shell,280,290); assert(!view(shell).close_confirmation && view(shell).settings_dirty);
    assert(surface_close(shell)==SURFACE_CLOSE_CONFIRM);
    click(shell,450,290); assert(view(shell).foreground==SURFACE_HOME);
    assert(!(view(shell).open_apps & (1u<<SURFACE_SETTINGS)) && !settings.saves);
    assert(surface_launch(shell,SURFACE_SETTINGS));
    assert(!view(shell).calibration.swap && !view(shell).settings_dirty);
    click(shell,80,350); click(shell,640,350);
    assert(settings.saves==1 && !view(shell).close_enabled);
    assert(surface_close(shell)==SURFACE_CLOSE_BUSY);
    assert(surface_launch(shell,SURFACE_CALCULATOR)); (void)view(shell);
    settings.status=SURFACE_SETTINGS_SAVED; (void)view(shell);
    assert(!shell->background[SURFACE_SETTINGS].state->settings_dirty);
    assert(surface_launch(shell,SURFACE_SETTINGS)); assert(!view(shell).settings_dirty);
    assert(surface_close(shell)==SURFACE_CLOSE_ACCEPTED);
    finish(&host,&shell);
}

/** @brief Failed starts retain existing tasks; shutdown drains a background lease. */
static void failures(void)
{
    surface_host_t probe;surface_config_t pc=surface_host_config(&probe);surface_t *ps=surface_create(&pc);assert(ps);
    size_t before=probe.memory.calls;assert(surface_launch(ps,SURFACE_BROWSER));(void)view(ps);
    size_t allocations=probe.memory.calls-before;finish(&probe,&ps);
    for (unsigned fail=1; fail<=allocations; ++fail) {
        surface_host_t host; surface_config_t config=surface_host_config(&host);
        surface_t *shell=surface_create(&config);
        assert(surface_launch(shell,SURFACE_CALCULATOR)); (void)view(shell); key(shell,'B');
        size_t retained=host.memory.bytes;
        host.memory.fail_at=host.memory.calls+fail;
        assert(surface_launch(shell,SURFACE_BROWSER));
        assert(view(shell).foreground==SURFACE_HOME && view(shell).status!=TIKU_RESOURCE_OK);
        assert(host.memory.bytes==retained);
        host.memory.fail_at=0;
        assert(surface_launch(shell,SURFACE_CALCULATOR)); assert(!strcmp(view(shell).hex,"B"));
        finish(&host,&shell);
    }
    surface_host_t host; surface_config_t config=surface_host_config(&host);
    surface_t *shell=surface_create(&config);
    assert(surface_launch(shell,SURFACE_BROWSER)); (void)view(shell);
    assert(surface_close(shell)==SURFACE_CLOSE_ACCEPTED);
    assert(surface_launch(shell,SURFACE_CALCULATOR)); (void)view(shell);
    assert(surface_launch(shell,SURFACE_SETTINGS)); (void)view(shell);
    surface_stop(shell); assert(!surface_destroy(&shell) && host.request.owner);
    assert(!surface_launch(shell,SURFACE_CALCULATOR));
    assert(surface_close(shell)==SURFACE_CLOSE_BUSY && !surface_resolve_close(shell,1));
    finish(&host,&shell);
}

/** @brief Overlay painting is identical for full and two-row rendering. */
static void pixels(void)
{
    static uint32_t full[800*480], rows[800*480];
    tiku_surface_t a={.px=full,.w=800,.h=480};
    tiku_surface_t b={.px=rows,.w=800,.h=480};
    for (unsigned modal=0; modal<2; ++modal) {
        surface_view_t v={.foreground=SURFACE_SETTINGS,.open_apps=14,.close_enabled=1};
        v.applications_menu=!modal; v.close_confirmation=modal;
        tiku_clip_reset(&a); surface_paint(&a,&v);
        for (int y=0;y<480;y+=2) {
            tiku_clip_set(&b,(tiku_rect_t){0,y,800,2}); surface_paint(&b,&v);
        }
        assert(!memcmp(full,rows,sizeof full));
        assert(surface_hit(&v,50,30)==0);
        assert(surface_hit(&v,SURFACE_DEVICE?40:80,350)==0);
        if (modal) {
            assert(surface_hit(&v,280,290)==ACTION_KEEP);
            assert(surface_hit(&v,450,290)==ACTION_DISCARD);
        } else assert(surface_hit(&v,300,SURFACE_TERMINAL?122:SURFACE_EDITOR?138:165)==ACTION_TASK);
    }
}

int main(int argc, char **argv)
{
    struct { const char *name; void (*run)(void); } cases[]={
        {"menu",menu},{"browser",browser},{"drafts",drafts},{"failures",failures},{"pixels",pixels}};
    unsigned ran=0;
    for (unsigned i=0;i<sizeof cases/sizeof cases[0];++i)
        if (argc==1 || (argc==2 && !strcmp(argv[1],cases[i].name))) { cases[i].run(); ++ran; }
    assert(ran); puts("Retained application sessions, close decisions and modal input passed");
    return 0;
}
