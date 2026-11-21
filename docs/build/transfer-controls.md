# Provisioned transfer controls

`SURFACE_TRANSFERS=1` adds a Transfers page to Settings. The connected LCD
profile binds it to the independently scheduled TikuOS TCP transfer service.
The local profile displays an unavailable provider. The candidate SDK and
matching firmware are selected as described in
[Desktop application candidate](full-desktop-candidate.md).

Provision an endpoint from the host before using Enable:

```sh
python3 tools/tiku_media_transfer.py --serial /dev/ttyUSB0 \
    --key-file transfer.key provision --port 7331
```

Run this command from the matching TikuOS checkout. Its `provision` operation
uses the selected serial console, a nonzero TCP port and a key file. It creates
the key file with mode 0600 and configures a stopped listener. `serve` provisions
and enables the endpoint, waiting for readiness when startup is asynchronous.
The GUI receives a profile generation and copied state; credentials stay in
the service. The existing authenticated transfer protocol uses the same key.

**Enable** starts the provisioned listener. **Stop** removes the endpoint and
waits for owned I/O, connections and session cleanup. An active connection,
upload or read requires confirmation. The confirmation retains the observed
profile generation so a changed profile cannot be stopped by an old dialog.
**Keep running**, Back and Home dismiss that confirmation. Closing Settings or
returning Home leaves the service running.

Accepted upload bytes and committed bytes have separate counters. Prepared read
bytes describe the service's progress, not peer acknowledgement. Recovery,
startup, shutdown and command failures have separate state. Refresh reads a
copied observation without storage or network I/O. The shell retains command
tokens after leaving Settings and consumes their results before shell teardown.

The service advances queued media and deferred TCP operations in its own TikuOS
process. Network output remains the network owner's responsibility. Stop
preserves provisioned credentials; `media forget` releases provisioning after
cleanup and consumed command results. Shared foreign media requests finish
before service backing is released.

Host fixtures cover typed ownership, cancellation, stale confirmation, retained
commands and lifecycle failures. Target links check placement and resource
admission. Physical network, storage, touch and dispatch timing remain separate
qualification gates.
