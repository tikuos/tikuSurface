# Runtime ownership

`surface_create()` allocates the shell, an eight-entry input queue and one
800 by 480 XRGB8888 framebuffer through the supplied allocator. Provider
contexts and callback tables outlive the shell and its outstanding work.
Construction failure releases the allocations already acquired.

Call shell operations and provider completions from one serialized owner.
`surface_step()` consumes at most eight shell events and four application
events, polls retirement and advances at most one new storage request.
These operation limits do not establish target execution time. The clock
supplies monotonic microseconds; a failed or decreasing reading sets a visible
status and leaves navigation available.

## Application and input lifetime

Applications use Stomme's static lifecycle owner and a four-entry input queue.
The product retains one instance of each of its three applications, with one
foreground slot, one retirement slot and one pending application ID. Home and
application switching background the current owner through Stomme's visibility
API. Background applications retain their state and accepted requests; their
input queues are cleared. Browser and settings completion polling continues.

Applications opens the task menu, which lists available applications and marks
open instances. Selecting an open application restores it without allocating
another instance. A navigation request invalidates queued shell/app input and
the pressed control. Repeated requests replace the pending ID. A closed browser
cannot reopen until its old request retires; other applications remain reachable.
Home cancels a pending launch. Invalid application IDs are refused. Failed starts
leave existing background applications intact and report failure on Home.

Close stops the foreground owner and retains its buffers until completion.
Unsaved settings open a modal Keep editing / Discard decision; Escape keeps
editing. Launch requests and underlying controls are refused during this decision.
Pending settings saves cannot be closed. Acknowledgement is polled even while
settings are in the background. Home and switching preserve an unsaved draft.
Opening the application menu cancels an armed control and consumes modal input.
The menu accepts keys 1 through 3 and Escape; it does not forward those keys to
the foreground application. Closing an application removes it from the task list.

Pointer actions activate on release inside the armed control. Dragging out,
focus loss and queue overflow cancel the pressed control. Queue overflow
also cancels queued application input. Escape performs Back: browser file
previews return to their directory, directories return to their parent, and
the root/application views return to the launcher.

The word calculator accepts unsigned 8-, 16-, 32- and 64-bit values in hexadecimal,
decimal, octal or binary. Width cycles from 64 to 8, 16, 32 and back to 64;
narrowing that cannot represent the value is refused. Input radix cycles from
hexadecimal to decimal, octal and binary while preserving the word. Swap bytes
uses the selected width. The four result fields show the value in each radix.
Invalid digits, overflow and overlong entries leave the input unchanged. Entry
length is bounded by the digit capacity of the selected width and radix.
Backspace erases a digit; X and the Clear button erase the word. The view's
`hex` field carries the input in its selected radix. The device-information view
reports the configured platform capabilities and edits touch orientation.
Save queues the calibration record through the configured settings provider.
The active mapping changes after successful completion; failed saves retain
the preceding mapping. Firmware progress and shutdown drain the accepted
request before releasing its buffers.

## Storage

The browser allocates a 16-entry page and a 1024-byte read buffer. It displays
six rows at a time, then requests another page after the last listed ID.
Refresh restarts enumeration at epoch zero. Directory and read operations
use the identities and epochs supplied by the storage provider. File previews
use hexadecimal for the first 128 bytes when the read contains non-ASCII or
binary control bytes. ASCII previews preserve newlines and replace tabs and
carriage returns with spaces. Up to 900 preview characters fit the current
ten-line view; the read buffer retains up to 1024 bytes.

Each accepted request holds an application lease. Cancellation hides results
immediately, and the provider retains the request and its buffers until matching
completion. A stale token cannot retire another request. Completion after
stop releases the lease and permits destruction without updating a new app.
Back or Refresh during a request cancels it and queues the replacement until
the old request retires. Missing/stale media and provider errors have visible
states with Refresh and Home available.

The host provider exposes a fixed catalog and checks the copied caller's
principal, authentication and read/content scopes. It performs no filesystem
enumeration. The LCD firmware adapter queues native-media requests and checks
visible-name epochs. Content-only syncs preserve enumeration cursors; namespace
changes return STALE_CURSOR. Its host suite interleaves writes against private
media. Physical SD removal, transfer timing and designated-card integrity
require board qualification.

## Presentation and shutdown

Rendering freezes a copy of visible app state for one frame. `paint_rows` in
the shell configuration selects 1..480 rows per `surface_render()` call; zero
selects a full-frame call and values above 480 refuse construction. Partial
painting returns BUSY and retains drawing ownership. Changes during painting
schedule another frame without changing the frozen view. Text and controls
outside the selected rows are skipped. The completed frame goes to the presenter.
These work limits require target timing measurements.

The presenter
retains pixels, dimensions, stride, damage and token until completion. Pixels
remain immutable while retained, and rendering reports BUSY. Pending repaint
state survives that interval. No pointer into application state is retained
by the presenter, so an old rendered frame can outlive its application.

Rejected submissions abandon drawing ownership for retry. Failed asynchronous
completion marks the display failed and schedules repaint. The host renderer
paints full frames; damage accumulation and bounded hardware publication are
separate adapter requirements.

`surface_stop()` abandons partial painting, closes input, cancels pending
navigation and stops foreground and background applications. This runtime teardown
discards unsaved in-memory drafts; the interactive Close path handles decisions.
Input and launch requests are refused afterward. Continue provider completions
and owner turns until retirement ends. `surface_destroy()` refuses a live app
lease or in-flight frame and leaves its pointer intact. It releases the shell
after providers have returned ownership. A provider that never completes
keeps its memory retained; shutdown does not free its buffers prematurely.
