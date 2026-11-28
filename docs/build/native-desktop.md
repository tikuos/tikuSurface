# Native desktop build

`SURFACE_NATIVE_DESKTOP=1` puts TikuDesktop's desktop, Deskbar and Tracker,
from Stomme's `packages/shell`, in the S31 application slot. `desktop/desktop.mk`
compiles the shell package and the kits it uses against `desktop/`, the TikuOS
side of the desktop. `desktop/firmware.mk` links them with the panel, touch and
settings bindings of the LCD runtime. Both native profiles link the SHA-256
implementation from the firmware checkout at `tikukits/crypto/sha256/`.

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

## Graphics acceleration

Stomme passes clipped rectangle fills and same-scale image copies to the native
renderer. On the desktop thread, rectangles of at least 16,384 pixels with a
suitable row stride can use the S31 Pixel Processing Accelerator. The CPU paints
the end strips outside whole cache lines and handles small operations and
hardware refusals. The worker can draw into the surface again after the kernel
has retired the job. The `paint` field in the desktop status line counts
completed hardware fills, copies and accepted jobs that failed.

Build with `SURFACE_PPA_PAINT=0` to measure software drawing with the same
hardware conversion to the LCD’s RGB565 format. `SURFACE_PPA=0` disables both
hardware drawing and conversion.

## Runtime

Start reserves two RGB565 frame buffers in the PSRAM tier and opens the panel
on them; the display driver then returns its own 768,000-byte scan-out
buffer. Start reserves the rest after that, in the room the driver left: the
desktop thread's 2.75 MiB stack, the job thread's 384 KiB stack, the settings
files' stages and the C library's heap, 9,275,392 bytes. It mounts the SD
card without writing, opens touch, and
starts the desktop on a TikuOS thread. Each owner turn turns touch
reports into pointer events, presents a frame the desktop handed over and
advances the desktop's card request. The shell's zero-initialized statics sit
in a 640 KiB section at the top of PSRAM; the firmware's PSRAM tier ends below it.

The About box draws the panel's SRAM, PSRAM and flash as bars, each with its
areas and what is in use, from `tiku_sys_memory_map()` in `desktop/sys.c`:
the kernel's `/sys/mem/sram_map` and `/sys/mem/nvm_map`, and PSRAM in address
order, the file store's shadow below the kernel's tier, the tier's free bytes,
the drivers' buffers in it (the card's bitmap), the two stacks with their
deepest use, the stages, the heap with the bytes its allocator has handed out
and the runtime's frames, then the shell's statics. Its free memory is the
heap less those bytes. The desktop draws the box on its own surface through
the box's painter (`open_drawn` in Stomme's application services), so the box
keeps no pixels: it takes about 46 KB of the heap while it is open.

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
`/dev/urandom` and `/dev/random` read the S31's health-checked random source
through TikuOS character devices. A read returns up to 64 bytes or `EIO` on a source fault. Tracker starts with image thumbnails off.

## Applications

Open **Demos → Orbit** in the Deskbar to watch three satellites and measure
desktop FPS. Orbit divides successful desktop presentations by elapsed time
and updates the reading about once a second. Minimizing the window pauses
the animation. Restoring it starts a fresh measurement.

`desktop/builtins.c` lists the applications linked into the desktop, which
open their windows in its own workspace: Camera, in the Deskbar's Graphics &
Design group, and Sound Recorder, in its Audio group; the icons `camera` and
`sound_recorder` come from the `DESKTOP_ASSETS` checkout. Camera opened
alone shows the board's camera; handed a PNG, JPEG or BMP file, from Tracker
through the launcher's host for built-in applications, it shows the file in
TikuImage's viewer from Stomme's `packages/apps/apps/image`.

Camera's window (`desktop/camera.c`) asks the owner turn, through the
bridge's camera request served by `platform/tikuos/camera.c`, to start the
S31's DVP camera streaming: 10.4 pictures a second, each every other line of
a 1280 by 720 UYVY frame, into the two halves of a 1,843,200-byte buffer
from the heap. The owner turn drops the cache's copy of each new picture's
half and publishes its number and half on the bridge, waking the desktop.
Each desktop turn the window compares the number with the one it last drew
and, when it changed, asks for its picture rectangle alone to be drawn
again (`tiku_trk_builtin_damage()`). The window is opaque, so that frame
draws nothing under it; the window converts the picture on the processor,
one pair of pixels to one pixel, its two lumas averaged, into a 640 by 360
rectangle, or 320 by 180 and smaller in a smaller window. On the panel the
conversion takes 44 ms and Camera shows about 8 pictures a second; it needs
PSRAM at 200 MHz, at 80 MHz scanout underruns.

Take photo captures one whole frame in the stream's buffer, encodes it with
the S31's JPEG encoder at quality 80 into a 512 KiB buffer, about 60 KB for a
room scene, and starts the stream again while a job saves the photo as
`~/Pictures/photo-YYYYMMDD-HHMMSS.jpg` on the card, numbered when the second
is taken; the view says so meanwhile. Photos shows the last one saved in
the viewer. The photo's buffer comes from the heap while it is encoded and
saved; closing the window stops the stream and the sensor once the running
request ends and frees the frame then.

Sound Recorder (`desktop/recorder.c`) records the board's microphones
through the kernel's microphone service (`services/audio/AGENTS.md` in
TikuOS). Record writes `1` to `/sys/audio/mic/enable`, under the desktop's
`TIKU_VFS_CAP_HW` authority, and opens `/dev/mic`; each desktop turn it
reads what waits, up to 16 reads of 4,096 samples, into a take held in the
heap a second to a 32,000-byte chunk, and draws the last four seconds as a
waveform in the scope with a level meter beside the elapsed time. Stop
writes `0` and hands the take to a job, which writes it as a mono 16 kHz
PCM16 WAV named `recording-YYYYMMDD-HHMMSS.wav` in `~/Recordings` on the
card. The take ends when the heap has no room for the next second, when the
microphone fails, or when the window closes, and is saved in each case.
Samples the microphone's four-second ring dropped while the desktop did not
read, such as during a screen capture over the UART, are reported with the
saved take.
The list shows the WAV files in `~/Recordings`, newest first; a tapped one's
waveform fills the scope. The microphone's gain is 30 dB unless
`/sys/audio/mic/gain` is set. `S31_MIC=1`, set in `desktop/firmware.mk`,
builds the service and its nodes into the S31 image. The P4X RAM desktop
selects the ES8311 with `P4X_MIC=1`. Its `~/Recordings` directory lives in
RAM and holds the WAV files until reset.

The viewer's window fills the screen below the bar, and it decodes each
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

In touch mode, open **Deskbar → Settings → Touch**. **Magnify small controls**
enlarges menus and small targets under the finger. It starts enabled and the
desktop saves the choice in `/data/ts/touch`. **Calibrate touchscreen** opens
the same nine-target page as **Calibrate Touch** in the Tiku menu. Tap each
target, then tap **Keep** to save the alignment. The page reaches the touch map through
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

## ESP32-P4X desktop archive

The P4X board selection builds the shared desktop, Deskbar, Tracker,
mouse motion and presentation code for a 1024 by 600 surface. The host
test covers the final row and column, source stride and calibration data.
Build the archive from the matching TikuOS root. The build reads
`applications/board/gui/tiku_draw_program.h` from the Tracker checkout
under TikuOS. Set `DESKTOP_ASSETS` to the separate TikuDesktop checkout.
The RAM and flash images also use the board fixture in
`TikuBench/tests/esp32p4/test_p4x_panel.c` and SHA-256 from `tikukits/`.

```sh
make -f tikuSurface/desktop/desktop.mk DESKTOP_BOARD=esp32_p4x_function_ev_board \
    DESKTOP_ASSETS=/path/to/TikuDesktop DESKTOP_FOUNDATION=/path/to/Stomme \
    CROSS_COMPILE=riscv32-esp-elf- desktop-archive
```

The archive lands in `build/desktop-p4x/libsurface_desktop.a`. TikuOS's
`arch/esp32p4/README.md` describes the separate RAM image for display,
touch and external memory. The display adapter uses the kernel’s paired
submission API at 1024 by 600. Each RGB565 scanout buffer occupies
1,228,800 bytes. The native runtime reserves 2,457,600 bytes for the pair.
The GT911 default maps raw (0, 0) to screen (1023, 599). A saved calibration
overrides the board default.

The P4X renderer draws the desktop on CPU0. For presentation, TikuSurface
submits each changed XRGB8888 rectangle to the TikuOS PPA scaler at unit
scale. The completion interrupt wakes the owner to present the RGB565 buffer.
The worker can draw into the source again while the display completes
the previous frame.
If the engine refuses a rectangle, the CPU converts it. If a conversion is
still pending after 250 ms, the driver resets its DMA engine and retires the
request. The desktop then uses CPU conversion until the display reopens.
Build the RAM image with `SURFACE_PPA=0` to measure CPU conversion.
Pass `--require-ppa` to the loader to check that hardware jobs completed
and the display reported zero conversion failures.

The RAM desktop runs the shared Tracker, Deskbar and application server on a
TikuOS worker on CPU0 at 400 MHz. At startup the test measures CPU cycles
against a 10 ms interval on the 16 MHz system timer and checks the selected
frequency within 0.5%. Pass `P4X_CPU_MHZ=40` to build the crystal-clock
configuration. The pixel clock is 40 MHz in both configurations. Tracker opens `/devices/board/sys/clock` and lists the kernel's
`ticks` device node. The loader places the desktop in PSRAM and verifies its
SHA-256 digest before starting it. The kernel file store uses a separate 3 MiB
PSRAM region that the test formats on each run. Settings last for the session.
The RAM profile leaves the SD card, camera, terminal and BASIC adapters offline.
The wall clock starts unset.

From the TikuOS checkout, build the RAM image with the matching kernel, drivers,
TikuBench, Stomme and TikuDesktop sources. The Python interpreter needs esptool
and pyserial. Run the loader with the board's USB Serial/JTAG port and serial
number. By default the loader resets the P4X to its installed firmware when
the test finishes. Add `--leave-running` to release the console and use
the RAM desktop until the next board reset.

```sh
make -j8 -f tikuSurface/tests/p4x/ram.mk TOOLCHAIN_PREFIX=riscv32-esp-elf- \
    DESKTOP_ASSETS=/path/to/TikuDesktop DESKTOP_FOUNDATION=/path/to/Stomme
python3 tikuSurface/tools/qualify_p4x.py --port "$PORT" --serial "$P4_SERIAL" \
    --capture tracker.png
```

`--test-recorder` opens **Audio → Sound Recorder** on the P4X, records two
takes through its Record and Stop controls, and reopens the WAV files on
the board to check the PCM format, sample counts and signal variation.

`--test-live-row` checks three distinct frame counters while Tracker displays
the clock folder. Each sampled conversion must cover less than half the panel.
`--test-menu` taps the Deskbar in software, selects the button in the touch
magnifier when enabled, and checks the shared Deskbar's painted menu state
before and after dismissal. With `--capture tracker.png --test-app`, the
runner opens **Settings → Touch** through the Deskbar, switches off
**Magnify small controls** and checks the saved byte in the kernel file
store. It closes the window, reopens it by direct taps, restores magnification
and closes it through the touch lens. The runner saves the window and
the restored Tracker frame alongside `tracker.png`. Both checks inject
screen coordinates into the desktop input queue. `--hold 120` leaves the
desktop available for physical touch for two minutes after capture.
The board reports the GT911 touch history before
reset. `--corrupt-segment` changes one byte in the transfer to check that the
loader rejects the payload.
