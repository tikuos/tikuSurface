# TikuOS runtime

The S31 application manifest builds the shared shell and applications against
Stomme's exported SDK. The firmware binding reserves one 64-byte-aligned PSRAM
span and runs a timer process in the kernel owner context. The SDK allocator
uses 32 allocation records inside that span. Allocation failures unwind the
shell; a refused span release retains ownership for another process turn.

The clock reports microseconds from the S31 system timer. The process wakes
on a 128 Hz timer, completes the preceding diagnostic frame, dispatches
bounded event counts and paints up to two rows from a frozen view. LCD mode
can queue additional bounded turns between timer events. Completed
frames are submitted after 240 paint turns; events during painting queue another
frame. Counters report dispatch duration,
turns exceeding 5 ms, frames, live bytes, peak bytes and allocation failures.
The dispatch sample includes shutdown and reservation release; synchronous
startup is outside that sample.
The 5 ms counter is an observation threshold. Row limits do not establish a
target execution-time bound without board measurements.

The default diagnostic provider retains pixels until the following owner
turn. Diagnostic media requests return unavailable, and diagnostic touch and
persistent settings controls are unavailable. `S31_SURFACE_LCD=1` selects
paired LCD presentation, GT1151 input, the mounted native-media service and
product calibration storage. The connected profile starts
Wi-Fi through a separate process after application startup. Connection failure
prints a console diagnostic.

`runtime.h` carries the boundary between SDK and firmware types. `bindings.c`
includes TikuOS headers; `runtime.c` includes exported SDK headers. Lifecycle
calls and statistics reads require serialized kernel-owner execution. The boot
composition starts the product after memory, clock, scheduler, namespace and
shell initialization. Stop drains the retained frame before returning PSRAM.
A failed startup that retains a reservation also starts the retirement process.
The owner accepts its poll, exit and expired wake-timer events; unrelated
events and stale timer deliveries do not advance the runtime.

The linker combines product code and the SDK into `surface-ui.o`. With
`S31_UI_LTO=1`, the partial link consumes UI/SDK LTO before the firmware link.
Firmware interrupt and storage objects remain native objects. The generated
linker script selects product executable code, constants, fonts and the SDK
identity in IROM. Runtime-owned buffers and contexts use the PSRAM reservation;
remaining static product state is accounted in SRAM. Final checks inspect
input sections, product symbols, SDK identity, resident anchors and budgets.

`make test` and `make test-asan` execute the runtime against a fixed aligned
reservation with heap traps. They cover startup allocation failures, delayed
release, retained presentation, application failures and 100 start/stop cycles.
Board boot, scheduling latency, console progress and stack high-water usage
require hardware qualification.

## Console diagnostics

Boot assembly publishes the static `/surface` subtree and includes it when the
namespace is restored. Reads require the kernel owner. The `control` and calibration writes
require `TIKU_VFS_CAP_SYS`; the UART shell supplies that capability.

| Path | Operation |
| --- | --- |
| `/surface/status` | Read runtime counters, foreground app ID and unavailable-media flag |
| `/surface/display` | Read LCD selection, completed handoffs and display failures |
| `/surface/touch` | Read report/error counts, current contact count and the last nonempty report's first raw contact |
| `/surface/calibration` | Read active mapping/status; write `x_min,x_max,y_min,y_max,swap,invert_x,invert_y` |
| `/surface/psram_free` | Read unreserved bytes in the primary PSRAM span |
| `/surface/control` | Write `start`, `stop`, `launch=0..3` or `fail=0..32` |

Commands have exact byte lengths. Signs, spaces, newlines, embedded NULs and
trailing data are rejected. App IDs are Home 0, calculator 1, browser 2 and
device information 3. `fail=N` rejects the Nth subsequent allocation attempt;
zero disables injection. `start` reports EIO when construction is refused.
`stop` requests retirement; read `status` until `running=0` before comparing
PSRAM availability or starting again. Status reads do not dispatch runtime work.

```text
cat /surface/status
write /surface/control launch=2
cat /surface/status
write /surface/control stop
cat /surface/status
cat /surface/psram_free
write /surface/control fail=1
write /surface/control start
write /surface/control fail=0
write /surface/control start
```

Build the VFS integration test against a TikuOS checkout and its TikuBench host
hooks with `make test-tikuos TIKUOS_ROOT=/path/to/tikuOS`. The test links the
production VFS and exercises permissions, malformed/nonterminated inputs,
context refusal, observation, partial failure, deferred retirement and remount.

`tools/qualify_runtime.py` exercises an already installed diagnostic image over
an explicitly selected UART. It does not flash or reset the board. Board access
requires authorization under this repository's AGENTS.md. Close other console
clients before running it, and retain the flashing/readback record separately:

```sh
python3 tools/qualify_runtime.py --port /dev/serial/by-id/DEVICE \
    --board esp32s31-korvo1 --image /path/to/kernel.bin \
    --output /path/to/new-runtime-report.json
```

The runner requires pyserial, opens the port exclusively at 115200 baud with
DTR/RTS deasserted, and refuses an existing report path. It records command
replies, five startup allocation failures, 100 start/stop cycles, app/media
state, console echoes, clock progress, PSRAM restoration and stack headroom.
Dispatch above 5 ms or stack headroom below 4096 bytes fails the report. Short
runs selected with `--cycles` cannot set `qualification_complete` until at least
100 cycles finish. Cleanup clears failure injection and leaves the runtime
stopped. The image hash identifies the operator-supplied file; the runner does
not establish which bytes were flashed. Simulated runner tests are host evidence.

## Flash and restore qualification

TikuOS's `tools/esp32s31_flash.py` loads `ApplicationQualification` from the
runtime runner with `--application-test`. Run it from the firmware checkout,
using esptool 5.6.0 and pyserial, after obtaining board authorization:

```sh
python tools/esp32s31_flash.py \
    --application-test tikuSurface/tools/qualify_runtime.py \
    --image build/surface-local/kernel.bin \
    --backup /path/to/factory-flash.bin --backup-sha256 <sha256> \
    --port /dev/serial/by-id/<bridge> --serial <bridge-serial> \
    --runs 1 --seconds 45 --log /path/to/new-surface-run.log
```

Use the image path produced by the selected build. The provider requires the
`tikusurface_s31_diag` project descriptor, one complete boot and successful
application startup before issuing commands. Each boot runs 100 lifecycle
cycles. `--seconds` sets the boot-capture interval; the command sequence can
take longer. A reset or fatal marker fails qualification. Raw UART bytes and
runtime results remain in the log and companion JSON after a failed command.
The adapter selects a 50 ms serial read timeout during capture and restores
the inherited timeout on return, including failure.

The flash runner validates the factory backup and live application sectors,
partition table, storage reservation and settings tail before writing. Both
reservations must be erased in the backup and on the board. Application mode
automatically protects storage and settings; do not add `--settings-test`.
It verifies the written image, restores and verifies the original application
and both reservations, and checks factory startup. A disconnected board can
prevent restoration; retain the backup and inspect the companion JSON.

`make test-flash TIKUOS_ROOT=/path/to/tikuOS` checks the integration using
simulated UART and flash operations. It covers identity refusal, incomplete
protection, nonblank reservations, failed qualification, interruption, partial
write, verification failure, restoration retry and exhausted retries.

## Presentation provider

`presentation.c` converts retained Stomme XRGB8888 views into caller-owned,
64-byte-aligned 800×480 RGB565 pairs. Initialization marks both buffers as
requiring a complete frame. Damage accumulates for each buffer until that
buffer becomes front. This preserves changes made while the other buffer
was scanned. Source rows may include padding.

Each poll converts at most the configured 1–4096 pixels, submits one request,
or polls one completion. The caller supplies transport operations, the
scanned buffer index and a token sequence that persists across reopen.
Submission refusal retains the source for retry; accepted requests retain
it until completion. A transport poll error must prove DMA retirement before
returning. `surface_presentation_closed()` releases a retained source with
error after the caller proves hardware closed. Transport faults and token
exhaustion require reinitialization.

`make test` and `make test-asan` compare converted buffers against independent
color references across alternating damage, padded rows and delayed completion.
They also check invalid layouts, busy retries, exhausted tokens and faults.
`make test-display TIKUOS_ROOT=/path/to/tikuOS` exercises the public firmware
API and S31 ring handoff through simulated registers. These checks do not
measure display timing. The LCD runtime binds this provider to scanout buffers.


## LCD runtime composition

Build with `S31_SURFACE_LCD=1` to select the S31 display. The application image
identity is `tikusurface_s31_lcd`; the diagnostic qualification runner refuses
that identity. Use UART0 during LCD operation because RGB data uses pins shared
with USB Serial/JTAG.

The runtime reserves 3,403,776 PSRAM bytes, including two 768,000-byte RGB565
buffers, the XRGB8888 source, allocation records and shell/application space.
The S31 backend separately reserves its 768,000-byte private scanout surface.
Seven startup allocations precede display open. Partial allocation failures
unwind; display open failures enter retirement. Initial display setup clears
the front surface and uses the existing synchronous initialization/pairing API.

Owner turns paint at most two rows or convert at most 1,024 pixels. Accepted
views remain retained through conversion, publication and DMA completion.
Scanout events are observed on owner turns, including while idle or converting.
The `/surface/status` frame count measures accepted rendered views;
`/surface/display` reports completed handoffs. A poll refusal preserves the
source lease and requests stop. A retired transport error completes the source
with error and requests stop. Close must succeed before scanout allocations
or a still-retained source can be released. Stop preserves an unclosed display
for another owner turn. Restart follows completed stop and reopens the display;
automatic restart is not selected.

Initialization, pairing and close retain the port's synchronous contracts.
The work limits are not elapsed-time guarantees. A complete frame requires 240 paint, 375 conversion and 188 publication
steps, plus submission and handoff. LCD mode queues another owner turn while
work remains, fewer than 32 turns have run in the burst, and less than 2 ms
has elapsed since its first turn. Touch polling, media progress and rendering use separate owner turns. The
budget is checked between steps and can be exceeded by the last step. The
burst state lives in the PSRAM runtime.

Continuations join the process queue behind already queued work. A one-tick
timer stays armed as fallback if the queue refuses a continuation. Exhausted
bursts and idle displays return to timer pacing. A delayed DMA completion is
polled within the same bounded burst policy. The 1,024-pixel and two-row
work limits remain measurement configurations. Host scheduling checks remove
the mandatory one-tick wait between every phase; they do not establish the
500 ms complete-frame goal or hardware dispatch bounds.

The product linker places foreground display/DMA code and constants in IROM.
It also selects named foreground tier allocation, backing and query helpers
into IROM. Public I2C wrappers, deadline/batch helpers, close, write-read and
`strrchr` run from IROM; other I2C functions retain SRAM placement. Tier
initialization, synchronous cache-disabled NVM writes and the remaining tier
code retain SRAM placement. The polled cache-enabled NVM path uses IROM as
described below. Map checks enforce these selections. The interrupt and
cache-disabled storage paths require a separate call-closure audit.
It places product/display BSS after core BSS so scanout descriptors do not
push core globals out of global-pointer range. Descriptors and driver state
remain in SRAM. The placement checker separately accounts for the fixed
6,112-byte descriptor block and checks other selected static SRAM against
2,048 bytes. Whole-image SRAM growth, alignment, stack and hardware timing
require their own qualification; a passing static-section check is not that
qualification.

## Touch and calibration

The public firmware touch provider returns complete raw reports independently
of Stomme. The product tracks the lowest contact ID at gesture start and keeps
that identity until release. Losing the primary while other contacts remain,
a malformed report, a bus failure, queue refusal or 100 ms without a report
cancels the interaction. Rearming requires an empty report. Application switches
and calibration changes also cancel the active interaction.

The adapter polls at most once per 10 ms and applies a 3.5 ms shared I2C
transaction deadline. The deadline spans status, payload and acknowledgement;
it is checked during batch waits and controller synchronization. Calls remain
serialized with other bus clients. The adapter borrows an open bus or owns and
closes a bus it initializes. Hardware timing must include deadline-check and
cleanup overhead.

The initial mapping explicitly selects raw bounds 0..799 and 0..479, no axis
swap and no inversion. These defaults require panel calibration. Inclusive raw
bounds map to 800 by 480 logical coordinates; swap precedes display-axis
inversion. Device information offers orientation controls and an explicit Save.
Measured bounds can be submitted through `/surface/calibration` with SYS
permission. Reads of `/surface/touch` perform no bus access.

Calibration is a 32-byte little-endian version-1 record with CRC-32, geometry,
raw bounds and axis flags, stored as `/data/tikusurface-cal-v1`. No record is
written during startup or pointer motion. Invalid records select defaults and
report their status. Unknown versions select defaults and refuse replacement.
Saves are queued, separated by at least five seconds, and applied after the
asynchronous transaction reports durable completion. A failure keeps the active
mapping unchanged; a storage
error after its commit point can leave the new record durable. Restart validates
the stored record again. The Wi-Fi settings reservation is not used.

`make test-input TIKUOS_ROOT=/path/to/tikuOS` exercises real report decoding,
bus ownership and S31 deadline logic against simulated I/O. Host tests cover
contact identity, cancellation, coordinate transforms, corrupt settings, explicit
save controls, restart loading and failure retention.

`make test-settings TIKUOS_ROOT=/path/to/tikuOS` links the LCD runtime and
settings adapter against the real TFS implementation with S31 slot geometry.
The private memory backend interrupts initial and replacement saves at each
write boundary, including partial payloads and ambiguous commit acknowledgements.
Remount and runtime restart must load the previous or fully committed mapping.
The fixture also checks malformed and future records, unavailable storage,
concurrent writers, exhausted slots and preservation of unrelated files. It
models the backend's atomic word contract; physical flash interruption and
calibration retention across board reset require hardware qualification.

`tools/qualify_calibration.py` is a two-boot provider for the protected
flash/restore runner. Select the LCD image and `--runs 2`. The first boot
requires default calibration, saves the temporary mapping
`12,787,8,471,0,1,0`, checks save acknowledgement and runtime restart, then
stops. The runner resets the board; the second boot checks that exact mapping
before any calibration write. Free PSRAM must match after shutdown on both
boots. The provider records persistence, 5 ms dispatch and 4096-byte free-stack
gates separately. A slow save permits the reload check but fails the combined
qualification. Factory application, storage and settings restoration remains
the flash runner's responsibility. This test verifies reset retention; it does
not measure touch accuracy or power-loss behavior.

## Native media browser

LCD builds use the queued native-media store with READ permission and a distinct
client ID. The volume must already be mounted through the media service. Opening
the browser never formats media. An unavailable volume is reported in the view.
Pages hold at most 16 entries and reads at most 1024 bytes. Previous-page history
holds eight page positions. Directories are name prefixes; an indexed seek beyond
a child prefix skips its descendants. Entry IDs are catalog ranks plus one.

`TIKU_MEDIA_NAMES_ENABLE=1` enables boot-scoped visible-name versions and queued
`ENUMERATE`/`OPEN_VERSION` operations. Enumeration resolves and fetches from one
serialized catalog state. Content-only sync preserves the namespace version;
visible publication, finalization, deletion and remount invalidate it. Opening
a read checks the epoch, exact name and rank before pinning its snapshot. Queue
pressure advances older reads one bounded phase per owner turn. Cancellation
retires its queued request and any older read using the bridge before releasing
the SDK request. Snapshot closure also completes before release.

Text previews read at most 1024 bytes and display ten fitted lines. Names,
paths and preview lines use an ellipsis when their text exceeds the available
width. Binary or non-ASCII content uses a hexadecimal preview of at most
128 bytes. The calculator accepts a 64-bit
hexadecimal word, displays four bases and reports invalid digits and overflow.

`make test-media TIKUOS_ROOT=/path/to/tikuOS` checks a 4096-entry directory,
subtree skipping, page progress during content writes, stale identities, queue
pressure, reads, cancellation and failed-media cleanup. Its combined protocol
fixture checks private upload progress, busy release/resume, atomic publication
and snapshot bytes while the browser enumerates. These are host checks.
The S31 read backend reopens and power-cycles the card for each acquired
query, advancing identification and I/O through polls. Board measurements remain
required for real-card responsiveness. Mount, external media writes and maintenance retain their synchronous service
contracts.

## LCD qualification provider

Use `tools/qualify_lcd.py` as `--application-test` in the same flash/restore
workflow, selecting the `tikusurface_s31_lcd` image. The provider requires seven
startup allocation refusals, completed handoffs, 100 lifecycle cycles, console
and clock progress, PSRAM restoration and the dispatch/stack thresholds. Its
report identifies the scope as `lcd-lifecycle`; touch and physical latency
remain unqualified. The diagnostic provider continues to refuse LCD images.
Physical grid/edge contacts, drag/release, text readability, complete-frame time
and storage latency require separate observations on the panel and card.

## Resumable S31 storage transport

LCD builds enable `TIKU_S31_SD_ASYNC_ENABLE=1` and retain the public SD open,
clock, sector-read and cancellation operations in the final image. The SPI
backend observes one configuration/transfer phase per poll and exchanges at
most 64 bytes per FIFO request. SD power delays, command responses and data
tokens progress between polls. Sector output is copied after CRC validation;
errors and cancellation close the read-only session before releasing buffers.

The S31 media backend adds caller-owned, CID-bound read sessions with checked
extent translation and 1..8-sector reads. A session retains the card across
reads and requires explicit close. State belongs in caller-provided storage;
these APIs allocate no heap memory. Synchronous block callbacks refuse an
asynchronous session. Invalid-context refusals retain ownership for retry.

`make test-transport TIKUOS_ROOT=/path/to/tikuOS` checks independent register
completion, CRC, deadlines and clock wrap, delayed identification, cancellation,
foreign bus ownership, media identity and extent limits. These are simulated
transport checks. LCD builds also enable `TIKU_MEDIA_ASYNC_ENABLE=1`. The
browser uses `tiku_media_card_read_step()` to advance one queue, transport or
filesystem phase per provider call. Catalog and extent validation consume one
sector per step; payload CRC consumes at most 512 bytes and a completed slice
copies at most 4096 bytes. Query and card state live in the PSRAM bridge.

A running query serializes the store while allowing enqueue. Metadata is
published after transport release. A pinned reader retains the file version
through later content writes. The read worker handles ENUMERATE, OPEN_VERSION,
READ_AT and CLOSE. Writes and maintenance remain queued for their existing
owner; the browser never executes them. A pending media request keeps bounded
runtime continuations active after painting completes. Responsiveness under
external synchronous writer turns still requires qualification. These operation
bounds do not establish physical dispatch time or card throughput.

The product linker places asynchronous transport and transfer code in IROM.
SD CRC/context helpers, the synchronous SPI wait, media-backend initialization
and CID packing retain SRAM placement. Region checks and whole-image resident
accounting cover this selection independently of hardware timing.


## Polled calibration persistence

LCD builds enable `TIKU_TFS_ASYNC_ENABLE=1` and
`TIKU_S31_STORAGE_ASYNC_ENABLE=1`. Each runtime owns a 512-byte aligned bridge
in its PSRAM reservation. It retains the encoded settings record, TFS state and
S31 region transaction until durable completion. TFS admits one writer, copies
payload in at most 256-byte requests and publishes the aligned commit word
after the payload and metadata complete. Ambiguous backend errors unmount TFS
until recovery.

The region writer retains the sector undo record through the existing paired
mirrors. Polls advance CRC work in 256-byte pieces and flash programs in at most
64-byte page-bounded commands, with read verification on a later turn. The
mapped region publishes after the cleared undo record is durable. Other region
writers and mirror flushes are refused during that transaction.

The flash controller suspends automatically for cached reads and resumes only
when the storage owner polls the suspended operation. Cached execution and PSRAM
access remain available while erase/program is pending. Admission checks the
recorded JEDEC ID and SFDP suspend commands and timing. Unsupported flash is
refused. Program and erase use separate resume delays for the admitted
64/512 us profile; suspend latency must be at most 50 us. Controller
configuration is restored after the operation retires;
command/progress timeouts halt with ownership retained. The product places
this orchestration in IROM and retains the controller-busy query in SRAM for
the synchronous storage path.

Connected profiles enable the driver's linked-image bounds check. The station
entry must fall inside the firmware's mapped code segment, including product
code that precedes the radio libraries. The qualification provider validates
boot identity before requesting a new prompt when startup logs follow the
shell's first prompt.

Settings progress alternates with rendering and queued media work; due touch
polls take priority. Stop cancels an accepted save and drains its backend
before releasing PSRAM. Cancellation after commit submission finishes that
transaction. A queued save that has not begun is discarded on stop.

`make test-nvm-backend TIKUOS_ROOT=/path/to/tikuOS` runs the simulated controller,
region undo/recovery and composed settings/TFS/flash checks. For sanitizer
coverage, use the same target with a separate `BUILD` directory and
`CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie'`.
The real-TFS runtime sanitizer check uses `make -f mk/tikuos-test.mk
settings-test` with those variables after `make sdk-host`. These fixtures do
not establish physical power-loss recovery or flash timing. The calibration
qualification provider measures reset retention and dispatch on the board.


`tools/qualify_endurance.py` uses the protected runner with `--runs 1`. It
observes 7200 seconds of active Home dwell across 100 lifecycle cycles, with
calculator, browser and device-information launches between dwell intervals.
One-second samples check runtime retention, display counters, timer progress
and the dispatch limit. Each stop must return PSRAM to the initial baseline.
The provider reports completed cycles as it runs. Its scope is local LCD idle
and periodic switching; it does not qualify physical input, transfers or
mounted-card integrity. The runner's `--seconds` controls boot capture, while
this provider defines the two-hour active observation period.

The general-storage and encrypted Wi-Fi-settings ROM paths both refuse
controller access while an automatic flash operation is pending. Calls are
serialized in the kernel owner, and refusal occurs before masking interrupts
or changing caches. `test_flash_ownership.py` exercises that interlock with
host models for the two IRQ instructions; the remaining C control flow is
compiled from the production sources.


## Connected service observations

Connected LCD builds enable `SURFACE_CONNECTED_ENABLE=1` and
`TIKU_MEDIA_TRANSFER_STATUS_ENABLE=1` and `TIKU_WIRELESS_OBSERVE_ENABLE=1`.
Device information reads cached Wi-Fi
state and the transfer listener's typed snapshot at 250 ms intervals. It
reports unavailable, connecting, joined, failed, scanning and disconnecting
radio states. Transfer observations distinguish stopped, console-ready,
listening and connected states, upload committed/expected bytes and download
read-position/length. Committed upload progress excludes staged input. Read
progress describes reply preparation, not peer receipt.

The radio observation copies four cached flags from the kernel owner. It
performs no vendor calls, waits, allocation or radio I/O. The synchronous
wireless status API retains its separate RSSI query contract. The 250 ms
interval limits sampling frequency; physical dispatch time under combined
radio, display and storage activity still requires board qualification.

Snapshots perform no transport or storage work and copy no credentials.
Unavailable backing or a latched session/disconnect fault reports the transfer
as unavailable. The binding reads snapshots in the kernel owner and stops
sampling when the runtime stops. The label and polling deadline live in PSRAM.
Changed labels repaint Device information; an in-progress frame retains its
frozen copy. These observations do not start a listener, change credentials,
connect a radio or write media.

`make test-services TIKUOS_ROOT=/path/to/tikuOS` exercises the typed server
snapshot over the existing transfer fixture and tests bounded labels with
independent radio/session observations. Sanitizer coverage uses
`make -f mk/tikuos-test.mk services-test` with a separate `BUILD` and sanitizer
`CFLAGS` after exporting the normal host SDK. `make test` and `make test-asan`
also exercise connected runtime polling and frozen-frame repaint behavior.
The real-driver fixture traps vendor RSSI access while the GUI observes a
joined radio and checks cached flags and kernel/interrupt-context refusal.
