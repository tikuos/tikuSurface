# First use and recovery

TikuSurface starts at Home on an installed S31 LCD image. Local applications
work without Wi-Fi. Media browsing requires a mounted TikuOS native-media card;
the product does not mount or format one at startup. Network credentials and
transfer access are provisioned from a computer.

## Prepare the device

Select `s31-local` or `s31-connected` with `S31_SURFACE_LCD=1` using the
[build guide](../build/README.md). An installed diagnostic image does not drive
the panel. The build and protected qualification procedures are separate from
the everyday workflow below.

Connect the S31 UART0 bridge at 115200 baud when setup or recovery needs the
console. Use the explicit device path for that bridge. LCD operation shares
pins with USB Serial/JTAG, so the UART0 bridge supplies the console during
display use. Close other programs using the port before running a host tool.

In the TikuOS shell, inspect the running product:

```text
cat /surface/status
cat /surface/display
cat /surface/calibration
```

`running=1 stopping=0` identifies an active runtime. `enabled=1` identifies
the LCD configuration. If the runtime is stopped, `write /surface/control start`
requests startup; read status again. If startup is refused, retain the console
error and follow the [runtime recovery guide](README.md#runtime-observation-and-recovery).
A display handoff counter does not establish that the panel is visibly correct.

## Use the local applications

At Home, open Word calculator, enter a hexadecimal value and inspect the other
bases. Use the width and input-base controls to change the word format; Swap
bytes reverses its byte order. Use Home to return and Applications to resume.
This application operates on unsigned words; it does not provide arithmetic
expressions, bit-field editing or checksums.

Open Device information and compare its build revisions with the installed
build record. Use the orientation controls only when the displayed touch
positions need adjustment. A changed draft is labelled unsaved. Save applies
the mapping after storage acknowledges it. Defaults restores the editable
panel bounds and orientation; Save remains a separate action. Home and switching
retain the draft. Close offers Keep editing or Discard when changes are unsaved.

After changing orientation, check Home, Back and controls near each edge.
If the new map prevents navigation, use the UART recovery procedure below.
An acknowledged save establishes persistence completion, not touch accuracy.

## Prepare and browse media

Use a card that already contains a TikuOS native-media volume. FAT/exFAT cards
do not become native-media volumes by mounting them. Creating a native volume
is a separate, destructive provisioning operation in TikuOS; it is not part of
this first-use sequence.

Finish active transfers, close Media browser and let its requests retire before
media maintenance. Returning Home retains the browser. In the TikuOS shell:

```text
media off
media mount 4000000
media info
```

Proceed when the media command reports `result=0` and `media info` identifies
the expected volume. `media off` stops the transfer listener; it does not
unmount or erase the card. If a command reports busy, let its current owner
finish and inspect again. A mount failure is not a request to format the card.
The 4 MHz mount setting is the command's default; higher rates require
qualification on the selected card and wiring.

Open Media browser and select Refresh. Open a folder and a file, then use Back
to return through the path. The browser reads files and does not rename,
delete or write them. An empty folder, unavailable volume and changed listing
are different states. Refresh retries the current listing; it does not mount
a card. For a changed or removed card, finish the outstanding operation, return
Home and repeat the explicit mount/identity check with the intended card.

Media mount and transfer maintenance have synchronous service paths. Performing
them from the console while Surface runs does not establish bounded UI latency.
Combined media/display operation requires its separate timing qualification.

## Provision connected operation

Use the connected build and TikuOS's existing Wi-Fi provisioning instructions
in `services/storage/media-transfer.md` and `drivers/wifi/esp/s31/README.md`.
On S31, the documented shell sequence uses `wifi on`, `wifi connect` and `ip`;
wait for the assigned address. Credentials belong to the radio service and
are not entered through TikuSurface. Keep credential-bearing setup transcripts
outside product screenshots and qualification reports.

The TikuOS host tool `tools/tiku_media_transfer.py` provisions a listener and
access key with its `serve` command, then transfers files through its TCP
commands. Its `--bridge-serial` argument selects the S31 bridge. Close the
interactive terminal before it opens that port. The service guide documents
the exact command arguments, key-file handling and transport semantics.

Device information displays cached Wi-Fi and transfer observations. It does
not create a listener, provision keys, start a transfer or stop one. Local
navigation stays available when the observer reports unavailable services.
Console provisioning does not fulfill the planned GUI transfer controls.

## Recover an unreachable touch mapping

The console calibration endpoint is independent of the current touch transform.
From the TikuSurface checkout, inspect it without changing settings:

```sh
python3 tools/recover_calibration.py --port /dev/serial/by-id/DEVICE \
    --output /path/to/new-inspection.json
```

The command requires pyserial and an existing output directory. It opens the
selected UART exclusively with DTR and RTS deasserted and does not reset the
board. Reports contain parsed status and commands, not raw console transcripts.
An existing report is never overwritten. Inspection requires a running LCD
runtime and makes no setting or navigation writes.

To restore panel defaults and return to Home, leave the panel untouched and run:

```sh
python3 tools/recover_calibration.py --port /dev/serial/by-id/DEVICE \
    --defaults --output /path/to/new-recovery.json
```

This explicit action persists `0,799,0,479,0,0,0`. It refuses pending saves,
unavailable settings and incompatible records. During a save it checks that
the active mapping remains unchanged until acknowledgement, then requests
Home and checks that the transition completes. It does not rewrite defaults
that are already acknowledged as saved. It does not restart or flash firmware,
format media, alter radio settings or delete an unsupported record.

If the report says the save outcome is unknown, inspect again before another
write. A timeout or lost UART reply cannot establish whether storage committed.
If storage failed, retain the report and use the runtime/storage recovery path.
An incompatible record requires firmware/schema compatibility work; Defaults
does not overwrite it. Once recovery completes, verify physical touch positions.

If panel defaults do not fit the controller coordinates, use `/surface/touch`
to observe raw contacts and the seven-field format documented in the
[runtime binding](../../platform/tikuos/README.md#console-diagnostics).
`x` and `y` retain the last nonempty report; `contacts=0` is not a new contact
sample. Raw bounds describe controller axes before swap and inversion. Collect
measured bounds and orientation before writing a custom map.

On the native desktop the Tiku menu's "Calibrate Touch..." does this on the
panel: it asks for a tap on each of nine crosses, fits the map from the taps
and puts it in force. A tap on its Keep box within ten seconds stores the
map; without one the stored map comes back, so a calibration that makes the
panel hard to use undoes itself. Taps that fit no map change nothing. From
the console, `target=X,Y` on `/surface/control` shows a ring to aim a finger
at, and `/surface/touchlog` gives the point each tap landed on.
