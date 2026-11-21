# File-content candidate

`SURFACE_CONTENT_JOBS=1` enables New file and Copy to in the media browser,
and the queued file-content provider. It requires `SURFACE_FILE_JOBS=1`, `SURFACE_TEXT_INPUT=1`
and an explicit Stomme SDK exporting `paged-storage-replacement`. The pinned
release SDK remains selected when these options are absent.

New file opens the touch filename dialog from a completed folder listing.
Apply creates an empty file at an unused absolute path and refreshes the folder.
Existing files and file/directory-prefix conflicts are refused. Paths accept
at most 64 bytes including the leading slash.

Open a file preview and choose Copy to to enter an unused destination path.
Copy reads at most 1024 bytes per request into a 65536-byte application buffer,
then publishes the complete destination. Files larger than 65536 bytes are
refused without creating the destination. The status line reports bytes read
and the write phase; Cancel retires the active request. Existing destinations
are preserved. Catalog changes require refresh before retrying. Switching
applications retains the job; Close cancels and retains borrowed buffers until
the provider retires them.

The replacement provider accepts complete immutable input from zero through
65536 bytes. A zero selected ID requires an unused destination. A nonzero ID
requires the selected file and catalog epoch to remain current when the queue
acquires ownership. Publication installs the new file as one filesystem
transaction; existing reader handles retain the previous contents.

Input remains borrowed until request retirement, including cancellation.
Interruption can require remount, and cancellation or an I/O failure does not
prove that publication was rolled back. Refresh the catalog before retrying.
The provider requires the authenticated local principal with WRITE scope.

Host media tests use private simulated storage. Target links compile the card
path without opening a board. Physical media timing and power-loss behavior
require board qualification.
