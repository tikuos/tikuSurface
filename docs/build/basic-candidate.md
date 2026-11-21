# Local BASIC workflow

`SURFACE_BASIC=1` requires the Terminal candidate and its prerequisite options.
Run BASIC in Editor consumes a snapshot of the current draft, opens Terminal
and starts a TikuOS interpreter job. Saving is independent of running. Changes
made after Run do not alter the running program. Terminal reports running,
finished, error, stopped or refused admission. Interpreter diagnostics appear
in its output. Stop and closing Terminal stop its owned job. Switching tasks
retains execution and the Editor draft.

The source limit is the Editor's 8192 bytes. Source must contain no NUL and
numbered BASIC lines; the interpreter applies its configured line and program
limits. Its default line buffer is 144 bytes. Jobs refuse INPUT, REBOOT and
POKE. Run replaces the interpreter's in-memory program. It does not save the
Editor document or overwrite its storage file. An existing BASIC session or
job refuses admission. Pending Terminal commands also refuse a new Run.

The caller-owned interpreter result preserves completion across later jobs.
Stopping an old result leaves a later owner's job running. Result and output
stream storage remain valid until completion or synchronous owned stop. The
product enables `TIKU_BASIC_JOB_OWNER_ENABLE=1`; the core option defaults off.
The console preserves its previous backend capabilities.

Execution uses the existing shell's nonblocking BASIC mode batches. Loading the
bounded source occurs during one owner turn. Host tests do not establish its
physical load latency or the latency of individual BASIC builtins. Advanced
TikuBasic debugging and project operations remain separate port work.
