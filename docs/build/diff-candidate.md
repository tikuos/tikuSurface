# File comparison candidate

TikuDiff compares two retained media files with Stomme's shared line pipeline.
Enable `SURFACE_DIFF=1` with the other options in
[Desktop application candidate](full-desktop-candidate.md). The exported SDK
must provide `diff-lines-progress` and `diff-unified-progress`; the published foundation pin does not
provide this composition.

Choose **Left file** and **Right file**, then enter absolute media paths. Each
file admits 8192 bytes and 256 lines. Reads bind the file identity, namespace
cursor, length and content version. The previous pair stays available if a
replacement is cancelled, unavailable, stale or exceeds either limit. Loaded
snapshots remain unchanged when files are subsequently modified on media.

The two columns show retained, removed and added lines. **Ignore spaces** folds
whitespace runs and trailing whitespace using the native comparison rules.
**Ignore final CR** treats CRLF and LF line endings alike. Final-newline
presence remains significant. **Swap** exchanges the loaded sides and compares
again. Files containing a NUL byte receive an exact binary identity comparison;
TikuHex provides byte inspection and editing.

**Page up/down** scroll the comparison. **Prev diff/Next diff** visit change
runs. **Text </Text >** scroll long lines horizontally in 32-byte steps, with
complete UTF-8 boundaries in the preview. Keyboard Up/Down and Page Up/Page Down
scroll vertically; Left/Right visit changes; Shift+Left/Right scroll text.
Command+O opens the left path and Command+Shift+O opens the right path.

File reads use the queued storage provider. Parsing and comparison advance in
bounded owner turns. **Cancel** retires pending I/O or cancels CPU work. Closing
also drains outstanding storage leases. Backgrounding retains the pair and
allows active work to finish. Painting copies the visible text into the frame
snapshot, preserving clipped rendering while another file is loaded.

The Diff composition reserves 4190208 PSRAM bytes, including unchanged render
surfaces, a 1048576-byte application allowance, scratch and metadata. It provides
80 allocation records. The UI IROM allowance is 393216 bytes, with at least
65536 bytes of whole-image IROM reserve. Product-required SRAM growth remains
limited to 2048 bytes, separately from fixed display descriptors. Target links,
combined application allocation tests and physical timing are distinct gates.

Tracker provides **Open with... > Compare...**, followed by **Left file** or
**Right file**. This passes the selected catalog identity to Diff. A stale
selection, busy comparison or failed launch preserves the loaded snapshots.
Return to Tracker to select the other side.

Folder/archive comparison remains outside this candidate window. Its desktop-port checklist entries remain
open. Host tests and cross-links do not qualify physical touch or dispatch time.

## Unified patches

Patch tools opens Save patch and Copy patch. The shared
`diff-unified-progress` serializer uses the compared snapshots, selected
normalization options and three context lines. Output names are the selected
media paths. Binary snapshots refuse text-patch export. Identical inputs produce
an empty saved patch; copying no differences preserves the clipboard.

Save patch takes an unused absolute media path and publishes the complete patch
through a queued create-only write. Existing destinations are preserved. The
32 KiB output buffer remains immutable until provider retirement, including
cancellation and close. Generating output advances at most 64 bytes or state
transitions per turn. The current file admission bounds the patch; capacity
refusal preserves both compared files. Copy patch admits 255 bytes and refuses
larger output without truncating or changing the clipboard. Command+S and
Command+C invoke these operations. Back leaves Patch tools.

The native allocating formatter and portable serializer use the same hunk
implementation. Independent parity, native patch-application checks and product
storage/cancellation tests cover the export path. Firmware runtime admission
also exercises a live export buffer with retained applications.
