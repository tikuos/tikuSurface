# Notes candidate

`SURFACE_NOTES=1` adds Notes to the expanded desktop. It requires the Diff and
persistent-directory compositions and an exported SDK with `note-file-format`.
Notes retains 24 independent documents with an 8192-byte UTF-8 body limit. The
reference desktop uses rich documents through 4 MiB; this candidate uses the
shared portable Editor model and displays its admitted capacity.

New note creates `/Notes` through the queued directory provider when needed.
Volumes without persistent directories refuse creation; Tracker provides the
explicit Enable folders operation. Startup reads native `note-*.txt` records.
Refresh retries failed loads and discovers additional notes while retaining
loaded drafts. The overview pages contain six notes with title, paper color,
importance, saved state and failures. Each document has selection, clipboard,
undo/redo, literal Find, replacement and Go to line. Note tools changes paper
and importance, duplicates a draft and confirms deletion.

Changes save after 500 ms without editing, including while Notes is backgrounded.
Save all requests publication of dirty documents. Failed saves retain drafts and
retry after five seconds. A save snapshots text and metadata separately from live
editing; successful publication marks that snapshot saved. Typing or metadata
changes during publication remain dirty. One note save runs at a time.

Before replacing or deleting a saved note, the provider refreshes its catalog
identity and compares the complete current record with the retained baseline.
Different contents refuse the operation. A catalog change between verification
and publication also refuses it. The stored format uses the native paper and
importance header, UTF-8 body and final LF. Files created outside Notes can cause
name conflicts; Refresh and Duplicate note preserve recovery paths. Closing
Notes with dirty documents asks whether to keep editing or discard their drafts.
Pending providers retain application leases through close and cancellation.

The Notes composition reserves 6287360 bytes of UI PSRAM, including unchanged
paired framebuffers, a 3 MiB application allowance and scratch/metadata space.
Editor sessions share a 1 MiB pool with 2048 internal slots; the runtime admits
128 allocation records. The host runtime stress includes 24 full-size notes,
12 applications, a 64 KiB copy and Reader replacement. Source and target
qualification remain separate from physical display, touch, latency, endurance
and storage power-loss acceptance.

Run `notes-test`, `note-io-test` and `runtime-lcd-test` through
`make -f mk/tikuos-test.mk` with the expanded desktop options,
`SURFACE_NOTES=1`, `SDK_ROOT` and `TIKUOS_ROOT`. Repeat with address and undefined
behavior sanitizers. The published dependency pin continues to build the base
product; candidate dependency adoption requires review.
