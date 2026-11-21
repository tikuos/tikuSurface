# Desktop and Tracker build

`mk/core.mk` selects desktop windows, writable Tracker and touch settings.
It enables `SURFACE_DESKTOP`, `SURFACE_TEXT_INPUT`, `SURFACE_FILE_JOBS`,
`SURFACE_CONTENT_JOBS`, `SURFACE_FILE_INFO` and `SURFACE_DIRECTORIES`.
The core launch inventory contains Home, Tracker and Settings.

The selected SDK must export `window-workspace` and `deskbar-paint` alongside
the storage models and controls. These capabilities require a matching Stomme
export; the current pinned SDK does not provide them. Pass the export through
`SDK_ROOT` for host builds and `SURFACE_SDK` for firmware builds. Keep the
foundation gitlink, lock revision and content hash together when adopting a
published dependency revision.

From the TikuSurface root:

```sh
make -f mk/core.mk SDK_ROOT=/path/to/host-sdk \
    TIKUOS_ROOT=/path/to/tikuOS core-test
make -f mk/core.mk SDK_ROOT=/path/to/host-sdk \
    TIKUOS_ROOT=/path/to/tikuOS BUILD=build/core-asan \
    CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie' \
    core-test
make -f mk/core.mk SDK_ROOT=/path/to/host-sdk \
    TIKUOS_ROOT=/path/to/tikuOS display-test input-test
make -f mk/core.mk SDK_ROOT=/path/to/host-sdk \
    TIKUOS_ROOT=/path/to/tikuOS preview
```

`capture` writes `home.ppm`, `browser.ppm` and `settings.ppm` under the build
directory. Host preview storage is a read-only fixture. `desktop-media-test`
drives the writable Tracker against private MFS media, including remounts and
stale-operation refusal. `desktop-runtime-test` exercises the local and
connected runtime bindings with simulated display and touch providers.

From the matching TikuOS root, with the product at `tikuSurface/`:

```sh
make MCU=esp32s31 S31_PROFILE=s31-local S31_SURFACE_LCD=1 \
    S31_DIR=build/surface-core-local S31_CPU_MHZ=160 \
    CROSS_COMPILE=/path/to/riscv32-esp-elf- PYTHON=/path/to/venv/bin/python \
    SURFACE_SDK=/path/to/s31-sdk \
    SURFACE_DESKTOP=1 SURFACE_TEXT_INPUT=1 SURFACE_FILE_JOBS=1 \
    SURFACE_CONTENT_JOBS=1 SURFACE_FILE_INFO=1 SURFACE_DIRECTORIES=1
```

The LCD core requires `S31_CPU_MHZ=160`. CPU and memory run at 160 MHz,
the system bus at 80 MHz and APB at 40 MHz. The clock transition uses the
CPLL initialized by the 80 MHz PSRAM backend.

`cat /surface/timing` reports paint-to-handoff microseconds, the maximum since
start, completed foreground identity, completed-frame count and pending work.
It does not measure physical touch or optical panel latency.

For connected operation, select `S31_PROFILE=s31-connected`, supply
`S31_WIFI_COUNTRY` for the deployment location and choose a separate `S31_DIR`.
For UI LTO, add `S31_UI_LTO=1`, select the LTO SDK and use a separate output
directory. These commands link firmware without accessing a board.

## Desktop operation

Volumes opens Tracker. Settings opens touch calibration. The right-hand
Deskbar lists retained tasks; its arrows reveal window rows. A task or window
row restores and activates its window. The TikuOS menu also opens Volumes and
Settings. Windows have native title tabs, stacking, focus, close, zoom and
resize controls. Drag the title tab to move; drag the lower-right corner to
resize. Hide minimizes the window. Command-M hides and Command-W closes the
focused window. Escape cancels a captured gesture and dismisses the menu.

On S31, Tracker uses a mounted TikuOS native-media SD volume. The product does
not mount or format a card at startup; it reports unavailable media until a
volume is mounted. Use an already prepared card. Stop Surface and wait for
`running=0` before mounting from the console:

```text
write /surface/control stop
cat /surface/status
media off
media mount 4000000
media info
write /surface/control start
```

Check that mounting reports `result=0` and `media info` identifies the intended
volume before restarting Surface. `media off` stops the transfer listener;
it does not erase the card. FAT/exFAT is not the native-media format. Provisioning
a new volume is a separate destructive operation and is not part of these commands.

Tracker displays eight entries per page. Click an entry to select it;
double-click, Return or Open opens it. Up returns to the containing folder.
Previous and Next change pages. File actions selects the entry for information,
rename/move, copy or confirmed permanent deletion. New file and New folder use
the touch keyboard. Rename/move and Copy to accept destination paths. Recursive
copy admits 65536 bytes per file and relative paths of at most 63 bytes; the
destination must not exist. Empty folders persist across remounts. Storage
changes invalidate stale identities before mutation.

The profile retains one Tracker window and one Settings window. Folder
navigation reuses Tracker. Arbitrary desktop file placement, multiple folder
windows, icon-position persistence, trash recovery and file drag-and-drop are
outside this profile. The Desktop's native workspace, list selection and
Deskbar painting are shared Stomme implementations; the embedded shell owns
its task roster and queued storage bindings.

## Resource contract

The UI reservation is 3928064 PSRAM bytes. The core UI IROM allowance is 229376
bytes; the whole-image IROM reserve remains 65536 bytes. Positive resident SRAM
growth is limited to 2048 bytes, accounted separately from display descriptors.
The core paints four rows or converts at most 4096 pixels per runtime dispatch.
Queued continuations yield after 20 ms or 64 turns; each dispatch retains the
5000 microsecond limit.
Window storage uses the configured allocator. The renderer freezes window
geometry and content per frame before incremental painting; native heap-backed
window caches and animations are not selected by the embedded profile.

Host captures and simulated providers establish software behavior. Physical
panel appearance, touch interaction, latency and interrupted storage operation
require qualification of the matching firmware artifact.

`tools/qualify_core.py` supplies the core lifecycle workload to TikuOS's
`tools/esp32s31_flash.py --application-test` runner. It first checks the boot
Tracker/Settings/Tracker journey, then ten startup allocation refusals and 100
start/Tracker/Settings/Home/Tracker/Settings/Tracker/stop cycles. Each cycle
requires LCD completion, timer and console progress, released PSRAM, dispatch
time within 5000 microseconds and at least 4096 free stack bytes. The runner
also matches each launch request to its completed foreground frame and requires
request-to-handoff time within 500000 microseconds.
The runner
requires explicit board identity, an image and a verified factory backup;
it verifies and restores its application, storage and settings regions.
This workload does not qualify physical touch or input-to-visible latency.

The timing node also reports accepted runtime-start/launch request IDs and the
last request ID that reached a completed frame. `request_us` includes startup construction
or launch handling, scheduler waits and display handoff; `max_request_us` retains
the maximum since start. A pending launch replaced before painting has no
completed sample. A frame already being presented retains its earlier request
ID. These counters describe programmatic requests, not physical touch input.
