# File-action candidate

`SURFACE_FILE_JOBS=1` enables file rename/move and deletion in the media browser.
It requires `SURFACE_TEXT_INPUT=1` and an exported Stomme SDK advertising
`text-field-events` and `paged-storage-mutation`. The default build keeps the
published foundation pin. A candidate SDK is selected explicitly with `SDK_ROOT`
for host builds or `SURFACE_SDK` for firmware builds.

Open a file to preview it. Rename / move opens the touch text dialog with its
absolute path. Enter a destination of at most 64 bytes including the leading
slash, then Apply. Existing destinations and file/directory-prefix conflicts
are refused. Delete opens a confirmation; Keep file dismisses it. Successful
operations refresh the containing folder.

The provider resolves the selected catalog entry and checks its epoch again
when the mutation acquires queue ownership. Changed selections require refresh.
The native filesystem preserves object identity and existing reader snapshots
through rename; deletion removes the name while retained readers keep their data.
[File-content jobs](content-jobs-candidate.md) add file creation, bounded copy and checked
replacement. Empty directories and recursive operations are separate capabilities.

Jobs retain application and transport buffers until completion. Cancel requests
retirement. Interrupted allocation or writes can leave the mounted card held
until remount; an already published transaction remains committed. The card
binding does not format or select a replacement medium. The shared provider
requires the local principal, authenticated WRITE scope and bounded paths.

The host fixture provider remains read-only. The private media integration
suite exercises the file controls through the shared SDK and queued TikuOS
filesystem; its disk and card transport are simulated. Target SDK/link checks
compile the S31 path without opening a board.
