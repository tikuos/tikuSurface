/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_surface.c - behavioral, ownership and independent pixel expectations.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host.h"
#include "surface_internal.h"
#include "tiku_font.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/** @brief Take a copied view after advancing one bounded owner turn. */
static surface_view_t step(surface_t *shell)
{ surface_view_t view; surface_step(shell); surface_view(shell, &view); return view; }
/** @brief Complete due providers and advance shell ownership. */
static surface_view_t advance(surface_host_t *host, surface_t *shell)
{ host->now += 100000; (void)surface_host_poll(host); return step(shell); }
/** @brief Send a pointer press and release at an independently selected point. */
static void click(surface_t *shell, int x, int y)
{
    tiku_input_event_t event = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1, .x = x, .y = y};
    assert(surface_post(shell, &event));
    event.type = TIKU_EVENT_POINTER_UP;
    assert(surface_post(shell, &event));
    surface_step(shell);
}
/** @brief Post one key and consume it. */
static void key(surface_t *shell, unsigned value)
{
    tiku_input_event_t event = {.type = TIKU_EVENT_KEY_DOWN, .key = value};
    assert(surface_post(shell, &event));
    surface_step(shell);
}
/** @brief Retire outstanding providers before verifying the empty heap. */
static void finish(surface_host_t *host, surface_t **shell)
{
    surface_stop(*shell);
    (void)advance(host, *shell);
    assert(surface_destroy(shell));
    assert(!host->memory.live && !host->memory.bytes);
}
/** @brief Compare selected coordinates and text output to independent expectations. */
static void calculator_and_pixels(void)
{
    surface_host_t host;
    surface_config_t config = surface_host_config(&host);
    surface_t *shell = surface_create(&config);
    assert(shell);
    surface_view_t view = step(shell);
    assert(view.foreground == SURFACE_HOME);
    assert(!strcmp(view.platform_name, "TikuSurface host preview"));
    assert(!strcmp(view.media_description, "Media: deterministic read-only fixture"));
    assert(surface_render(shell, surface_host_presenter(&host)) == TIKU_PRESENT_OK);
    assert(host.frame.width == 800 && host.frame.height == 480);
    assert(host.frame.pixels[0] == 0x172b34);
    assert(host.frame.pixels[80 * 800 + 1] == 0xe9edf1);
    assert(host.frame.pixels[479 * 800 + 1] == 0xd8e1e7);
    uint32_t saved[32];
    memcpy(saved, host.frame.pixels + 200 * 800 + 32, sizeof saved);
    click(shell, 80, 200);
    key(shell, 'F'); key(shell, 'f');
    view = step(shell);
    assert(view.foreground == SURFACE_CALCULATOR && !strcmp(view.hex, "FF"));
    assert(!strcmp(view.bases[0], "FF") && !strcmp(view.bases[1], "255"));
    assert(!strcmp(view.bases[2], "377") && !strcmp(view.bases[3], "11111111"));
    assert(surface_render(shell, surface_host_presenter(&host)) == TIKU_PRESENT_BUSY);
    assert(!memcmp(saved, host.frame.pixels + 200 * 800 + 32, sizeof saved));
    assert(!tiku_framebuffer_complete(host.frame.owner, host.frame.token + 1, TIKU_PRESENT_OK));
    (void)advance(&host, shell);
    assert(surface_render(shell, surface_host_presenter(&host)) == TIKU_PRESENT_OK);
    assert(host.frame.pixels[121 * 800 + 33] == 0xffffff);
    assert(host.frame.pixels[193 * 800 + 33] == 0xe9edf1);
    key(shell, 8);
    view = step(shell); assert(!strcmp(view.hex, "F"));
    click(shell, 620, 390);
    for (unsigned i = 0; i < 20; ++i) key(shell, 'F');
    view = step(shell);
    assert(!strcmp(view.hex, "FFFFFFFFFFFFFFFF"));
    assert(!strcmp(view.bases[1], "18446744073709551615") && view.status == TIKU_RESOURCE_LIMIT);
    key(shell, '?'); assert(step(shell).status == TIKU_RESOURCE_INVALID);
    key(shell, 8); assert(step(shell).status == TIKU_RESOURCE_OK);
    click(shell, 50, 30);
    view = step(shell); assert(view.foreground == SURFACE_HOME);
    key(shell, 27); key(shell, 27);
    assert(!surface_launch(shell, (surface_app_t)99));
    assert(step(shell).foreground == SURFACE_HOME);
    finish(&host, &shell);
}
/** @brief A press changes pixels inside its control and dragging out cancels activation. */
static void control_pixels(void)
{
    static uint32_t reference[800 * 480];
    surface_host_t host;
    surface_config_t config = surface_host_config(&host);
    surface_t *shell = surface_create(&config);
    (void)step(shell);
    assert(surface_render(shell, surface_host_presenter(&host)) == TIKU_PRESENT_OK);
    memcpy(reference, host.frame.pixels, sizeof reference);
    (void)advance(&host, shell);
    tiku_input_event_t event = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1, .x = 80, .y = 200};
    assert(surface_post(shell, &event)); (void)step(shell);
    assert(surface_render(shell, surface_host_presenter(&host)) == TIKU_PRESENT_OK);
    unsigned changed = 0;
    for (unsigned y = 0; y < 480; ++y) for (unsigned x = 0; x < 800; ++x) {
        if (reference[y * 800 + x] == host.frame.pixels[y * 800 + x]) continue;
        assert(x >= 32 && x < 264 && y >= 160 && y < 304);
        ++changed;
    }
    assert(changed > 100);
    (void)advance(&host, shell);
    event.type = TIKU_EVENT_POINTER_MOVE; event.x = 799; event.y = 400;
    assert(surface_post(shell, &event));
    event.type = TIKU_EVENT_POINTER_UP; assert(surface_post(shell, &event));
    assert(step(shell).foreground == SURFACE_HOME);
    assert(surface_render(shell, surface_host_presenter(&host)) == TIKU_PRESENT_OK);
    assert(!memcmp(reference, host.frame.pixels, sizeof reference));
    click(shell, 80, 200);
    click(shell, 60, 216);
    click(shell, 240, 328);
    click(shell, 330, 384);
    assert(!strcmp(step(shell).hex, "0AF"));
    assert(!strcmp(step(shell).bases[1], "175"));
    finish(&host, &shell);
}
/** @brief Verify directories, pages, file reads, parent navigation and stale media. */
static void browser(void)
{
    surface_host_t host;
    surface_config_t config = surface_host_config(&host);
    surface_t *shell = surface_create(&config);
    assert(surface_launch(shell, SURFACE_BROWSER));
    surface_view_t view = step(shell);
    assert(view.status == TIKU_RESOURCE_PENDING && host.request.owner);
    assert(host.request.request->capacity == 16);
    view = advance(&host, shell);
    assert(view.status == TIKU_RESOURCE_OK && view.row_count == 6 && view.more);
    assert(!strcmp(view.rows[0], "docs") && view.directory[0]);
    assert(!strcmp(view.rows[1], "sample-01.txt"));
    click(shell, 70, 130);
    view = advance(&host, shell);
    assert(!strcmp(view.path, "/docs") && view.row_count == 2);
    assert(!strcmp(view.rows[0], "guide.txt"));
    click(shell, 70, 130);
    view = advance(&host, shell);
    assert(view.reading && !strncmp(view.preview, "TikuSurface fixture file.", 24));
    key(shell, 27); view = advance(&host, shell);
    assert(!view.reading && !strcmp(view.path, "/docs"));
    key(shell, 27); view = advance(&host, shell);
    assert(!strcmp(view.path, "/") && view.row_count == 6);
    click(shell, 650, 410); view = step(shell);
    assert(!strcmp(view.rows[0], "sample-06.txt"));
    click(shell, 650, 410); view = step(shell);
    assert(!strcmp(view.rows[0], "sample-12.txt") && view.row_count == 4);
    click(shell, 650, 410); view = advance(&host, shell);
    assert(!strcmp(view.rows[0], "sample-16.txt") && view.row_count == 6 && !view.more);
    click(shell, 480, 410); view = advance(&host, shell);
    assert(!strcmp(view.rows[0], "sample-12.txt") && view.previous);
    click(shell, 480, 410); view = step(shell);
    assert(!strcmp(view.rows[0], "sample-06.txt"));
    click(shell, 650, 410); click(shell, 650, 410); view = advance(&host, shell);
    assert(!strcmp(view.rows[0], "sample-16.txt"));
    click(shell, 70, 130);
    ++host.epoch;
    view = advance(&host, shell);
    assert(view.status == TIKU_RESOURCE_STALE_CURSOR && !view.preview[0]);
    click(shell, 70, 410); view = advance(&host, shell);
    assert(view.status == TIKU_RESOURCE_OK && !strcmp(view.rows[0], "docs"));
    click(shell, 70, 130);
    assert(host.request.owner);
    key(shell, 27);
    assert(host.request.cancelled);
    (void)advance(&host, shell);
    view = advance(&host, shell);
    assert(!strcmp(view.path, "/") && view.row_count == 6);
    host.unavailable = 1;
    click(shell, 70, 410); view = step(shell);
    assert(view.status == TIKU_RESOURCE_UNAVAILABLE);
    click(shell, 50, 30); assert(step(shell).foreground == SURFACE_HOME);
    finish(&host, &shell);
}
/** @brief Home cancels a pending launch while late completion retains app memory. */
static void retirement(void)
{
    surface_host_t host;
    surface_config_t config = surface_host_config(&host);
    surface_t *shell = surface_create(&config);
    size_t baseline = host.memory.bytes;
    assert(surface_launch(shell, SURFACE_BROWSER));
    (void)step(shell);
    tiku_paged_storage_t *old = host.request.owner;
    uint64_t token = host.request.request->token;
    assert(surface_launch(shell, SURFACE_CALCULATOR));
    surface_view_t view = step(shell);
    assert(view.foreground == SURFACE_HOME && view.retiring && view.pending == SURFACE_CALCULATOR);
    assert(host.request.owner == old && host.memory.bytes > baseline);
    assert(!tiku_paged_storage_complete(old, token + 1, TIKU_RESOURCE_OK, 0, 1, 1));
    size_t retained = host.memory.bytes;
    for (int i = 0; i < 100; ++i) assert(surface_launch(shell, i % 2 ? SURFACE_BROWSER : SURFACE_SETTINGS));
    (void)step(shell);
    assert(host.cancellations == 1 && host.memory.bytes == retained);
    click(shell, 50, 30);
    assert(tiku_paged_storage_complete(old, token, TIKU_RESOURCE_OK, 0, host.epoch, 1));
    host.request.owner = NULL;
    host.request.request = NULL;
    view = advance(&host, shell);
    assert(!view.retiring && view.foreground == SURFACE_HOME && host.memory.bytes == baseline);
    assert(surface_launch(shell, SURFACE_BROWSER)); (void)step(shell);
    assert(surface_launch(shell, SURFACE_SETTINGS)); (void)step(shell);
    view = advance(&host, shell);
    assert(!view.retiring && view.foreground == SURFACE_SETTINGS);
    finish(&host, &shell);
}
/** @brief Fail each shell and browser allocation and check recovery to the baseline. */
static void allocation_failures(void)
{
    surface_host_t probe;
    surface_config_t config = surface_host_config(&probe);
    surface_t *shell = surface_create(&config);
    size_t creates = probe.memory.calls;
    finish(&probe, &shell);
    for (size_t i = 1; i <= creates; ++i) {
        surface_host_t host;
        config = surface_host_config(&host); host.memory.fail_at = i;
        shell = surface_create(&config);
        assert(!shell && !host.memory.live && !host.memory.bytes);
    }
    config = surface_host_config(&probe); shell = surface_create(&config);
    size_t before = probe.memory.calls;
    assert(surface_launch(shell, SURFACE_BROWSER)); (void)step(shell);
    size_t launches = probe.memory.calls - before;
    finish(&probe, &shell);
    for (size_t i = 1; i <= launches; ++i) {
        surface_host_t host;
        config = surface_host_config(&host); shell = surface_create(&config);
        size_t baseline = host.memory.bytes;
        host.memory.fail_at = host.memory.calls + i;
        assert(surface_launch(shell, SURFACE_BROWSER));
        surface_view_t view = step(shell);
        assert(view.foreground == SURFACE_HOME && view.status != TIKU_RESOURCE_OK);
        assert(host.memory.bytes == baseline && !host.request.owner);
        host.memory.fail_at = 0;
        assert(surface_launch(shell, SURFACE_CALCULATOR));
        assert(step(shell).foreground == SURFACE_CALCULATOR);
        finish(&host, &shell);
    }
    printf("allocation failure positions: shell=%zu browser=%zu\n", creates, launches);
}
/** @brief Overflow and focus loss cannot activate a pressed launcher control. */
static void input_and_faults(void)
{
    surface_host_t host;
    surface_config_t config = surface_host_config(&host);
    surface_t *shell = surface_create(&config);
    tiku_input_event_t event = {.type = TIKU_EVENT_POINTER_DOWN, .x = 80, .y = 200, .button = 1};
    assert(surface_post(shell, &event)); (void)step(shell);
    event.type = TIKU_EVENT_POINTER_MOVE;
    for (unsigned i = 0; i < 8; ++i) assert(surface_post(shell, &event));
    assert(!surface_post(shell, &event));
    (void)step(shell);
    event.type = TIKU_EVENT_POINTER_UP;
    assert(surface_post(shell, &event));
    assert(step(shell).foreground == SURFACE_HOME);
    event.type = TIKU_EVENT_POINTER_DOWN; assert(surface_post(shell, &event)); (void)step(shell);
    event.type = TIKU_EVENT_DEACTIVATED; assert(surface_post(shell, &event)); (void)step(shell);
    event.type = TIKU_EVENT_POINTER_UP; assert(surface_post(shell, &event));
    assert(step(shell).foreground == SURFACE_HOME);
    assert(surface_launch(shell, SURFACE_CALCULATOR)); (void)step(shell);
    key(shell, 'B');
    event = (tiku_input_event_t){.type = TIKU_EVENT_KEY_DOWN, .key = 'A'};
    for (unsigned i = 0; i < 8; ++i) assert(surface_post(shell, &event));
    assert(!strcmp(step(shell).hex, "B"));
    assert(surface_post(shell, &event));
    assert(surface_launch(shell, SURFACE_HOME));
    assert(surface_launch(shell, SURFACE_CALCULATOR));
    assert(!step(shell).hex[0]);
    host.now = 100; assert(!step(shell).clock_failed);
    host.now = 99; assert(step(shell).clock_failed);
    host.now = 101; assert(!step(shell).clock_failed);
    host.present_error = 1;
    assert(surface_render(shell, surface_host_presenter(&host)) == TIKU_PRESENT_ERROR);
    assert(step(shell).display_failed);
    host.present_error = 0;
    host.present_busy = 1;
    assert(surface_render(shell, surface_host_presenter(&host)) == TIKU_PRESENT_BUSY);
    host.present_busy = 0;
    assert(surface_render(shell, surface_host_presenter(&host)) == TIKU_PRESENT_OK);
    assert(tiku_framebuffer_complete(host.frame.owner, host.frame.token, TIKU_PRESENT_ERROR));
    host.frame = (tiku_present_view_t){0};
    assert(step(shell).display_failed);
    assert(surface_render(shell, surface_host_presenter(&host)) == TIKU_PRESENT_OK);
    (void)advance(&host, shell);
    unsigned presented = host.submissions;
    assert(surface_render(shell, surface_host_presenter(&host)) == TIKU_PRESENT_OK);
    assert(host.submissions == presented + 1 && !step(shell).display_failed);
    surface_stop(shell);
    assert(!surface_destroy(&shell));
    assert(!surface_post(shell, &event) && !surface_launch(shell, SURFACE_CALCULATOR));
    finish(&host, &shell);
}
/** @brief Repeated switches keep one request, bounded retirement and no retained bytes. */
static void endurance(void)
{
    surface_host_t host;
    surface_config_t config = surface_host_config(&host);
    surface_t *shell = surface_create(&config);
    size_t baseline = host.memory.bytes;
    for (unsigned i = 0; i < 1000; ++i) {
        assert(surface_launch(shell, SURFACE_BROWSER)); (void)step(shell);
        assert(host.request.owner);
        assert(surface_launch(shell, SURFACE_CALCULATOR));
        surface_view_t view = step(shell);
        assert(view.retiring && view.foreground == SURFACE_HOME);
        assert(surface_launch(shell, SURFACE_SETTINGS));
        view = advance(&host, shell);
        assert(view.foreground == SURFACE_SETTINGS && !view.retiring);
        assert(surface_launch(shell, SURFACE_CALCULATOR)); (void)step(shell);
        key(shell, 'A'); key(shell, 'B');
        assert(!strcmp(step(shell).bases[1], "171"));
        assert(surface_launch(shell, SURFACE_HOME)); (void)step(shell);
        assert(host.memory.bytes == baseline && !host.request.owner);
    }
    assert(host.memory.peak - baseline < 262144);
    printf("1000 retirement cycles; shell=%zu bytes; app peak=%zu bytes\n", baseline, host.memory.peak - baseline);
    finish(&host, &shell);
}
/** @brief Sliced painting retains a copied view and submits complete frames. */
static void sliced_paint(void)
{
    surface_host_t host, reference;
    surface_config_t config = surface_host_config(&host);
    config.paint_rows = 481;
    assert(!surface_create(&config) && !host.memory.live);
    config.paint_rows = 7;
    unsigned turns = (480 + config.paint_rows - 1) / config.paint_rows;
    surface_t *shell = surface_create(&config);
    surface_config_t other = surface_host_config(&reference);
    surface_t *full = surface_create(&other);
    assert(shell && full);
    for (unsigned app = 0; app <= 3; ++app) {
        assert(surface_launch(shell, (surface_app_t)app));
        assert(surface_launch(full, (surface_app_t)app));
        (void)advance(&host, shell); (void)advance(&reference, full);
        (void)advance(&host, shell); (void)advance(&reference, full);
        assert(surface_render(full, surface_host_presenter(&reference)) == TIKU_PRESENT_OK);
        unsigned submissions = host.submissions;
        for (unsigned turn = 0; turn < turns; ++turn) {
            tiku_present_result_t result = surface_render(shell, surface_host_presenter(&host));
            assert(result == (turn + 1 == turns ? TIKU_PRESENT_OK : TIKU_PRESENT_BUSY));
            assert(host.submissions == submissions + (turn + 1 == turns));
        }
        assert(!memcmp(host.frame.pixels, reference.frame.pixels, 800 * 480 * sizeof(uint32_t)));
    }
    (void)advance(&host, shell); (void)advance(&reference, full);
    assert(surface_launch(shell, SURFACE_HOME)); (void)step(shell);
    assert(surface_launch(full, SURFACE_HOME)); (void)step(full);
    assert(surface_render(full, surface_host_presenter(&reference)) == TIKU_PRESENT_OK);
    assert(surface_render(shell, surface_host_presenter(&host)) == TIKU_PRESENT_BUSY);
    assert(surface_launch(shell, SURFACE_CALCULATOR)); (void)step(shell);
    for (unsigned i = 1; i < turns; ++i)
        assert(surface_render(shell, surface_host_presenter(&host)) == (i + 1 == turns ? TIKU_PRESENT_OK : TIKU_PRESENT_BUSY));
    assert(!memcmp(host.frame.pixels, reference.frame.pixels, 800 * 480 * sizeof(uint32_t)));
    assert(surface_render(shell, surface_host_presenter(&host)) == TIKU_PRESENT_BUSY);
    (void)advance(&host, shell); (void)advance(&reference, full);
    assert(surface_launch(full, SURFACE_CALCULATOR)); (void)step(full);
    assert(surface_render(full, surface_host_presenter(&reference)) == TIKU_PRESENT_OK);
    for (unsigned i = 0; i < turns; ++i)
        assert(surface_render(shell, surface_host_presenter(&host)) == (i + 1 == turns ? TIKU_PRESENT_OK : TIKU_PRESENT_BUSY));
    assert(!memcmp(host.frame.pixels, reference.frame.pixels, 800 * 480 * sizeof(uint32_t)));
    (void)advance(&host, shell);
    assert(surface_launch(shell, SURFACE_HOME)); (void)step(shell);
    assert(surface_render(shell, surface_host_presenter(&host)) == TIKU_PRESENT_BUSY);
    finish(&host, &shell);
    finish(&reference, &full);
}

/** @brief Service changes repaint Device information while partial frames keep their prior label. */
static void service_labels(void)
{
    surface_host_t host,reference;
    surface_config_t config=surface_host_config(&host),other=surface_host_config(&reference);
    char label[96]="Wi-Fi idle",other_label[96]="Wi-Fi idle";
    config.services_description=label;other.services_description=other_label;
    config.paint_rows=7;
    surface_t *shell=surface_create(&config),*full=surface_create(&other);
    assert(shell && full && surface_launch(shell,SURFACE_SETTINGS) && surface_launch(full,SURFACE_SETTINGS));
    (void)advance(&host,shell);(void)advance(&reference,full);
    assert(surface_render(full,surface_host_presenter(&reference))==TIKU_PRESENT_OK);
    assert(surface_render(shell,surface_host_presenter(&host))==TIKU_PRESENT_BUSY);
    strcpy(label,"Wi-Fi joined; transfer listening");surface_services_changed(shell);
    unsigned turns=(480+6)/7;
    for(unsigned i=1;i<turns;i++)
        assert(surface_render(shell,surface_host_presenter(&host))==(i+1==turns?TIKU_PRESENT_OK:TIKU_PRESENT_BUSY));
    assert(!memcmp(host.frame.pixels,reference.frame.pixels,800*480*sizeof(uint32_t)));
    (void)advance(&host,shell);(void)advance(&reference,full);
    strcpy(other_label,label);surface_services_changed(full);
    assert(surface_render(full,surface_host_presenter(&reference))==TIKU_PRESENT_OK);
    for(unsigned i=0;i<turns;i++)
        assert(surface_render(shell,surface_host_presenter(&host))==(i+1==turns?TIKU_PRESENT_OK:TIKU_PRESENT_BUSY));
    assert(!memcmp(host.frame.pixels,reference.frame.pixels,800*480*sizeof(uint32_t)));
    (void)advance(&host,shell);assert(!surface_render_pending(shell));
    surface_services_changed(shell);assert(surface_render_pending(shell));
    finish(&host,&shell);finish(&reference,&full);surface_services_changed(NULL);
}

/** @brief Wide media names retain the type column and draw a visible truncation suffix. */
static void media_text_bounds(void)
{
    static uint32_t actual[800*480],expected[800*480];
    tiku_surface_t a={.px=actual,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=expected;
    surface_view_t view={.foreground=SURFACE_BROWSER,.row_count=1,.status=TIKU_RESOURCE_OK};
    strcpy(view.path,"/");surface_paint(&b,&view);
    memset(view.rows[0],'@',63);view.rows[0][63]=0;
    surface_paint(&a,&view);
    for(unsigned y=112;y<152;y++)
        assert(!memcmp(actual+y*800+680,expected+y*800+680,(800-680)*sizeof *actual));
    char fitted[55];memset(fitted,'@',51);memcpy(fitted+51,"...",4);
    assert(tiku_text_width(tiku_font_plain(),fitted)<=632);
    tiku_text(&b,tiku_font_plain(),48,137,fitted,0x20343f);
    for(unsigned y=112;y<152;y++)assert(!memcmp(actual+y*800,expected+y*800,800*sizeof *actual));
    assert(strlen(view.rows[0])==63&&view.rows[0][62]=='@');
    view.row_count=0;view.path[0]=0;surface_paint(&b,&view);
    memset(view.path,'@',63);view.path[63]=0;surface_paint(&a,&view);
    for(unsigned y=70;y<110;y++)
        assert(!memcmp(actual+y*800+768,expected+y*800+768,32*sizeof *actual));
    view.path[0]=0;view.reading=1;surface_paint(&b,&view);
    memset(view.preview,'@',90);view.preview[90]=0;surface_paint(&a,&view);
    for(unsigned y=146;y<170;y++)
        assert(!memcmp(actual+y*800+768,expected+y*800+768,32*sizeof *actual));
    char preview[64];memset(preview,'@',60);memcpy(preview+60,"...",4);
    tiku_text(&b,tiku_font_plain(),32,160,preview,0x20343f);
    for(unsigned y=146;y<170;y++)assert(!memcmp(actual+y*800,expected+y*800,800*sizeof *actual));
}

int main(int argc, char **argv)
{
    struct { const char *name; void (*run)(void); } cases[] = {
        {"pixels", calculator_and_pixels}, {"controls", control_pixels}, {"browser", browser}, {"retirement", retirement},
        {"allocation", allocation_failures}, {"input", input_and_faults}, {"endurance", endurance}, {"sliced", sliced_paint}, {"services", service_labels}, {"media-text",media_text_bounds}
    };
    unsigned ran = 0;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        if (argc == 1 || (argc == 2 && !strcmp(argv[1], cases[i].name))) {
            cases[i].run();
            ++ran;
        }
    }
    if (!ran) return 2;
    puts("TikuSurface host ownership, input, pixels and failures passed");
    return 0;
}
