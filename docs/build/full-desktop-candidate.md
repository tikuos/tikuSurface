# Desktop application candidate

The published foundation pin builds the base three-application product. The
expanded desktop uses an explicitly exported candidate Stomme SDK and matching
TikuOS storage, console and device interfaces. It is not a qualified release.
The source repositories and dependency pin must be reviewed together before
adoption. Product code consumes exported headers and archives.

Enable the candidate with these make options:

```sh
SURFACE_TEXT_INPUT=1 SURFACE_FILE_JOBS=1 SURFACE_ARITHMETIC=1 \
SURFACE_CONTENT_JOBS=1 SURFACE_EDITOR=1 SURFACE_TERMINAL=1 \
SURFACE_FILE_INFO=1 SURFACE_BASIC=1 SURFACE_DEVICE=1 SURFACE_VFS=1 \
SURFACE_READER=1 SURFACE_DIRECTORIES=1 SURFACE_HEX=1 \
SURFACE_DOCUMENTS=1 SURFACE_PREFERENCES=1 SURFACE_DIFF=1 SURFACE_TRANSFERS=1 SURFACE_NOTES=1 SURFACE_EDITOR_DOCUMENTS=1 SURFACE_EDITOR_LINES=1
```

Pass the options to `make -f mk/host.mk SDK_ROOT=/path/to/candidate-sdk test`.
Media integration targets use `make -f mk/tikuos-test.mk`, the same options and
`TIKUOS_ROOT=/path/to/matching-tikuOS`. The firmware build accepts these options
through `product/tikuos.mk` and uses `SURFACE_SDK` for the target SDK export.

Developer Calculator supports exact words, bit ranges, 255-byte literal/hex
checksum input and labeled clipboard results; see [checksum controls](word-checksums.md).

The twelve retained applications are Developer Calculator, Tracker, Settings,
Calculator, Editor, Terminal, Device Monitor, VFS Inspector, Reader, TikuHex, TikuDiff and Notes.
Tracker provides queued create, rename, copy, move and delete operations,
persistent directories, atomic subtree operations and explicit volume migration.
Copy admits 64 KiB per file. Opening documents uses shared desktop handler
matching and saved compatible preferences.

Editor retains eight independent files and provides UTF-8 editing through 8 KiB each, selection, clipboard, history,
retained literal search/options, replacement, Go to line and checked save.
Editor can run or stop its draft through an owned TikuOS BASIC job and Terminal
output. TikuHex edits binary documents through 8 KiB with history, search,
navigation, explicit hexadecimal/ASCII keyboard editing and checked publication. Reader supports admitted local HTML,
Markdown, text and help, with literal Find, wrapped match navigation and highlighting. Monitor and Inspector expose typed device/process and
VFS observations; supported VFS writes require review. Settings persists touch
calibration and independent desktop/document preferences. Its
[device controls](device-preferences.md) provide explicit reads and reviewed
updates through the checked firmware provider.

TikuDiff retains a pair of file snapshots with shared comparison options, change
navigation and binary identity checks; see [file comparison](diff-candidate.md).
[Notes](notes-candidate.md) retains 24 documents with paper, importance and checked
autosave. The other document applications keep one retained document each. Additional desktop
applications, richer CodeView behavior, dynamic VFS enumeration
and physical display/touch/latency/endurance acceptance remain open. Host and
cross-link results do not establish these gates. The
[Editor](editor-candidate.md), [document routing](document-routing.md) and
[desktop preferences](desktop-preferences.md) and [Hex keyboard](hex-keyboard.md) documents describe their controls
and admission rules.

Settings provides [transfer controls](transfer-controls.md) for a console-provisioned
endpoint. The connected service progresses independently of the foreground app.
