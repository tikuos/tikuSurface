# Reader candidate

`SURFACE_READER=1` requires `SURFACE_VFS=1` and its prerequisite options. The
selected exported SDK must advertise `document-layout`, `document-parser-steps`,
`document-paint`, `markdown-subset`, `document-text-geometry` and
`literal-search-progress`. The published foundation pin does not
provide these capabilities. Candidate builds select the matching SDK explicitly.

Reader opens local HTML, Markdown and plain text through the same queued media
provider as Tracker. Supported suffixes are `.html`, `.htm`, `.md`, `.markdown`,
`.txt`, `.bas` and `.log`, without case sensitivity. Tracker's Read document
button passes the listed file identity and catalog epoch. Open path resolves a
fresh identity. Files larger than 8192 bytes, embedded NULs, changed catalogs,
unsupported suffixes and unavailable media retain the previous page with an error.
The provider's read permission applies independently of write permission.

The shared Stomme document parser, Markdown converter and painter provide text,
headings, lists, tables, links and the engine's CSS subset. Conversion output is
bounded to 65536 bytes. The fixed document tables report layout limits. The
portable font subset supplies fallback faces. Images are not decoded. Reader
performs no network access. Absolute and relative local links resolve within the
media root; anchors scroll within the document. Eight navigation positions
retain paths and scroll offsets. Back reopens files and therefore can report a
media error. Up and Down scroll 220 pixels. Help uses the same document lifecycle.

Loading, conversion and parsing advance on owner turns. A current page remains
visible until replacement publication. Cancel retires an outstanding provider
request before releasing its application lease; later opens remain available.
A painted frame holds a reference to its immutable page until its final row or
abandonment. Closing the application does not invalidate that frame.

Reader-enabled builds reserve 3928064 bytes of PSRAM for the LCD runtime. This
includes the existing paired surfaces, 786432 bytes of application allowance,
65536 bytes of scratch allowance and 4096 bytes of allocator metadata. Other
compositions retain their previous reservation. Reader permits 327680 bytes of
product IROM, with the whole-image 65536-byte reserve and 2048-byte resident
SRAM-growth limit unchanged.

The home screen uses a three-by-three application grid. The Applications menu
contains the same nine retained tasks. Physical readability, touch accuracy,
parsing/layout dispatch latency, active-service coexistence and endurance need
qualification on the resulting image. Stylesheet collection, individual table
or list blocks, plain-text conversion and final layout are atomic steps bounded
by input and model capacity; incremental parsing alone does not establish the
five-millisecond dispatch gate. Reader is a local document application; rich
formats, image decoding and full desktop Reader parity remain outside this
implemented subset.

## Find in document

**Find** opens a literal query field. **Next match** and **Previous match** wrap
through matches in the page's rendered text, including text split across inline
styles. ASCII letters match without case sensitivity; other UTF-8 bytes match
exactly. Matching text is highlighted, and the current ordinal and total count
are displayed. Command+F opens Find; Command+G repeats forward and
Command+Shift+G repeats backward. Page Up and Page Down scroll the document.

Search advances through the shared Stomme prefix matcher in 64-comparison turns.
Revealing a match examines at most 32 layout pieces per turn. Cancel stops search
without replacing the document. Opening another document waits for search to
finish or be cancelled. A published replacement clears match geometry while
retaining the query for another search. Frames retain their page and copied match
range, so later search or replacement does not change an unfinished frame.

## Document tools

**Tools** opens Help, the heading list and Copy selected match. Headings use the
shared document model's H1 through H6 levels, retain their hierarchy indentation
and show six entries per page. Previous and Next visit heading pages. Choosing
a heading scrolls to it; Back restores the prior reading position. Back from the
heading list returns to Tools, and Document returns to the reading view.

Find selects a passage for **Copy selected match** or Command+C. Copy preserves
the page's original text and letter case in the shared desktop clipboard. With
no selected match it retains the clipboard. New documents clear the selection.
Heading discovery examines at most 32 document blocks per owner turn before
publication. Empty heading lists have an explicit state. Folded heading groups,
bookmarks and folder search remain separate native Reader workflows.
