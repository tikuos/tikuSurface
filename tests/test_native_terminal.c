/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_native_terminal.c - Stomme's terminal and session over a bounded shell fixture.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "terminal.h"
#include "tiku_remote.h"
#include "tiku_event.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static tiku_remote_listener_t listener;
static int live, busy, quota, closed, interrupted;
static const char *output;
static char accepted[48];
static size_t count;

int surface_terminal_claim(void *storage)
{
    assert(storage);
    if (busy) return -1;
    live = 1;
    return 17;
}
int surface_terminal_read(int handle, void *buf, size_t size, unsigned *lost)
{
    assert(handle == 17 && lost);
    *lost = 0;
    if (!live) return -1;
    size_t n = output ? strlen(output) : 0;
    if (n > size) n = size;
    if (n) { memcpy(buf, output, n); output += n; }
    return (int)n;
}
int surface_terminal_write(int handle, const void *buf, size_t size)
{
    assert(handle == 17 && live);
    if (size > (size_t)quota) size = (size_t)quota;
    assert(count + size < sizeof accepted);
    memcpy(accepted + count, buf, size);
    count += size;
    return (int)size;
}
int surface_terminal_interrupt(int handle)
{
    assert(handle == 17);
    interrupted++;
    return 1;
}
void surface_terminal_release(int handle)
{
    assert(handle == 17);
    live = 0;
    closed++;
}

int main(void)
{
    static unsigned char tag;
    surface_terminal_bind(&listener);
    assert(surface_terminal_start());
    tiku_remote_window_t *w = &listener.session[0].win[0];
    assert(w->opened && w->frame && w->has_menus && !strcmp(w->title, "Terminal"));
    w->window = (void *)&tag;
    size_t bytes = (size_t)w->fw * w->fh * 4;
    void *snapshot = malloc(bytes);
    assert(snapshot);
    memcpy(snapshot, w->frame, bytes);
    output = "tikuOS:/> hello\r\n";
    assert(surface_terminal_poll() && memcmp(snapshot, w->frame, bytes));
    int k = w->menus.menu[0].nitem - 1;
    assert(!strcmp(w->menus.menu[0].item[k].label, "Touch keyboard"));
    tiku_remote_pick(&listener, w->window, w->menus.menu[0].item[k].command);
    tiku_event_t tap = {.type = TIKU_EVENT_POINTER_DOWN, .x = 1, .y = w->fh - 139};
    tiku_remote_event(&listener, w->window, &tap);
    assert(count == 0);
    quota = 1;
    surface_terminal_poll();
    assert(count == 1 && accepted[0] == '1');
    tiku_event_t enter = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_RETURN};
    tiku_remote_event(&listener, w->window, &enter);
    assert(count == 2 && accepted[1] == '\r');
    tiku_event_t cancel = {.type = TIKU_EVENT_KEY_DOWN, .key = 'c', .modifiers = TIKU_MOD_CMD};
    tiku_remote_event(&listener, w->window, &cancel);
    assert(interrupted == 1);
    tiku_remote_window_closed(&listener, w->window);
    surface_terminal_poll();
    assert(!live && closed == 1 && !w->opened);
    assert(surface_terminal_start());
    live = 0;
    surface_terminal_poll();
    surface_terminal_poll();
    assert(closed == 2 && !w->opened);
    busy = 1;
    assert(surface_terminal_start() && w->opened && !live);
    surface_terminal_stop();
    assert(closed == 2);
    free(snapshot);
    puts("PASS Terminal pixels, touch key, input retry, Return, interrupt, close, competing backend and owner exit");
}
