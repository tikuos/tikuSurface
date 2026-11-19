# Operating the S31 product

Select `S31_SURFACE_LCD=1` to build the panel runtime. The default diagnostic
image provides console-controlled lifecycle checks with deferred presentation.
The image identities are `tikusurface_s31_lcd` and `tikusurface_s31_diag`;
qualification providers check the selected identity before sending commands.
See the [build instructions](../build/README.md) for the SDK and target flags.

## Navigation and applications

The launcher opens Word calculator, Media browser or Device information.
Home returns to the launcher and releases an application's pending work after
its providers retire. Back returns from a file preview to its folder, from a
folder to its parent, or from an application root to the launcher. Controls
activate when the primary contact is released inside the pressed control.
Dragging outside cancels the action. Additional contacts do not activate it.

The calculator accepts a hexadecimal word of up to 16 digits and displays
hexadecimal, decimal, octal and binary forms. Invalid digits and overflow have
visible error states. Backspace removes a digit and Clear resets the word.

The browser reads an already mounted native-media volume. It does not format
or write a card. Refresh restarts a listing after media changes or a stale
identity. Content writes can preserve the current directory position; adding,
removing or renaming visible entries invalidates its enumeration version.
A missing volume displays an unavailable state with Home and Refresh usable.
Text previews show printable content; binary/non-ASCII content uses a bounded
hexadecimal preview. The host preview uses fixture files instead of a card.
Names, paths and preview lines are fitted to their columns with `...` when
text is omitted. The stored name and selected object identity remain intact.
Fitting preserves UTF-8 code points; characters outside the pinned font subset
render as `?`. This font set does not provide complete language coverage.

Connected LCD builds show Wi-Fi and transfer status in Device information.
Upload progress counts committed data; read progress counts prepared reply
bytes. An unavailable radio or transfer service leaves the local applications
available. This view observes services; it does not start a listener or change
network settings.

## Calibration

Device information offers axis/orientation controls and an explicit Save.
Initial raw bounds are 0..799 horizontally and 0..479 vertically. The panel's
measured bounds can be supplied through `/surface/calibration`; the runtime
binding documents the seven-field command format. A valid save is queued and
applied after durable completion. While pending, the active transform stays
unchanged. Save attempts are spaced by at least five seconds.

Calibration is stored in `/data/tikusurface-cal-v1`. Startup validates the
record's version, size, geometry and checksum. Invalid data selects defaults;
an unsupported version also refuses replacement. Save failure retains the
active transform. A failure after the commit may leave a complete new record
on flash; restart validates that stored record again. The product does not
write settings during pointer motion or startup.

Stopping cancels an uncommitted save and drains accepted flash work before
releasing its memory. A submitted commit completes before stop retires the
runtime. Start is refused until retirement ends. Calibration storage does not
use the Wi-Fi credential reservation.

## Runtime observation and recovery

The UART shell exposes `/surface/status`, `/surface/display`, `/surface/touch`,
`/surface/calibration` and `/surface/psram_free`. Reads observe state without
advancing the runtime. `/surface/control` accepts `start`, `stop`, `launch=0..3`
and allocation-failure injection. The [runtime binding](../../platform/tikuos/README.md)
lists exact syntax, permissions and counter semantics.

A failed display submission retains its buffers until DMA retirement is
established. The error remains visible until close/reopen. A storage backend
failure can make the file store unavailable until boot recovery; retrying a
UI save is not a substitute for that recovery. Stop must reach `running=0`
with zero live allocations before its reservation is considered released.

Qualification uses TikuOS's protected flash runner with the recorded image,
full factory backup hash, board serial and explicit UART port. The runner
checks the installed boot layout, records the protected data, loads the test
application and verifies restoration afterward. Do not run a second owner on
the UART during that execution. A provider result and its restoration result
are distinct: both are required in a qualification record.

`qualify_calibration.py` requires two boots and verifies an acknowledged record
after runtime restart and board reset. `qualify_lcd.py` requires 100 lifecycle
cycles. `qualify_endurance.py` requires one boot, 100 cycles and two hours of
active Home observation between application switches. These providers do not
observe physical touch accuracy, screen readability or input-to-visible latency.
Mounted-card and connected transfer integrity require their own workloads.
