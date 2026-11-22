# Native desktop build

`SURFACE_NATIVE_DESKTOP=1` puts TikuDesktop's desktop, Deskbar and Tracker,
from Stomme's `packages/shell`, in the S31 application slot. `desktop/desktop.mk`
compiles the shell package and the kits it uses against `desktop/`, the TikuOS
side of the desktop. `desktop/firmware.mk` links them with the panel, touch and
settings bindings of the LCD runtime.

From the matching TikuOS root, with TikuSurface at `tikuSurface/`:

```sh
make MCU=esp32s31 S31_PROFILE=s31-local S31_SURFACE_LCD=1 S31_CPU_MHZ=160 \
    S31_APPLICATION_MANIFEST=tikuSurface/product/tikuos.mk \
    SURFACE_NATIVE_DESKTOP=1 S31_DIR=build/desktop-native-local \
    DESKTOP_ASSETS=/path/to/TikuDesktop \
    CROSS_COMPILE=/path/to/riscv32-esp-elf- PYTHON=/path/to/venv/bin/python all
```

The build refuses to start without `S31_SURFACE_LCD=1` and `S31_CPU_MHZ=160`.
`DESKTOP_ASSETS` names the TikuDesktop checkout that supplies the desktop's
strings, sounds and icons. `DESKTOP_FOUNDATION` names the Stomme checkout and
defaults to the pinned `foundation/`.

## Runtime

Start reserves the runtime's PSRAM span for two RGB565 scanout buffers, the
desktop thread's 3 MiB stack and the C library's heap. It mounts the SD card
without writing, seeds the desktop's random generator, opens the panel and
touch, and starts the desktop on a TikuOS thread. Each owner turn turns touch
reports into pointer events, presents a frame the desktop handed over and
advances the desktop's card request. The shell's zero-initialized statics sit
in a 640 KiB section at the top of PSRAM; the firmware's PSRAM tier ends below it.

## Files

The desktop's files live in a tree in its heap: `/home` with `Desktop`,
`Documents` and `.config/tracker`, its settings and the `/proc` files the
volume kit reads. The tree is built again at each start. `/Card` shows the SD
card's media volume read-only through the queued media bridge; a write there
returns `EROFS`. `/dev/urandom` and `/dev/random` return SHA-256 counter-mode
output from a key seeded once at start. Tracker starts with image thumbnails off.

## Limits

`DESKTOP_LIMITS` in `desktop/desktop.mk` sizes the shell for the panel's PSRAM:
256 entries per folder view, a 256 KiB largest state file, 128 native settings
entries and resource providers, one remote session, 32 notices per batch, menus
four levels deep, and 1 MiB each for window content caches and kept folder
pixels. Settings files are polled for outside edits every 5 s.
`SURFACE_HEAP_PROFILE=1` records the desktop's heap blocks by call site.

## Touch

With `SURFACE_TOUCH_MODE=1`, the default, a finger gets a mark under it as
soon as the first report arrives: the runtime draws it into the buffer on show
between submissions and writes those rows back to PSRAM, and each frame
presented while the finger stays carries it. The pointer stays where the press
landed until the finger travels 10 pixels, and a press within 24 pixels of a
tap and 400 ms after it lands on that tap, so the desktop sees a double click
(`desktop/gesture.h`). `SURFACE_TOUCH_MODE=0` posts the raw contacts.

## Diagnostics

`/surface/control` accepts these commands from the owner context:

| command | effect |
|---|---|
| `capture` | print the panel's front buffer on the console as run-length rows |
| `report` | the desktop prints frame statistics, mounts and its trail |
| `hash=PATH` | the desktop prints the size and SHA-256 of a file in its tree |
| `samples` | print the last 4096 program counters of the desktop thread |
| `stacks` | take and print 1024 stack samples of the desktop thread |
| `heap` | print the heap profile of a `SURFACE_HEAP_PROFILE=1` build |
| `down=X,Y` `move=X,Y` `up=X,Y` `tap=X,Y` `dtap=X,Y` | queue pointer events as touch posts them |
| `drag=X0,Y0,X1,Y1` | queue a press, eight moves along the line and a release |
| `finger=X,Y` `lift` | place a finger at a panel point, or lift it, through the touch path: mark, slop and tap landing |
