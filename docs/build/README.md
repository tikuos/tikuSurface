# Dependency and SDK checks

Initialize the pinned foundation and install the checkout's commit hook:

```sh
git submodule update --init
sh hygiene/install_hooks.sh
make check
```

`make check` verifies the gitlink, commit, source-content hash and unchanged
foundation checkout, then runs product prose/comment checks and dependency tests.
New files must be staged for tracked-source checks. The hook verifies the
foundation dependency and applies Stomme's commit-message and author rules.

Build and execute the product host suite:

```sh
make -j6 test
```

This exports/checks the pinned host SDK and tests the shell, calculator, fixture
browser, input cancellation, allocation failures, delayed presentation and
1000 application-switch cycles. `make test-asan` instruments product and host
provider sources with AddressSanitizer and UndefinedBehaviorSanitizer; the
exported foundation archives remain uninstrumented. `make test-foundation`
separately runs the foundation models, services, lifetimes, pixel fixtures,
tooling tests and relocated SDK consumer.

Build and run the Linux X11 preview with `make preview`. X11 headers and the
X11 library are required for that executable; the product tests do not need
a native window library. `make host` builds without opening a window.
`build/host/surface-preview --smoke` opens a window, exercises pointer
translation and rendering, and closes it after ten owner turns.

`make capture` writes `home.ppm`, `calculator.ppm`, `browser.ppm` and
`settings.ppm` under `build/host/`. This path opens no native window and
requires no display server. `build/host/surface-preview --capture DIRECTORY`
writes the same deterministic frames into an existing directory.

Host output lives in `build/host/`; sanitizer output lives in `build/asan/`.
The preview's clock and delayed provider completions are fixtures. Its timings
are not device latency measurements. Captures use full-frame painting.

Build the S31 portable SDK and link its consumers:

```sh
make -j6 sdk-s31 CROSS_COMPILE=/path/to/riscv32-esp-elf-
make -j6 sdk-s31-lto CROSS_COMPILE=/path/to/riscv32-esp-elf-
```

The compiler must support RV32 `ilp32f`. Each configuration has a separate
directory under `foundation/build/`: `surface-host`, `surface-s31` and
`surface-s31-lto`. The latter uses `-Os -g -flto`. SDK exports carry their
target, ABI and content identities, and consumer links verify those identities.
These targets build SDK consumers and do not flash or access board ports.

An S31 SDK link does not establish firmware placement or board execution.
Build the diagnostic runtime from the TikuOS root after exporting its SDK:

```sh
make MCU=esp32s31 S31_PROFILE=s31-local \
    CROSS_COMPILE=/path/to/riscv32-esp-elf- PYTHON=/path/to/venv/bin/python
make MCU=esp32s31 S31_PROFILE=s31-connected S31_WIFI_COUNTRY=SG \
    CROSS_COMPILE=/path/to/riscv32-esp-elf- PYTHON=/path/to/venv/bin/python
```

Use the Wi-Fi country code for the deployment location. The connected profile
requires the port's verified Wi-Fi/PHY and SAE assets. The local profile uses
ROM symbol declarations from the driver checkout without linking radio archives.
Both select 80 MHz PSRAM and the `tikuSurface/product/tikuos.mk` manifest through
`S31_APPLICATION_MANIFEST`. Missing product or SDK inputs are configuration errors.
The remote GUI flag `TIKU_APPL_GUI=1` cannot accompany these profiles.

For UI/SDK LTO, export `sdk-s31-lto` and add `S31_UI_LTO=1`. Use a distinct
`S31_DIR=build/surface-local-lto` or `build/surface-connected-lto`. The final
firmware link consumes the native UI unit produced by the LTO partial link;
it does not apply LTO to firmware interrupt or storage objects. SDK identities
and final placement are checked before generating `kernel.bin`.

[Runtime bindings](../../platform/tikuos/README.md) describe the diagnostic
providers. The default builds use diagnostic presentation. Add
`S31_SURFACE_LCD=1 S31_DIR=build/surface-lcd-local` to drive the LCD; use a
separate output directory for each profile/LTO selection. LCD builds select
GT1151 touch and persistent product calibration.
These images require the
installed second-stage flash bootloader. Build commands do not access a board;
boot, dispatch timing, stack and console qualification use the protected
providers described in the runtime bindings.

`make test-tikuos TIKUOS_ROOT=/path/to/tikuOS` links the diagnostic controls with
the production VFS and TikuBench's hardware-free host hooks. For sanitizer
coverage of that integration, run:

```sh
make -f mk/tikuos-test.mk TIKUOS_ROOT=/path/to/tikuOS \
    BUILD=build/tikuos-asan \
    CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie'
```
