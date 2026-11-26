#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Derive product code/constant placement from the firmware linker contract."""
import argparse
from pathlib import Path
from check_placement import TIER_FLASH_SECTIONS, I2C_FLASH_SECTIONS, STORAGE_FLASH_SECTIONS, MEDIA_FLASH_SECTIONS, NVM_FLASH_SECTIONS


def generate(base, transfer=False):
    """Select the native UI unit and owner-context bindings before broad wildcards."""
    exclusion = 'EXCLUDE_FILE (*tiku_bt.o *tiku_shell_cmd_bt.o)'
    marker = '        *(.flash.text)'
    if base.count(exclusion) != 1 or base.count(marker) != 1:
        raise ValueError('Firmware linker anchors changed; review product placement')
    units = ('*surface-ui.o', '*tikuSurface/platform/tikuos/console_io.o', '*tikuSurface/platform/tikuos/device_io.o', '*tikuSurface/platform/tikuos/process_io.o', '*tikuSurface/platform/tikuos/vfs_io.o', '*tikuSurface/platform/tikuos/bindings.o',
             '*tikuSurface/platform/tikuos/diagnostics.o', '*tikuSurface/platform/tikuos/display.o',
             '*tikuSurface/platform/tikuos/services.o', '*tikuSurface/platform/tikuos/settings_io.o', '*tikuSurface/platform/tikuos/media_io.o',
             '*compat/posix/tiku_posix_media.o', '*compat/posix/tiku_posix_media_io.o',
             '*services/storage/tiku_media_async.o', '*services/storage/tiku_media_mutation.o', '*services/storage/tiku_media_checkpoint.o', '*interfaces/bus/tiku_i2c_bus.o', '*arch/esp32s31/tiku_spi_arch.o',
             '*arch/esp32s31/tiku_sdcard_arch.o', '*arch/esp32s31/tiku_storage_async_io.o', '*arch/esp32s31/tiku_mfs_block_arch.o',
             '*tikuSurface/platform/tikuos/input.o', '*interfaces/input/tiku_touch_gt1151.o',
             '*drivers/input/gt1151/tiku_drv_input_gt1151.o',
             '*interfaces/display/tiku_display.o', '*interfaces/display/tiku_display_async.o',
             '*arch/esp32s31/tiku_display_arch.o', '*arch/esp32s31/tiku_lcdcam_arch.o',
             '*arch/esp32s31/tiku_dma_arch.o',
             '*arch/esp32s31/tiku_ppa_arch.o')
    if transfer:
        units += ('*services/storage/tiku_media_transfer_control.o',
                  '*services/storage/tiku_media_transfer_async.o',
                  '*services/storage/tiku_media_transfer_listener.o',
                  '*services/storage/tiku_media_transfer_tcp.o',
                  '*services/storage/tiku_media_transfer_service.o',
                  '*services/storage/tiku_media_transfer_server.o',
                  '*tikukits/net/ipv4/tiku_kits_net_tcp.o')
    nvm = ('*arch/esp32s31/tiku_storage_arch.o', '*arch/esp32s31/tiku_libc_heap.o')
    media = ('*kernel/fs/tiku_mfs.o', '*services/storage/tiku_media_card.o')
    expanded = 'EXCLUDE_FILE (*tiku_bt.o *tiku_shell_cmd_bt.o ' + ' '.join(units + media + nvm) + ')'
    base = base.replace(exclusion + ' .rodata* .srodata .srodata.*',
                        expanded + ' .rodata* ' + expanded + ' .srodata ' + expanded + ' .srodata.*')
    selection = '        . = ALIGN(4);\n        __surface_irom_start = .;\n'
    for unit in units:
        code = ' '.join(STORAGE_FLASH_SECTIONS.get(unit.rsplit('/', 1)[-1], ('.text', '.text.*')))
        selection += '        ' + unit + '(' + code + ' .rodata* .srodata* .data.rel.ro*)\n'
    for unit in media:
        selection += '        ' + unit + '(' + ' '.join(MEDIA_FLASH_SECTIONS[unit.rsplit('/', 1)[-1]]) + ' .rodata* .srodata* .data.rel.ro*)\n'
    for unit in nvm:
        selection += '        ' + unit + '(' + ' '.join(NVM_FLASH_SECTIONS[unit.rsplit('/', 1)[-1]]) + ' .rodata* .srodata* .data.rel.ro*)\n'
    selection += '        *arch/esp32s31/tiku_i2c_arch.o(' + ' '.join(I2C_FLASH_SECTIONS) + ')\n'
    selection += '        *services/console/tiku_console.o(.text.tiku_console_inject .text.tiku_console_inject_clear)\n'
    selection += '        *libc.a:libc_a-strrchr.o(.text .text.*)\n'
    selection += '        *libgcc.a:_floatdisf.o(.text .text.*)\n'
    selection += '        *kernel/vfs/tiku_vfs.o(.text.tiku_vfs_read_passive .text.tiku_vfs_child_at)\n'
    selection += '        *libc.a:libc_a-strcspn.o(.text .text.*)\n'
    selection += '        *kernel/memory/tiku_tier.o(' + ' '.join(TIER_FLASH_SECTIONS) + ')\n'
    selection += '        *services/storage/tiku_media_transfer_server.o(.text.tiku_media_transfer_server_status)\n'
    if transfer:
        selection += '        *boot/tiku_s31_ram.o(.text.tiku_process_thread_s31_wifi_start)\n'
    selection += '        KEEP(*surface-ui.o(.tiku.identity))\n        __surface_irom_end = .;\n'
    bss = '        *(.bss .bss.* .sbss .sbss.* COMMON)'
    if base.count(bss) != 1:
        raise ValueError('Firmware BSS anchor changed; review global-pointer placement')
    excluded = 'EXCLUDE_FILE (' + ' '.join(units) + ')'
    tail = '        *(' + ' '.join(excluded + ' ' + section for section in
                                  ('.bss', '.bss.*', '.sbss', '.sbss.*', 'COMMON')) + ')\n'
    tail += ''.join('        ' + unit + '(.bss .bss.* .sbss .sbss.* COMMON)\n' for unit in units)
    return base.replace(marker, selection + marker).replace(bss, tail.rstrip())


def native_desktop(script):
    """Give the native desktop a 2 MB code window and its statics in PSRAM.

    The shell's zero-initialized data in surface-ui.o moves to a section at the
    top of PSRAM; the firmware attaches its PSRAM tier below that section.
    Small data stays in SRAM, where the global pointer reaches it.
    """
    window = 'IROM (rx) : ORIGIN = 0x40020020, LENGTH = DEFINED(S31_FIRMWARE) ? 1024K - 32 : 512K - 32'
    if script.count(window) != 1:
        raise ValueError('Firmware code window anchor changed; review the desktop window')
    script = script.replace(window, 'IROM (rx) : ORIGIN = 0x40020020, LENGTH = 2048K - 32')
    memory_end = script.index('}', script.index('MEMORY {'))
    script = (script[:memory_end] + '    PSRAMSTATIC (rw) : ORIGIN = 0x50F60000, LENGTH = 640K\n' +
              script[memory_end:])
    unit_bss = '        *surface-ui.o(.bss .bss.* .sbss .sbss.* COMMON)\n'
    if script.count(unit_bss) != 1:
        raise ValueError('Desktop BSS selection changed; review the desktop statics')
    script = script.replace(unit_bss, '        *surface-ui.o(.sbss .sbss.*)\n')
    anchor = '    .uninit (NOLOAD) : ALIGN(4096) {'
    if script.count(anchor) != 1:
        raise ValueError('Firmware uninit anchor changed; review the desktop statics')
    section = ('    .surface_psram (NOLOAD) : ALIGN(64) {\n'
               '        __tiku_s31_psram_statics_start = .;\n'
               '        __surface_psram_bss_start = .;\n'
               '        *surface-ui.o(.bss .bss.* COMMON)\n'
               '        . = ALIGN(64);\n'
               '        __surface_psram_bss_end = .;\n'
               '    } > PSRAMSTATIC\n')
    return script.replace(anchor, section + anchor)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--transfer', action='store_true')
    parser.add_argument('--native-desktop', action='store_true')
    args = parser.parse_args()
    output = generate(args.base.read_text(), args.transfer)
    if args.native_desktop:
        output = native_desktop(output)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output)


if __name__ == '__main__':
    main()
