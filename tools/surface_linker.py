#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Derive product code/constant placement from the firmware linker contract."""
import argparse
from pathlib import Path
from check_placement import TIER_FLASH_SECTIONS, I2C_FLASH_SECTIONS, STORAGE_FLASH_SECTIONS, MEDIA_FLASH_SECTIONS, NVM_FLASH_SECTIONS


def generate(base):
    """Select the native UI unit and owner-context bindings before broad wildcards."""
    exclusion = 'EXCLUDE_FILE (*tiku_bt.o *tiku_shell_cmd_bt.o)'
    marker = '        *(.flash.text)'
    if base.count(exclusion) != 1 or base.count(marker) != 1:
        raise ValueError('Firmware linker anchors changed; review product placement')
    units = ('*surface-ui.o', '*tikuSurface/platform/tikuos/bindings.o',
             '*tikuSurface/platform/tikuos/diagnostics.o', '*tikuSurface/platform/tikuos/display.o',
             '*tikuSurface/platform/tikuos/services.o', '*tikuSurface/platform/tikuos/settings_io.o', '*tikuSurface/platform/tikuos/media_io.o',
             '*services/storage/tiku_media_async.o', '*interfaces/bus/tiku_i2c_bus.o', '*arch/esp32s31/tiku_spi_arch.o',
             '*arch/esp32s31/tiku_sdcard_arch.o', '*arch/esp32s31/tiku_storage_async_io.o', '*arch/esp32s31/tiku_mfs_block_arch.o',
             '*tikuSurface/platform/tikuos/input.o', '*interfaces/input/tiku_touch_gt1151.o',
             '*drivers/input/gt1151/tiku_drv_input_gt1151.o',
             '*interfaces/display/tiku_display.o', '*interfaces/display/tiku_display_async.o',
             '*arch/esp32s31/tiku_display_arch.o', '*arch/esp32s31/tiku_dma_arch.o')
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
    selection += '        *libc.a:libc_a-strrchr.o(.text .text.*)\n'
    selection += '        *kernel/memory/tiku_tier.o(' + ' '.join(TIER_FLASH_SECTIONS) + ')\n'
    selection += '        *services/storage/tiku_media_transfer_server.o(.text.tiku_media_transfer_server_status)\n'
    selection += '        KEEP(*surface-ui.o(.tiku.identity))\n        __surface_irom_end = .;\n'
    bss = '        *(.bss .bss.* .sbss .sbss.* COMMON)'
    if base.count(bss) != 1:
        raise ValueError('Firmware BSS anchor changed; review global-pointer placement')
    excluded = 'EXCLUDE_FILE (' + ' '.join(units) + ')'
    tail = '        *(' + ' '.join(excluded + ' ' + section for section in
                                  ('.bss', '.bss.*', '.sbss', '.sbss.*', 'COMMON')) + ')\n'
    tail += ''.join('        ' + unit + '(.bss .bss.* .sbss .sbss.* COMMON)\n' for unit in units)
    return base.replace(marker, selection + marker).replace(bss, tail.rstrip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = generate(args.base.read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output)


if __name__ == '__main__':
    main()
