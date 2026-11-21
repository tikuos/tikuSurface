# Local VFS Inspector

`SURFACE_VFS=1` adds VFS Inspector to a composition with `SURFACE_DEVICE=1`.
It uses the same exported candidate SDK as the device composition. The host
preview supplies a deterministic namespace; firmware uses the local TikuOS VFS.

Directories show six copied static or mounted children per page. Path entry,
Back, Previous, Next and Refresh navigate without invoking value handlers.
Dynamic child providers are identified; their enumeration is not available.
Selecting a file shows its read policy and permissions. Read once explicitly
invokes its handler, including a consuming or effectful handler. The preview
holds at most 1024 bytes and reports truncation. Copy requires a complete text
value fitting the 255-byte clipboard. No periodic value reads are performed.

Edit setting requires a complete text read and local write permission. The
adapter admits device name, CPU target, crypto mode, radio transmit power and
wall-clock settings. Names contain up to 31 ASCII letters, digits, underscores
or hyphens; crypto mode is `auto` or `sw`; transmit power is an integer from
-100 to 30; time is unsigned 32-bit seconds. CPU targets must match the device's
advertised frequency list and a live or reboot change mode. The device handler
can impose further constraints.

Before writing, the adapter checks the node identity, current permission and
reviewed value. Clock writes allow up to 300 seconds of forward advancement.
Caller capability is restored after dispatch. A successful write queues one
readback. A failed readback preserves the successful write indication and
requires refresh before another edit. Closing before dispatch drops a queued
operation; an already dispatched synchronous handler cannot be cancelled.
Persistent and hardware handlers retain their kernel timing contracts.

The application owns its metadata, value and draft buffers. One provider
operation runs per owner turn. Runtime allocation metadata expands to 64 records
inside the existing PSRAM reservation. Static metadata traversal is bounded;
value-handler timing and physical touch/readability require target qualification.
