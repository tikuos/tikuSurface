# Arithmetic Calculator candidate

`SURFACE_ARITHMETIC=1` adds the Calculator application beside the Developer
Calculator, browser and device settings. It requires `SURFACE_TEXT_INPUT=1`
and an explicitly selected SDK advertising `arithmetic-calculator`.
The default build continues to select the published foundation pin.

Build a matching portable SDK from the shared candidate, then run:

```sh
make -f mk/host.mk SDK_ROOT=/path/to/stomme/build/arithmetic-host/sdk \
  SURFACE_TEXT_INPUT=1 SURFACE_FILE_JOBS=1 SURFACE_ARITHMETIC=1 \
  BUILD=build/arithmetic test
```

`SURFACE_FILE_JOBS=1` additionally requires the SDK's paged-storage mutation
capability. An arithmetic build can omit that flag. Firmware profiles accept
`SURFACE_ARITHMETIC=1` and select their SDK through `SURFACE_SDK`.
Host, S31 and S31 UI-LTO SDK archives are separate builds.

Calculator is available on Home and in Applications. Its touch keypad and
physical keys use the shared desktop arithmetic state machine: operations run
left to right, an adjacent operator replaces the pending operator, and percent
means division by 100. It accepts 15 entry characters and formats results with
12 significant digits. Division by zero or magnitude over 1e300 displays Error;
C or Escape clears it. Copy result and the command-C shortcut use the shell
clipboard. Switching retains the calculation; closing resets the next instance.

The shared model uses fixed-buffer binary64 conversion with nearest-even
rounding. It imports no heap allocation or host number-formatting service.
The firmware's existing compiler arithmetic helpers supply floating-point
operations. The UI-LTO intermediate link preserves function/data sections so
unused SDK routines can be discarded by the final link.

Target links and simulated runtime tests establish placement and allocator
admission. They do not establish physical key latency, touch accuracy or panel
readability. Dependency publication and coordinated pin adoption remain release
steps; this option does not change the pinned submodule or its lock file.
