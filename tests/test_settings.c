/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_settings.c - independent calibration record and save-control checks.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host.h"
#include "settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static surface_touch_map_t stored = {0,799,0,479,0,0,0};
static unsigned saves, state = SURFACE_SETTINGS_DEFAULT;
/** @brief Return the fixture's accepted record and save state. */
static unsigned read_settings(void *ctx, surface_touch_map_t *map) { (void)ctx; *map = stored; return state; }
/** @brief Capture an explicit save request for later completion. */
static int save_settings(void *ctx, const surface_touch_map_t *map)
{ (void)ctx; ++saves; stored = *map; state = SURFACE_SETTINGS_PENDING; return 1; }
/** @brief Activate one independently selected settings control. */
static void click(surface_t *shell, int x, int y)
{
    tiku_input_event_t e = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1, .x = x, .y = y};
    assert(surface_post(shell, &e)); e.type = TIKU_EVENT_POINTER_UP; assert(surface_post(shell, &e));
    surface_step(shell);
}
/** @brief Reject truncated/corrupt records and verify that axis edits require explicit Save. */
int main(void)
{
    unsigned char bytes[SURFACE_SETTINGS_BYTES], copy[SURFACE_SETTINGS_BYTES];
    surface_touch_map_t map = {12,4095,24,2047,1,0,1}, decoded = {0};
    assert(surface_settings_encode(&map, bytes));
    assert(!memcmp(bytes,"TSCL\1\40\40\3\340\1\14\0\377\17\30\0\377\7\1\0\1",21));
    assert(surface_settings_decode(bytes,sizeof bytes,&decoded)==SURFACE_SETTINGS_SAVED);
    assert(decoded.x_min==12 && decoded.x_max==4095 && decoded.y_min==24 && decoded.y_max==2047 && decoded.swap && !decoded.invert_x && decoded.invert_y);
    for (unsigned i=0;i<sizeof bytes;++i) {
        memcpy(copy,bytes,sizeof copy); copy[i]^=1;
        assert(surface_settings_decode(copy,sizeof copy,&decoded)==SURFACE_SETTINGS_CORRUPT);
        assert(decoded.x_min==12);
        assert(surface_settings_decode(bytes,i,&decoded)==SURFACE_SETTINGS_CORRUPT);
    }
    assert(!surface_settings_encode(&(surface_touch_map_t){0},bytes));
    /* The store answers 0 for a record, 1 for none and -1 for an error, on
     * reads and on writes: a record read is decoded, and only then does the
     * map change. */
    surface_touch_map_t kept = {1,2,3,4,0,0,0}, loaded = kept;
    assert(surface_settings_encode(&map, bytes));
    assert(surface_settings_load(1,bytes,sizeof bytes,&loaded)==SURFACE_SETTINGS_DEFAULT);
    assert(surface_settings_load(-1,bytes,sizeof bytes,&loaded)==SURFACE_SETTINGS_UNAVAILABLE);
    assert(!memcmp(&loaded,&kept,sizeof kept));
    memcpy(copy,bytes,sizeof bytes); copy[10]^=1;
    assert(surface_settings_load(0,copy,sizeof copy,&loaded)==SURFACE_SETTINGS_CORRUPT);
    assert(!memcmp(&loaded,&kept,sizeof kept));
    assert(surface_settings_load(0,bytes,sizeof bytes,&loaded)==SURFACE_SETTINGS_SAVED);
    assert(!memcmp(&loaded,&map,sizeof map));
    assert(surface_settings_stored(0)==SURFACE_SETTINGS_SAVED);
    assert(surface_settings_stored(-1)==SURFACE_SETTINGS_FAILED);
    /* A calibration page hands back the positions the map reports at the
     * screen's edges.  On the default map they are the new bounds; on a
     * map already right they change nothing. */
    surface_touch_map_t plain = {0,799,0,479,0,0,0}, refit = kept;
    assert(surface_touch_map_refit(&plain,(int[4]){17,797,17,480},&refit));
    assert(refit.x_min==17 && refit.x_max==797 && refit.y_min==17 && refit.y_max==480 &&
        !refit.swap && !refit.invert_x && !refit.invert_y);
    plain = refit;
    assert(surface_touch_map_refit(&plain,(int[4]){0,799,0,479},&refit));
    assert(!memcmp(&refit,&plain,sizeof plain));
    /* A controller twice the screen's size: the edges scale with it, an
     * inverted axis is corrected from its far end, and a swapped map takes
     * the screen's x from the controller's y. */
    plain = (surface_touch_map_t){0,1598,0,958,0,0,0};
    assert(surface_touch_map_refit(&plain,(int[4]){10,789,5,474},&refit));
    assert(refit.x_min==20 && refit.x_max==1578 && refit.y_min==10 && refit.y_max==948);
    plain.invert_x = 1;
    assert(surface_touch_map_refit(&plain,(int[4]){10,789,0,479},&refit));
    assert(refit.x_min==20 && refit.x_max==1578 && refit.invert_x==1);
    plain = (surface_touch_map_t){0,479,0,799,1,0,0};
    assert(surface_touch_map_refit(&plain,(int[4]){17,797,0,479},&refit));
    assert(refit.y_min==17 && refit.y_max==797 && refit.x_min==0 && refit.x_max==479 && refit.swap==1);
    /* Edges out of order are no calibration, and the map is left alone. */
    refit = kept;
    assert(!surface_touch_map_refit(&plain,(int[4]){400,400,0,479},&refit));
    assert(!surface_touch_map_refit(&plain,(int[4]){0,799,300,200},&refit));
    assert(!memcmp(&refit,&kept,sizeof kept));
    surface_host_t host; surface_config_t config=surface_host_config(&host);
    config.settings=(surface_settings_provider_t){NULL,read_settings,save_settings};
    surface_t *shell=surface_create(&config); assert(shell);
    assert(surface_launch(shell,SURFACE_SETTINGS)); surface_step(shell);
    click(shell,60,350); click(shell,250,350); assert(!saves);
    surface_view_t view; surface_view(shell,&view); assert(view.calibration.swap && view.calibration.invert_x);
    click(shell,650,350); assert(saves==1 && stored.swap && stored.invert_x);
    state=SURFACE_SETTINGS_FAILED; surface_step(shell); surface_view(shell,&view);
    assert(view.settings_status==SURFACE_SETTINGS_FAILED);
    surface_stop(shell); assert(surface_destroy(&shell)); assert(!host.memory.live);
    puts("settings: fixed record, corruption, explicit save and failure status pass");
    return 0;
}
