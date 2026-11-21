# Tracker document handlers

`SURFACE_DOCUMENTS=1` adds Open and Open with to a selected Tracker file.
It requires `SURFACE_HEX=1` and an exported Stomme SDK with the
`document-handler-relation` capability. The published foundation pin is unchanged.

Open uses the product filename table and shared desktop handler relation:
Editor handles text, Reader handles HTML and Markdown, and TikuHex handles binary
files. Filename suffixes ignore ASCII case. Unknown and unsupported types show
an explicit raw-byte inspection choice. Open with lists installed handlers by
match strength. The existing Edit text, Read document and Edit bytes actions
remain available. Each document application accepts files through 8 KiB.

Command-O opens the default handler; Command-Shift-O opens the choice dialog.
Escape cancels that dialog, and keys 1 through 3 select its visible rows.
The source path, node and catalog epoch accompany the request. A dirty or busy
target keeps its document and Tracker names the application requiring attention.
Applications returns to that retained task. Failed application startup retires
the document handoff. Preferred-handler settings and executable launching are
outside this implementation.
