# Desktop and document preferences

`SURFACE_PREFERENCES=1` requires `SURFACE_DOCUMENTS=1`. Settings opens a
Desktop / files page with six background colors and compatible default handlers
for text, HTML and Markdown. Each handler button cycles through its choices.
Save applies the changes after persistence completes. Defaults prepares a new
draft. Closing unsaved changes offers Keep or Discard. Changes stay available
while switching applications; pending saves prevent closing Settings.

Open with names the saved default and retains alternative handlers. Unknown
file types still require an explicit choice. Text and binary document limits
remain those of the selected application.

The desktop record is 32 bytes with a version and CRC, stored under
`tikusurface-desk-v1`. Calibration retains its independent `tikusurface-cal-v1`
record and format. Both use one polled TFS/NVM bridge in the runtime's PSRAM.
Overlapping saves are refused, and the five-second save interval applies to
both record types. A failed save preserves applied preferences; cancellation
retires the storage transaction before runtime memory is released. Restart reads
the last valid committed record. Unsupported schemas are reported and preserved.

Host tests use deterministic preference providers. Real-storage tests simulate
TFS writes, S31 flash transactions and restart recovery. Those tests do not
establish physical flash power-loss behavior or panel readability. Font sizing,
icon sizing, window policy, sound and device-specific controls remain separate
work.
