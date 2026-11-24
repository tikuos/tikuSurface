# Native desktop build

`SURFACE_NATIVE_DESKTOP=1` puts TikuDesktop's desktop, Deskbar and Tracker,
from Stomme's `packages/shell`, in the S31 application slot. `desktop/desktop.mk`
compiles the shell package and the kits it uses against `desktop/`, the TikuOS
side of the desktop. `desktop/firmware.mk` links them with the panel, touch and
settings bindings of the LCD runtime.

From the matching TikuOS root, with TikuSurface at `tikuSurface/`:

```sh
make MCU=esp32s31 S31_PROFILE=s31-local S31_SURFACE_LCD=1 S31_CPU_MHZ=320 \
    S31_APPLICATION_MANIFEST=tikuSurface/product/tikuos.mk \
    SURFACE_NATIVE_DESKTOP=1 S31_DIR=build/desktop-native-local \
    DESKTOP_ASSETS=/path/to/TikuDesktop \
    CROSS_COMPILE=/path/to/riscv32-esp-elf- PYTHON=/path/to/venv/bin/python all
```

The build refuses to start without `S31_SURFACE_LCD=1` and `S31_CPU_MHZ` of
160 or 320. At 320 MHz a tap takes a median 136 ms idle and 158 ms while a
card copy runs (160 ms and 286 ms at 160 MHz).
After the link, `tools/desktop_placement.py` checks where the desktop's code
and statics landed, and `tools/libc_stubs.py` reads the link map for system
calls taken from libnosys, whose definitions set `errno` to `ENOSYS`; a call
the desktop needs that no object defines links without an error and fails
when it runs. Kept libnosys calls other than `_execve`, `_fork`, `_kill` and
`_wait` fail the build, and the image is deleted.
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

## Clock

The desktop's clock calls, `clock_gettime`, `gettimeofday` and `time`, come
from TikuOS's `compat/posix` library, which `desktop/firmware.mk` selects
with `TIKU_POSIX_ENABLE := 1`; real time is the kernel's wall clock. Until
the clock is set, by `write /sys/time` or the network time command, the
Deskbar shows dashes for the time and files made then have no date. After
a restart the clock goes on from the time last set: the Deskbar shows that
time dimmed, with a question mark, until the clock is set again.

## Files

The desktop's files live in a tree in its heap, built again at each start,
with its settings in the store and the `/proc` files the volume kit reads.
With an SD card in the board, `/home` is the card's `home` folder: `Desktop`,
`Documents`, the Trash and each file saved there are card objects, made the
first time the desktop starts with that card, while `.config` and `.local`
stay in the tree. `/Card` shows the rest of the card's media volume through
the queued media bridge. A card file opened to write holds its bytes in its
node and is published whole at `fsync()` or `close()`, one atomic replacement
of at most 64 KiB. A larger file is written front to back: its bytes stream
into a hidden object at the card's root in 64 KiB pieces, `fsync()` commits
them there, and at `close()` the object takes the file's name in one store
transaction, so the old version stays whole until then; while it streams,
a read of the file returns `EBUSY`. A hidden object left by a cut is removed
before the next large write. Renames, deletes
and folders reach the card. A folder keeps a hidden empty `.folder` object,
so it stays after a restart when empty, and renaming one renames each object
under it, putting them back in turn when one is refused. A card Trash keeps
its items' original paths in a hidden `.origins` object, so Restore works
after a restart. A move between the card and the tree's own folders returns
`EXDEV`, and Tracker copies instead. After another client changes the card,
such as the shell's `media` command or a computer transfer, a file is found
again by name before it is next opened, renamed or deleted: one that is gone
returns `ENOENT` and leaves its folder, and one replaced refuses its first
rename or delete with `ESTALE`. A folder shows a file another client added
after the next start. Without a card, `Desktop` and
`Documents` are in the tree, take no new file and hold a read-only
`No SD card - nothing is saved here.txt`. `/Board` is the kernel's namespace, `/sys`, `/dev`, `/data`
and the rest, read through TikuOS's `compat/posix` (`desktop/firmware.mk`
selects it and leaves the library's own file hooks out); a change under it
returns `EROFS`. Tracker lists it as a volume in the root window.
`/dev/urandom` and `/dev/random` return SHA-256 counter-mode
output from a key seeded once at start. Tracker starts with image thumbnails off.

## Applications

`desktop/builtins.c` lists the applications linked into the desktop, which
open their windows in its own workspace: TikuImage, from Stomme's
`packages/apps/apps/image`, for PNG, JPEG and BMP files. The launcher hands
it a document Tracker opens through its host for built-in applications. Its
window fills the screen below the bar, and it decodes each
picture no larger than its view, after letting the one shown go: a baseline
JPEG of one scan is converted a row of MCUs at a time, its planes reduced to
a half, quarter or eighth of each block by the means of the samples they
cover, so a 1640 by 1232 photo needs the file's bytes, its 427 by 321
pixels and a few kilobytes of planes. A PNG or BMP is decoded whole first. The view is
painted a row at a time with no cache. On the panel at 320 MHz the 176 KiB
`camera-1640x1232.jpg` on the card takes 2.1 s to read and 2.0 s to decode.
The Open button is unavailable; pictures are opened from Tracker.

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

Touch mode also turns on the shell's touch behaviour (`TIKU_SHELL_TOUCH`):
the 16 px face, the lens for small controls and menus, and the calibration
page in the Tiku menu. The page reaches the touch map through
`tiku_shell_touch_fit()`, `tiku_shell_touch_keep()` and
`tiku_shell_touch_revert()` in `desktop/runtime.c`: a fit corrects the map in
force from the edges the page measured and stores nothing, keep writes the
calibration record, and revert reads it back.

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
| `target=X,Y` | show the touch mark alone at a panel point, as a point to aim a finger at; a finger moves it and its lift removes it |
