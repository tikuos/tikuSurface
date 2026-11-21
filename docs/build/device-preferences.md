# Device settings

The expanded Settings application includes a Device settings page. It exposes
device name, UTC epoch seconds, CPU target frequency in Hz, cryptography mode,
radio transmit power in dBm and the firmware's available CPU rates. A node may
be unavailable or read-only on a particular firmware build.

Choose a setting, then **Read** to obtain its current value. **Edit** opens the
review field; Apply requests a checked write. The provider rechecks node identity,
write capability and the reviewed value. CPU writes also check advertised rates
and whether the firmware supports live or reboot-dependent changes. The radio
driver validates the supported transmit-power range. The clock comparison
allows up to 300 seconds of forward movement since review.

A successful write is followed by a separate readback. A failed readback reports
that the setting was written; it does not report an unchanged device. A changed
value requires another read before editing. Missing-node errors can be retried
by returning to Device settings and selecting the control again. Copy transfers
a complete value only when it fits the desktop clipboard.

Settings owns this page's metadata and draft independently of VFS Inspector.
Selecting a setting does not read its value. Navigation does not write it.
Cancelling the field leaves the device unchanged. Closing Settings before a
queued write is dispatched cancels that write, following the Inspector model's
ownership contract. A dispatched write uses the firmware provider's result and
cannot be undone by closing the application.

Touch calibration and desktop/document preferences retain their independent
records and drafts. Device writes use each kernel node's persistence semantics;
the desktop preferences Save button does not commit device changes. The
application allocates its device page on first use and releases it with Settings.
Host/provider tests and target links do not qualify physical handler latency or
persistence on the board.
