# TikuSurface

TikuSurface is the embedded graphical environment for TikuOS. It is a separate
product built on the Stomme UI foundation, with ESP32-S31 as its first target.

The default host build provides an 800 by 480 launcher, application switching, Home/Back navigation, a
hexadecimal word calculator, read-only fixture browsing and device information.
It uses Stomme's portable SDK for rendering, input, application ownership and
asynchronous storage. S31 diagnostic profiles link the runtime with a PSRAM allocator, timer process
and deferred test presentation. LCD builds add paired RGB565 presentation,
GT1151 input, native-media browsing and persistent calibration. Protected
board providers measure lifecycle, reset retention, dispatch and endurance;
physical panel/input and combined-service qualification remain separate.

Home and application switching retain open application state. The Applications
menu restores an open task; Close ends it. Unsaved settings require a Keep
editing or Discard decision. The default browser uses read-only fixture media. The
[expanded desktop candidate](docs/build/full-desktop-candidate.md) adds writable
Tracker, both calculators, Editor, Terminal/BASIC, Device Monitor, VFS Inspector,
Reader, TikuHex, document routing and desktop preferences through an explicitly
exported matching Stomme SDK. The published foundation pin remains the base
profile; the candidate has separate dependency and physical qualification gates.

## Development checkout

Clone this repository into `tikuOS/tikuSurface/`. TikuOS ignores that directory;
TikuSurface keeps its own Git history and remote:

```sh
git clone https://github.com/tikuos/tikuSurface.git tikuSurface
cd tikuSurface
git submodule update --init
sh hygiene/install_hooks.sh
make test
make preview
```

Run TikuSurface Git commands from this directory. Commit shared UI changes in
Stomme and kernel or hardware-service changes in their owning repositories.
The TikuSurface launcher, application selection and platform adapters belong
here. [Build instructions](docs/build/README.md) describe the host and S31 SDK
checks. [Product capabilities](product/capabilities.json) declare the intended
configurations and their implementation status.

The Linux preview uses X11. Click a launcher icon to open an application.
The calculator accepts hexadecimal keys, Backspace to erase and X to clear.
Escape performs Back. The browser shows fixture folders/files with delayed
completion; it does not read the host filesystem. Device information identifies
the available host capabilities and selected build revision. The
[appearance contract](docs/contracts/appearance.md) specifies shared BeOS
components, artwork sources and touch adaptations.
[Kernel use](docs/contracts/kernel-use.md) lists the operating-system
facilities the native desktop takes from TikuOS and those it still carries.
[Runtime ownership](docs/contracts/runtime.md)
describes cancellation and application retirement. The [S31 operating guide](docs/operation/README.md)
covers panel navigation, calibration, observation and recovery. The
[first-use workflow](docs/operation/first-use.md) includes explicit media setup
and UART recovery when the touch mapping prevents navigation.

The repository's license is in [LICENSE](LICENSE).

The [Reader candidate](docs/build/reader-candidate.md) adds local HTML, Markdown,
text and help through shared Stomme parsing and rendering. It requires its
exported SDK and prerequisite application options.
