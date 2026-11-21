# Operating the S31 product

Select `S31_SURFACE_LCD=1` to build the panel runtime. The default diagnostic
image provides console-controlled lifecycle checks with deferred presentation.
The image identities are `tikusurface_s31_lcd` and `tikusurface_s31_diag`;
qualification providers check the selected identity before sending commands.
See the [build instructions](../build/README.md) for the SDK and target flags.

The [first-use and recovery workflow](first-use.md) covers boot checks, card
preparation, external network provisioning and touch recovery through UART.

## Navigation and applications

The launcher opens Word calculator, Media browser or Device information.
Home returns to the launcher and retains open applications and their work.
Applications lists the available apps and marks open instances; selecting one
restores its state. Close ends the foreground application after its providers
retire. Unsaved settings require Keep editing or Discard; a pending save must
finish before Close is available. Back returns from a file preview to its folder, from a
folder to its parent, or from an application root to the launcher. Controls
activate when the primary contact is released inside the pressed control.
Dragging outside cancels the action. Additional contacts do not activate it.

The word calculator supports 8-, 16-, 32- and 64-bit unsigned words. Tap the
width button to cycle widths; a narrower width that cannot hold the word is
refused. Tap the input-base button to cycle hexadecimal, decimal, octal and
binary while preserving the value. Swap bytes reverses byte order at the
selected width. Four result fields show the value in each base. Invalid digits
and overflow leave the word unchanged and display an error. Backspace removes
a digit and Clear resets the word. Width, base and input survive app switching.

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

Device information displays the selected product and TikuOS revisions. A
`-modified` suffix identifies a build from a modified working tree. Host previews
display the product revision and the host designation.

## Calibration

Device information offers axis/orientation controls, Defaults and an explicit Save.
Defaults resets the editable bounds to 0..799 and 0..479 and clears axis swaps
and reversals. It requires Save to persist or apply the change. Home and switching
retain an unsaved draft; Close asks before discarding it. Pending saves and unavailable or incompatible settings
disable these controls. Defaults requires a usable input mapping to reach it.
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
