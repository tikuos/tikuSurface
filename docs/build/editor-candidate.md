# Editor candidate

`SURFACE_EDITOR=1` adds Editor to the desktop and Applications menu. It requires
`SURFACE_CONTENT_JOBS=1`, `SURFACE_FILE_JOBS=1`, `SURFACE_TEXT_INPUT=1`,
`SURFACE_ARITHMETIC=1` and an explicit Stomme SDK with `editor-document`,
`editor-control`, `editor-literal-search` and `bounded-pool` capabilities.
The foundation lock continues to select the published release SDK.

Open a Tracker preview and choose Edit text. Editor accepts UTF-8 text through
8192 bytes, including line endings. Invalid text and larger files preserve the
current document. With `SURFACE_EDITOR_DOCUMENTS=1`, eight independent documents stay open while
other applications run. This composition requires `SURFACE_NOTES=1`. Tracker
opens another file in a retained session and focuses an already-open draft.
Documents lists files, dirty state and pending operations. New document and
Command-N create a session. Close current confirms unsaved changes and retires
borrowed storage before releasing the model. Closing Editor checks every draft.
Without this flag, Editor retains one document and requires saving or discarding
its edits before opening another file.

Type opens the touch keyboard for insertion at the selected caret. The document
view supports touch selection, keyboard movement, multiline text, Undo, Redo,
Copy, Cut, Paste and literal Find with wrap. Search tools retains the query,
case and whole-word options, forward/backward repeat search, selected-match
replacement and atomic Replace all with one undo step. Empty replacement deletes
matches. Cancel and failed replacement preserve the document. Go to line accepts
an existing one-based line number. Newline inserts a line break.

Command-G repeats forward, Command-Shift-G repeats backward, Command-L opens
Go to line, Command-H replaces the selected match and Command-Shift-H replaces
all matches. Search tools has touch controls for these commands and Select all.
Back and Escape return from Search tools to the document.
The shared desktop document engine owns editing and history. Physical typing
joins adjacent edits into undo steps. The clipboard holds 255 bytes; copying a
larger selection is refused. Non-ASCII text remains in the document and is
rendered with replacement cells by the current font subset. Regular-expression
search and full desktop CodeView rendering remain outside this candidate.

Save replaces the current file at its observed catalog version. Save as creates
an unused absolute path through 64 bytes. A concurrent catalog change refuses
Save; Save as can preserve the draft under another name. The complete save
input remains immutable while storage owns it. Closing a modified document
requires Keep or Discard. Discard during a save cancels the request and retains
its memory until retirement. An interrupted save may require remount and refresh.

The shell owns a 128 KiB bounded document pool, or 1 MiB with Notes enabled. Retained document sessions
share that allocator and keep it bound until their last model is released. Each
document has a 16 KiB history target. The Notes composition reserves 6287360 UI bytes. Retained Editor documents
admit 160 runtime allocation records and retain the shared 2048-slot pool.
The complete Diff/Notes composition allows 384 KiB UI IROM and requires
at least 64 KiB whole-image code reserve. Added SRAM remains limited to 2048 bytes, excluding the separately
counted display descriptors. Target links establish placement and allocation;
physical touch, display, latency and storage qualification remain required.

`SURFACE_EDITOR_LINES=1` adds Edit tools with shared native duplicate, move up,
move down and delete-line transactions. The selected lines use one undo step;
a selection ending at the following line start excludes that line. The commands
preserve UTF-8 and existing line breaks. Final-line duplication inserts LF,
matching the Editor insertion setting. Refused growth preserves the document.
Go to line remains in Search tools and Command-L. This composition requires the
`editor-line-transactions` SDK capability and admits 448 KiB UI IROM while
retaining the independent 64 KiB whole-image reserve.

Edit tools also provides Indent and Unindent through the shared native
`editor-line-indentation` capability. Indent adds four spaces to each touched
nonempty line; a single empty line can be indented. Unindent removes one leading
tab or up to four leading spaces. Multiline operations select complete touched
lines, retaining selection direction. Each operation has one undo step.

Tab indents a multiline selection or inserts four spaces over a single-line
selection. Shift-Tab unindents the touched lines. These keys use the shared
CodeView transaction helper; adjacent typing retains the document history policy.
