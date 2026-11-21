# Device Monitor candidate

`SURFACE_DEVICE=1` requires the BASIC candidate and its prerequisite options.
Device Monitor reports uptime, CPU frequency, process SRAM use/free, stack
headroom, process count and SRAM capacity. It samples one endpoint per owner
turn, with a one-second interval between completed cycles. Pause stops automatic
sampling; Refresh requests one cycle even while paused. Missing, invalid and
overflowing readings are unavailable. A 120-sample process-SRAM history records
gaps using Stomme's shared sensor model. Switching retains readings and history.

The local adapter accepts the seven named observations. Typed nodes must satisfy
the kernel's passive-read policy; secret, consuming, effectful and bus-sampled
nodes are refused. The untyped `/proc/count` handler is an audited process
counter; when it has a descriptor, that policy applies. The adapter performs no
writes. Memory-reconstruction controls remain port work.

The desktop and Applications menu include the configured tasks using TikuDesktop artwork.
Tracker allocates its path and preview storage when it starts. Other tasks do
not reserve those buffers. Editor, Terminal and Monitor share storage for their
mutually exclusive presentation views. Allocation refusal retains existing tasks.
Physical telemetry timing, readability and touch qualification remain open.

Processes opens two pages of four registry slots. Selecting a slot shows its
name, scheduler state, declared plus attached-arena memory, uptime, wake count,
queued events and restarts. Empty slots and unavailable observations are distinct.
Back returns from detail to the list, then to the overview. Previous/Next in
detail selects the adjacent slot. Selection survives application switching.

The provider copies one process per owner turn through the public kernel
registry and memory queries. Refresh samples the seven aggregate readings and
eight process slots; Pause stops periodic work and Refresh still requests one
cycle. Overview does not poll individual slots. Queued event counts exclude
broadcasts. Counters can wrap; the list is a sequence of observations rather
than an atomic system-wide snapshot. Other-tier memory includes the process's
reported non-SRAM arena usage. Memory-reconstruction controls are not provided.
