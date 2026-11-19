/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * main.c - X11 host preview and deterministic frame captures.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/** @brief Complete fixture work and advance one shell turn. */
static void advance(surface_host_t *host, surface_t *shell)
{ host->now += 100000; (void)surface_host_poll(host); surface_step(shell); }

/** @brief Capture four deterministic app views without opening a native display. */
static int capture(surface_host_t *host, surface_t *shell, const char *directory)
{
    static const char *names[] = {"home", "calculator", "browser", "settings"};
    for (int i = 0; i < 4; ++i) {
        if (!surface_launch(shell, (surface_app_t)i)) return 0;
        surface_step(shell);
        advance(host, shell);
        if (i == SURFACE_CALCULATOR) {
            tiku_input_event_t key = {.type = TIKU_EVENT_KEY_DOWN, .key = 'F'};
            (void)surface_post(shell, &key); surface_step(shell);
            (void)surface_post(shell, &key); surface_step(shell);
        }
        if (surface_render(shell, surface_host_presenter(host)) != TIKU_PRESENT_OK) return 0;
        char path[1024];
        int length = snprintf(path, sizeof path, "%s/%s.ppm", directory, names[i]);
        if (length < 0 || (size_t)length >= sizeof path || !surface_host_capture(host, path)) return 0;
        printf("Captured %s\n", path);
        advance(host, shell);
    }
    return 1;
}

/** @brief Translate primary pointer, cancellation and key events to portable input. */
static void input(surface_t *shell, XEvent *event)
{
    tiku_input_event_t portable = {0};
    if (event->type == ButtonPress || event->type == ButtonRelease) {
        if (event->xbutton.button != Button1) return;
        portable.type = event->type == ButtonPress ? TIKU_EVENT_POINTER_DOWN : TIKU_EVENT_POINTER_UP;
        portable.button = 1;
        portable.x = event->xbutton.x;
        portable.y = event->xbutton.y;
    } else if (event->type == MotionNotify) {
        portable.type = TIKU_EVENT_POINTER_MOVE;
        portable.x = event->xmotion.x;
        portable.y = event->xmotion.y;
    } else if (event->type == FocusOut) portable.type = TIKU_EVENT_DEACTIVATED;
    else if (event->type == KeyPress) {
        char text[16];
        KeySym symbol;
        int n = XLookupString(&event->xkey, text, sizeof text, &symbol, NULL);
        portable.type = TIKU_EVENT_KEY_DOWN;
        portable.key = symbol == XK_Escape ? 27 : symbol == XK_BackSpace ? 8 : n == 1 ? (unsigned char)text[0] : 0;
    } else return;
    (void)surface_post(shell, &portable);
}

/** @brief Run a fixed-size native window; smoke exits after ten owner turns. */
static int preview(surface_host_t *host, surface_t *shell, int smoke)
{
    Display *display = XOpenDisplay(NULL);
    if (!display) { fputs("Cannot open X11 display; use --capture DIRECTORY for headless output.\n", stderr); return 0; }
    int screen = DefaultScreen(display);
    Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 40, 40, 800, 480, 0, 0, 0);
    XStoreName(display, window, "TikuSurface - host preview");
    XSizeHints hints = {.flags = PMinSize | PMaxSize, .min_width = 800, .max_width = 800, .min_height = 480, .max_height = 480};
    XSetWMNormalHints(display, window, &hints);
    Atom close = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &close, 1);
    XSelectInput(display, window, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask);
    GC gc = XCreateGC(display, window, 0, NULL);
    XImage *image = XCreateImage(display, DefaultVisual(display, screen), (unsigned)DefaultDepth(display, screen),
        ZPixmap, 0, NULL, 800, 480, 32, 0);
    int ok = image && image->bits_per_pixel == 32 && image->red_mask == 0xff0000 &&
        image->green_mask == 0xff00 && image->blue_mask == 0xff && image->byte_order == LSBFirst;
    if (ok) { image->data = calloc(480, (size_t)image->bytes_per_line); ok = image->data != NULL; }
    if (!ok) fputs("Preview requires an XRGB-compatible 32-bit X11 visual.\n", stderr);
    XMapWindow(display, window);
    unsigned displayed = 0, turns = 0;
    int running = ok, exposed = 1;
    while (running) {
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type == ClientMessage && (Atom)event.xclient.data.l[0] == close) running = 0;
            else if (event.type == Expose) exposed = 1;
            else input(shell, &event);
        }
        if (smoke && turns == 2) {
            XEvent event = {.xbutton = {.type = ButtonPress, .button = Button1, .x = 80, .y = 200}};
            input(shell, &event);
            event.type = ButtonRelease;
            input(shell, &event);
        }
        host->now += 10000;
        (void)surface_host_poll(host);
        surface_step(shell);
        (void)surface_render(shell, surface_host_presenter(host));
        if (host->frame.owner && displayed != host->submissions) {
            for (int y = 0; y < 480; ++y)
                memcpy(image->data + y * image->bytes_per_line, host->frame.pixels + y * 800, 800 * 4);
            displayed = host->submissions;
            exposed = 1;
        }
        if (exposed) {
            XPutImage(display, window, gc, image, 0, 0, 0, 0, 800, 480);
            XSync(display, False);
            exposed = 0;
        }
        if (smoke && ++turns == 10) {
            surface_view_t view;
            surface_view(shell, &view);
            ok = view.foreground == SURFACE_CALCULATOR && displayed >= 2;
            running = 0;
        }
        struct timespec pause = {0, 10000000};
        nanosleep(&pause, NULL);
    }
    if (image) XDestroyImage(image);
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc != 1 && !(argc == 2 && !strcmp(argv[1], "--smoke")) &&
        !(argc == 3 && !strcmp(argv[1], "--capture"))) {
        fputs("Usage: surface-preview [--smoke | --capture DIRECTORY]\n", stderr);
        return 2;
    }
    surface_host_t host;
    surface_config_t config = surface_host_config(&host);
    surface_t *shell = surface_create(&config);
    if (!shell) { fputs("Shell allocation failed\n", stderr); return 1; }
    int ok = argc == 3 ? capture(&host, shell, argv[2]) : preview(&host, shell, argc == 2);
    surface_stop(shell);
    advance(&host, shell);
    if (!surface_destroy(&shell) || host.memory.live) ok = 0;
    return ok ? 0 : 1;
}
