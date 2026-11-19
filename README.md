# TikuSurface

TikuSurface is the embedded graphical environment for TikuOS. It is a separate
product built on the Stomme UI foundation, with ESP32-S31 as its first target.

The host product provides an 800 by 480 launcher, Home/Back navigation, a
hexadecimal word calculator, read-only fixture browsing and device information.
It uses Stomme's portable SDK for rendering, input, application ownership and
asynchronous storage. S31 diagnostic profiles link the runtime with a PSRAM allocator, timer process
and deferred test presentation. LCD builds add paired RGB565 presentation,
GT1151 input, native-media browsing and persistent calibration. Protected
board providers measure lifecycle, reset retention, dispatch and endurance;
physical panel/input and combined-service qualification remain separate.

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

The Linux preview uses X11. Click a launcher tile to open an application.
The calculator accepts hexadecimal keys, Backspace to erase and X to clear.
Escape performs Back. The browser shows fixture folders/files with delayed
completion; it does not read the host filesystem. Device information identifies
the available host capabilities. [Runtime ownership](docs/contracts/runtime.md)
describes cancellation and application retirement. The [S31 operating guide](docs/operation/README.md)
covers panel navigation, calibration, observation and recovery.

The repository's license is in [LICENSE](LICENSE).
