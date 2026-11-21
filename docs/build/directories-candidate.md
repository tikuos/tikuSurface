# Persistent folder candidate

`SURFACE_DIRECTORIES=1` adds New folder and folder selection to Tracker. It
requires `SURFACE_CONTENT_JOBS=1`, the SDK `paged-storage-directories` and `paged-storage-subtrees` capabilities
plus `paged-storage-volume-features` and `paged-storage-tree-copy`, and matching firmware built with `TIKU_MEDIA_DIRECTORIES_ENABLE=1`.

A directory-enabled MFS volume stores empty folders as typed catalog objects.
New folder, subtree rename/move and confirmed permanent deletion use the
queued sector-paced storage owner. Select chooses a folder for actions; a
normal tap enters it. File creation can traverse persistent folder parents.
File replacement refuses a directory, and file readers cannot read one.

Provision a new private extent with `tiku_mfs_format_directories()` only after
storage ownership and formatting are authorized. Mount never reformats or
upgrades existing media. Ordinary `tiku_mfs_format()` creates the legacy format;
New folder on such a volume reports unsupported and offers Enable folders to a
local host-control caller. The confirmation explains the older-firmware limit.
Keep current performs no migration. Enable checkpoints the existing catalog,
retires legacy headers and preserves file payloads. After completion, use New
folder again. An interrupted operation can require remount and explicit resume
through the same control. A mixed-feature mount permits reads and refuses
ordinary writes until migration finishes.
Directory volumes carry a mandatory feature bit that older firmware rejects.
Do not roll back to a reader that lacks this feature while expecting access to
that volume. Migration is explicit; mounting alone does not write feature metadata.

Folder rename/move and deletion include descendants. The owner validates the
original catalog, edits unpublished candidate roots and publishes one journal
transaction. It refuses occupied destinations, pending uploads, moves into the
source tree and mapped paths longer than 63 relative bytes. There is no merge.
Delete requires explicit confirmation for the folder and its contents.
Prefix-derived folders on legacy volumes support these subtree operations;
their listed rank and epoch identify the selection. No format upgrade is needed.
Reader snapshots retain their original contents through rename or deletion.
Cancellation after allocation can require remount; it does not imply rollback.
Copy to duplicates a selected typed or prefix-derived folder at an absent
absolute destination. It preserves source identities and payloads, creates new
destination identities and publishes the complete destination tree once. The
copy includes empty typed folders and nested files. It borrows one 65536-byte
PSRAM transfer buffer; each file must fit that buffer. Oversized descendants and
mapped-path violations refuse the job before destination allocation. Payload
slices and whole-file digests are checked before publication. Progress reports
prepared payload bytes; they become visible after successful completion.
Read and write authority are both required. Closing or cancelling retains the
workspace until the provider retires. Cancellation can require remount; a
published destination is not rolled back.
The source flag defaults to zero. The published SDK pin does not yet provide
this candidate export.

The private-media integration is available with the candidate SDK and firmware:

```sh
make -f mk/tikuos-test.mk SDK_ROOT=/path/to/sdk TIKUOS_ROOT=/path/to/tikuOS \
  SURFACE_TEXT_INPUT=1 SURFACE_FILE_JOBS=1 SURFACE_CONTENT_JOBS=1 \
  SURFACE_DIRECTORIES=1 BUILD=build/directories \
  tracker-directories-test media-directories-test media-subtrees-test \
  media-directory-upgrade-test tracker-directory-upgrade-test \
  media-tree-copy-test tracker-tree-copy-test media-card-capacity-test
```

These checks model persistence, queue ownership, namespace conflicts, interrupted
writes and GUI controls. They do not qualify SD power loss or physical touch.
