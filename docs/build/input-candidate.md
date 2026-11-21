# Text input candidate

`SURFACE_TEXT_INPUT=1` selects the touch field dialog, shell clipboard, bit
editing and checksum input. It requires a separately exported Stomme SDK with
`text-field-events`. The default build uses the pinned foundation and leaves
this selection disabled. Selecting the option against an SDK without that
capability fails before compilation. Foundation pin validation remains enabled.

Build the candidate SDK from its Stomme checkout, then run product tests:

```sh
make -C /path/to/Stomme FOUNDATION_PROFILE=portable-ui BUILD=build/input test check-sdk
make -f mk/host.mk SDK_ROOT=/path/to/Stomme/build/input/sdk \
    BUILD=build/input SURFACE_TEXT_INPUT=1 test
make -f mk/host.mk SDK_ROOT=/path/to/Stomme/build/input/sdk \
    BUILD=build/input-asan SURFACE_TEXT_INPUT=1 \
    CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie' test
```

Build an S31 candidate export with `FOUNDATION_TARGET=s31` and `CROSS_COMPILE`.
Pass its absolute SDK directory to the firmware build as `SURFACE_SDK`, together
with `SURFACE_TEXT_INPUT=1`. UI-LTO requires a matching SDK built with
`CFLAGS='-Os -g -flto'`. These exports are development inputs; publication and
adoption update the foundation gitlink and lock together before release.

The dialog uses the shared field controller for selection, UTF-8 commits and
four undo/redo steps. Its clipboard belongs to the shell and holds at most 255
bytes. It is not synchronized with the host system clipboard. The touch keys
provide printable ASCII; physical input accepts the field's UTF-8 event
contract. The preview translates Ctrl shortcuts, Shift, arrows, Home, End,
Backspace, Delete, Return and Escape. Its X11 text path uses `XLookupString`;
input-method composition is not implemented by that adapter.

Tap the calculator's value to edit it. Apply checks the current base and word
width before publishing; Cancel retains the previous value. The dialog owns
input and rejects application changes until it closes. Queue overflow and
focus loss cancel pressed keys and incomplete text input. The Bits tab toggles
bits in two 32-bit pages; bits outside the selected width are disabled. The
Checksums tab decodes at most 64 bytes from at most 128 hexadecimal input
characters and displays CRC-32/ISO-HDLC and CRC-16/CCITT-FALSE. Spaces consume
input capacity. These operations use Stomme's word model.

Text entry, clipboard state and history occupy the shell's allocator-backed
storage. No runtime heap calls are added outside the configured allocator.
The copied frame view retains the visible text during incremental painting.
The media browser remains read-only: file mutation, document handoff and Editor
integration require their storage and application contracts.
