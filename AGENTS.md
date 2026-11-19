# AGENTS.md

TikuSurface owns its shell, static application inventory and host/TikuOS
adapters. Shared models, controls and rendering belong in Stomme. Firmware
register access and service operations belong in TikuOS or its driver repository.

The `foundation/` submodule and `foundation.lock.json` select Stomme. Update
the gitlink, revision and content hash together. Runtime consumers use exported
SDK headers and archives. Foundation examples and private headers are not
product dependencies. Keep host libraries out of shared shell/application code.

Run `make check`, `make test` and `make test-asan` for product runtime changes.
Run `make test-foundation`, `make sdk-s31` and `make sdk-s31-lto` for dependency
changes. Pass `CROSS_COMPILE` for the S31 toolchain. For firmware diagnostic
changes, also run `make test-tikuos` with `TIKUOS_ROOT` pointing at the matching
firmware checkout and TikuBench hooks. For flash qualification changes, run
`make test-flash` with the same `TIKUOS_ROOT`; this target simulates board I/O.
For display changes, run `make test-display` with the same `TIKUOS_ROOT`;
this target tests publication and S31 handoff with simulated registers.
Touch changes require `make test-input TIKUOS_ROOT=/path/to/tikuOS`.
S31 storage transport changes require `make test-transport` with that firmware
path; this suite uses simulated SPI registers and SD protocol replies.
Connected-service snapshots require `make test-services` with that firmware
path and a separate address/undefined-sanitizer build of `services-test`.
Settings persistence changes require `make test-settings` with that firmware
path; it combines the LCD runtime, settings adapter and TFS over fault-injected
private NVM. Its restart checks do not qualify physical flash power loss.
S31 NVM progression changes require `make test-nvm-backend` with the firmware
path and a separate address/undefined-sanitizer build. Its flash model checks
controller admission, undo recovery and product settings through TFS.
Polled TFS replacement changes require `make test-storage-write` with the firmware
path, plus an address/undefined-sanitizer build of that target. These checks model
backend completion and interrupted writes; they do not qualify flash timing.
Media changes require `make test-media` with the same firmware path; this suite
uses private in-memory media. Run the MFS host suite with and without
`TIKU_MEDIA_NAMES_ENABLE=1` when changing the namespace contract.
LCD runtime changes also require local/connected target links in normal and
UI-LTO modes with `S31_SURFACE_LCD=1`. Account for resident growth and alignment
separately from the fixed display descriptors; placement checks alone do not
establish the whole-image SRAM gate. The diagnostic runner refuses LCD images. `tools/qualify_lcd.py` checks LCD
lifecycle through the flash/restore runner; its scope excludes physical touch
and latency qualification.
S31 SDK checks compile and link consumers; they do not execute on hardware.
Stage new files before lint.
Regression checks must reject the failing input before their pass is evidence.

`shell/` and `apps/` compile against exported SDK headers and archives, with
no host or firmware includes. `platform/host/` owns X11, fixture storage and
the tracked host allocator. Tests trap direct heap calls outside that provider.
Presentation retains shell-owned pixels; storage retains an application lease.
Keep application lifecycle calls outside its own callbacks. Use separate
output directories for sanitizer and normal builds.

Run `sh hygiene/install_hooks.sh` after initializing the submodule. The hook
uses the pinned foundation's commit checker. Git worktrees share local config;
the installed absolute hook path refers to the checkout that ran the installer.
Follow `foundation/comment-style.md`, `foundation/prose-style.md` and
`foundation/hygiene/commit-style.md`. Keep plans and evidence outside this repo.

Present subject, configured author and body for approval before committing.
Use plain `git commit` without author overrides or assistant trailers. Push
when requested. Preserve unrelated work. Board access and flashing require an
explicit request; host and target-link checks require no board.
