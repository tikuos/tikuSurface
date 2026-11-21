# Terminal candidate

`SURFACE_TERMINAL=1` adds Terminal to the desktop and Applications menu. It
requires `SURFACE_EDITOR=1`, its prerequisite candidate options and a Stomme
SDK exporting `terminal-grid`. The shared desktop parser supplies an 80-column,
20-row grid with 64 scrollback rows, ANSI colors and VT100 controls.

Command opens the touch keyboard. Apply queues the complete line and newline.
Physical keys enter console input directly; Ctrl-C or Stop requests cancellation.
Older and Newer move through retained output; Copy line copies the cursor line
or the first visible history line. Clear resets the local display and history.
Switching applications retains the console; closing cancels pending injected
input and restores the previous UART output backend after retirement.

The TikuOS adapter shares the local UART shell session. Serial and graphical
input reach that same shell. Output continues to UART and is copied into a
4096-byte queue. A full queue records loss and drops incoming output without
blocking the shell. The application reports truncation. Console acquisition
refuses an existing graphical owner or a non-UART backend. It preserves the
previous backend's flags and VFS capabilities.

Each application turn copies at most 32 input and 16 output bytes. Commands
execute through the existing shell scheduler and service implementations.
These bounds cover GUI queue work; physical command latency and commands that
block the cooperative scheduler remain qualification work. The host adapter
is a deterministic provider fixture and does not execute a host shell.

Terminal uses the shared desktop grid/parser implementation with a bounded
parameter binding and a caller-owned console provider. The original desktop
retains its 80 by 24 grid and 500 history rows. The embedded font and parser
render ASCII; richer terminal Unicode behavior remains open.
