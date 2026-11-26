/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * terminal.c - Stomme Terminal on a local window session and a TikuOS shell.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "terminal.h"
#include "bridge_os.h"
#include "../platform/tikuos/console_io.h"
#include "tiku_app.h"
#include "tiku_ui.h"
#include "../shell/touch_key.h"
#include "tiku_remote.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern const tiku_app_descriptor_t tiku_term_app;
static tiku_remote_listener_t *listener;
static tiku_remote_session_t *session;
static void *app;
static int closing, changed;
static _Alignas(16) unsigned char capture[SURFACE_CONSOLE_BRIDGE_BYTES];
static unsigned char input[2048];
static size_t queued;
static int refused;
static unsigned lost_output;
static const uint32_t *terminal_pixels;
static int width, height, keyboard, shift, symbols;
enum { KEYBOARD_COMMAND = 0x7070, CHARACTER_KEYS = 40, EXTRA_KEYS = 7 };

int tiku_term_stream_open(void)
{
    queued = 0;
    refused = 0;
    lost_output = 0;
    return surface_terminal_claim(capture);
}

static void flush_input(int handle)
{
    if (!queued) return;
    size_t offered = queued < 256 ? queued : 256;
    int n = surface_terminal_write(handle, input, offered);
    if (n > 0) {
        queued -= (size_t)n;
        memmove(input, input + n, queued);
    }
}

void tiku_term_stream_write(int handle, const char *bytes, size_t length)
{
    if (length == 1 && bytes[0] == 3) {
        queued = 0;
        refused = !surface_terminal_interrupt(handle);
        return;
    }
    if (length > sizeof input - queued) {
        refused = 1;
        return;
    }
    memcpy(input + queued, bytes, length);
    queued += length;
    flush_input(handle);
}

int tiku_term_stream_read(int handle, char *bytes, size_t size)
{
    flush_input(handle);
    if (size >= 96 && (refused || lost_output)) {
        int n = snprintf(bytes, size, "\r\n[Terminal: %s%u output bytes lost]\r\n",
                         refused ? "input queue full, " : "", lost_output);
        refused = 0;
        lost_output = 0;
        return n;
    }
    int n = surface_terminal_read(handle, bytes, size < 256 ? size : 256, &lost_output);
    if (n < 0) {
        closing = 1;
        return 0;
    }
    if (n) return n;
    errno = EAGAIN;
    return -1;
}

void tiku_term_stream_close(int handle)
{
    queued = 0;
    surface_terminal_release(handle);
}

static uint32_t open_window(void *ctx, const char *title, int w, int h)
{
    struct {
        uint32_t type, length, id;
        int32_t w, h;
        char title[TIKU_REMOTE_TITLE];
    } packet = {TIKU_RMSG_OPEN, 12 + TIKU_REMOTE_TITLE, 1, w, h, {0}};
    (void)ctx;
    snprintf(packet.title, sizeof packet.title, "%s", title);
    return tiku_remote_receive(session, (unsigned char *)&packet, sizeof packet) == 1 ? 1 : 0;
}

static tiku_rect_t key_rect(int key)
{
    int row = key < CHARACTER_KEYS ? key / 10 : 4;
    int columns = row == 4 ? EXTRA_KEYS : 10;
    int col = row == 4 ? key - CHARACTER_KEYS : key % 10;
    int left = col * width / columns;
    return (tiku_rect_t){left, height - (5 - row) * 28,
                         (col + 1) * width / columns - left, 28};
}

static void paint_keyboard(tiku_remote_window_t *window)
{
    static const char *const labels[] = {"Shift", "Symbols", "Space", "Erase", "Enter", "^C", "Hide"};
    tiku_surface_t surface = {.px = window->frame, .w = width, .h = height,
        .scale = 1, .clip = {0, 0, width, height}};
    for (int i = 0; i < CHARACTER_KEYS + EXTRA_KEYS; i++) {
        char label[2] = {0};
        const char *text;
        if (i < CHARACTER_KEYS) {
            label[0] = surface_touch_key(i, symbols, shift);
            text = label;
        } else text = labels[i - CHARACTER_KEYS];
        tiku_ui_button(&surface, key_rect(i), text,
                       ((i == 40 && shift) || (i == 41 && symbols)) ? TIKU_S_PRESSED : 0);
    }
}

static int frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    (void)ctx;
    int published = tiku_remote_frame_copy(session, id, w, h, px);
    if (published) {
        terminal_pixels = px;
        width = w;
        height = h;
        if (keyboard) paint_keyboard(&session->win[0]);
    }
    changed |= published;
    return published;
}

static int menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{
    struct {
        uint32_t type, length, id;
        tiku_menuset_t set;
    } packet = {TIKU_RMSG_MENUS, 4 + sizeof *set, id, *set};
    (void)ctx;
    int i = packet.set.menu[0].nitem;
    if (i < (int)(sizeof packet.set.menu[0].item / sizeof packet.set.menu[0].item[0])) {
        snprintf(packet.set.menu[0].item[i].label, sizeof packet.set.menu[0].item[i].label,
                 "Touch keyboard");
        packet.set.menu[0].item[i].command = KEYBOARD_COMMAND;
        packet.set.menu[0].item[i].enabled = 1;
        packet.set.menu[0].nitem++;
    }
    return tiku_remote_receive(session, (unsigned char *)&packet, sizeof packet) == 1;
}

static int touch_key(const tiku_event_t *event)
{
    if (!keyboard || event->type != TIKU_EVENT_POINTER_DOWN) return 0;
    for (int i = 0; i < CHARACTER_KEYS + EXTRA_KEYS; i++) {
        tiku_rect_t r = key_rect(i);
        if (event->x < r.x || event->x >= r.x + r.w ||
            event->y < r.y || event->y >= r.y + r.h) continue;
        tiku_event_t key = {.type = TIKU_EVENT_KEY_DOWN};
        switch (i) {
        case 40: shift = !shift; break;
        case 41: symbols = !symbols; break;
        case 42: key.key = ' '; break;
        case 43: key.key = TIKU_KEY_BACKSPACE; break;
        case 44: key.key = TIKU_KEY_RETURN; break;
        case 45: key.key = 'c'; key.modifiers = TIKU_MOD_CMD; break;
        case 46: keyboard = 0; break;
        default:
            key.key = (unsigned char)surface_touch_key(i, symbols, shift);
            key.text[0] = (char)key.key;
            break;
        }
        if (key.key) tiku_term_app.event(app, &key);
        if (!key.key && terminal_pixels) frame(NULL, 1, terminal_pixels, width, height);
        return 1;
    }
    return 0;
}

static int dispatch(void *ctx, uint32_t type, const void *bytes, uint32_t length)
{
    uint32_t id;
    (void)ctx;
    if (!app || !bytes || length < sizeof id || closing) return 0;
    memcpy(&id, bytes, sizeof id);
    if (id != 1) return 0;
    const unsigned char *payload = (const unsigned char *)bytes + sizeof id;
    if (type == TIKU_RMSG_EVENT && length == 4 + sizeof(tiku_event_t)) {
        tiku_event_t e;
        memcpy(&e, payload, sizeof e);
        if (!touch_key(&e)) closing = tiku_term_app.event(app, &e);
    } else if (type == TIKU_RMSG_PICK && length == 8) {
        int32_t command;
        memcpy(&command, payload, 4);
        if (command == KEYBOARD_COMMAND) {
            keyboard = !keyboard;
            if (terminal_pixels) frame(NULL, id, terminal_pixels, width, height);
        } else closing = tiku_term_app.pick(app, id, command);
    } else if (type == TIKU_RMSG_CLOSED && length == 4) {
        closing = 1;
    } else return 0;
    return 1;
}

void surface_terminal_bind(tiku_remote_listener_t *owner) { listener = owner; }

int surface_terminal_start(void)
{
    static const tiku_app_services_t services = {
        .open = open_window, .frame = frame, .menus = menus
    };
    if (!listener) return 0;
    if (app && !closing) {
        uint32_t raise[] = {TIKU_RMSG_RAISE, 4, 1};
        changed |= tiku_remote_receive(session, (unsigned char *)raise, sizeof raise) == 1;
        return 1;
    }
    if (session) return 0;
    session = tiku_remote_attach(listener, dispatch, NULL);
    if (!session) return 0;
    snprintf(session->name, sizeof session->name, "Terminal");
    session->features = TIKU_FEAT_TEXT_V2;
    if (tiku_term_app.start(&app, &services) != 0) {
        tiku_remote_detach(session);
        session = NULL;
        return 0;
    }
    changed = 1;
    return 1;
}

void surface_terminal_stop(void)
{
    if (app) tiku_term_app.stop(app);
    if (session) tiku_remote_detach(session);
    app = NULL;
    session = NULL;
    closing = keyboard = shift = symbols = 0;
    terminal_pixels = NULL;
    changed = 1;
}

int surface_terminal_poll(void)
{
    if (closing) surface_terminal_stop();
    else if (app) tiku_term_app.tick(app, 0);
    int result = changed;
    changed = 0;
    return result;
}
