#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Validate final product code, constants, resident anchors and region admission."""
import argparse
from fnmatch import fnmatchcase
import json
import re
from pathlib import Path
import subprocess

IROM = (0x40020020, 0x40120000)
SRAM = (0x2f000000, 0x2f060000)
I2C_FLASH_SECTIONS = ('.text.budget*', '.text.sync_done', '.text.run_batch',
                      '.text.tiku_i2c_arch_budget*', '.text.tiku_i2c_arch_write_read',
                      '.text.tiku_i2c_arch_close')
SD_FLASH_SECTIONS = ('.text.command', '.text.quiesce', '.text.deselect', '.text.byte', '.text.block', '.text.tiku_s31_sd*', '.text.setup', '.text.elapsed', '.text.exchange',
                     '.text.command_step', '.text.block_step', '.text.crc16_extend', '.text.retire')
STORAGE_FLASH_SECTIONS = {
    'tiku_storage_async_io.o': ('.text.flash*', '.text.command*', '.text.tiku_s31_storage_slice'),
    'tiku_sdcard_arch.o': SD_FLASH_SECTIONS,
    'tiku_spi_arch.o': ('.text.tiku*', '.text.initialize', '.text.clock_set', '.text.run', '.text.context'),
    'tiku_mfs_block_arch.o': ('.text.transfer', '.text.read_blocks', '.text.write_blocks', '.text.sync_blocks',
                             '.text.tiku_s31_mfs_block_acquire', '.text.tiku_s31_mfs_block_release',
                             '.text.tiku_s31_mfs_async*'),
}
STORAGE_RESIDENT_SYMBOLS = ('tiku_s31_mfs_block_init', 'tiku_s31_sdcard_identity', 'tiku_s31_storage_io_busy')
NVM_FLASH_SECTIONS = {
    'tiku_storage_arch.o': ('.text.write_*', '.text.tiku_s31_storage_write*'),
    'tiku_libc_heap.o': ('.text.__assert_func',),
}
MEDIA_FLASH_SECTIONS = {
    'tiku_mfs.o': ('.text.tiku_mfs_query*', '.text.query_*', '.text.catalog_decode*', '.text.node_decode*',
                   '.text.frame_header_decode*', '.text.frame_footer_decode*'),
    'tiku_media_card.o': ('.text.tiku_media_card_read_*', '.text.read_acquire', '.text.read_sector',
                         '.text.read_poll', '.text.read_close'),
}
TIER_FLASH_SECTIONS = ('.text.work*', '.text.record*', '.text.create_check',
                       '.text.arena_create', '.text.tiku_mem_arena_create',
                       '.text.tiku_mem_workspace_open', '.text.tiku_tier_arena_create*',
                       '.text.tiku_backing*', '.text.tiku_mem_reservation_next',
                       '.text.tiku_tier_span_space', '.text.tiku_tier_span_stats',
                       '.text.tiku_tier_stats')
RESIDENT = ('tiku_s31_trap', 'tiku_s31_runtime_init', 'tiku_s31_time',
            'tiku_s31_storage_io', 'tiku_s31_storage_context',
            'tiku_thread_kernel_block', 'tiku_thread_in_kernel', 'tiku_s31_diagnostic_entry',
            'tiku_thread_switch', 'tiku_timer_request_poll', 'memcpy', 'memset')


def symbols(prefix, path):
    """Read defined symbol addresses and optional sizes from a final ELF/object."""
    result = {}
    text = subprocess.check_output([prefix + 'nm', '-S', '--defined-only', str(path)], text=True)
    for line in text.splitlines():
        fields = line.split()
        if len(fields) == 4:
            address, size, kind, name = fields
            result[name] = {'address': int(address, 16), 'size': int(size, 16), 'kind': kind}
        elif len(fields) == 3:
            address, kind, name = fields
            result[name] = {'address': int(address, 16), 'size': 0, 'kind': kind}
    return result


def validate(final, inputs):
    """Check surviving UI symbols and resident anchors against their required ranges."""
    errors, checked = [], {}
    for name, symbol in inputs.items():
        if symbol['kind'] not in 'TR' or name not in final or name.startswith('$'):
            continue
        value = final[name]
        low, high = SRAM if name in STORAGE_RESIDENT_SYMBOLS else IROM
        if not low <= value['address'] < value['address'] + max(1, value['size']) <= high:
            errors.append('Product symbol outside selected region: ' + name)
        checked[name] = value
    for name in ('surface_runtime_start', 'surface_runtime_poll', 'tiku_application_start', 'surface_paint'):
        if name not in checked:
            errors.append('Missing product anchor: ' + name)
    for name in RESIDENT + tuple(n for n in ('tiku_s31_settings_io', 'tiku_s31_settings_context') if n in final):
        if name not in final or not SRAM[0] <= final[name]['address'] < SRAM[1]:
            errors.append('Resident symbol outside SRAM: ' + name)
    for name in ('__surface_irom_start', '__surface_irom_end', '__tier_sram_start', '__tier_sram_end'):
        if name not in final:
            errors.append('Missing linker bound: ' + name)
    if errors:
        raise ValueError('\n'.join(errors))
    status = final.get('tiku_media_transfer_server_status')
    if status and not IROM[0] <= status['address'] < status['address'] + status['size'] <= IROM[1]:
        raise ValueError('Transfer snapshot outside IROM')
    observation = final.get('tiku_wireless_observe')
    if observation and not IROM[0] <= observation['address'] < observation['address'] + observation['size'] <= IROM[1]:
        raise ValueError('Radio observation outside IROM')
    ui_bytes = final['__surface_irom_end']['address'] - final['__surface_irom_start']['address']
    tier_bytes = final['__tier_sram_end']['address'] - final['__tier_sram_start']['address']
    if ui_bytes <= 0 or ui_bytes > 196608:
        raise ValueError('UI IROM budget exceeded or empty')
    if tier_bytes < 90112 + 6112 + 4096:
        raise ValueError('SRAM admission below radio/display/residual reservation')
    return {'ui_irom_bytes': ui_bytes, 'sram_tier_bytes': tier_bytes,
            'residual_after_radio_and_display': tier_bytes - 90112 - 6112,
            'ui_symbols': checked, 'resident_symbols': {n: final[n] for n in RESIDENT}}


def contributions(path, objects):
    """Read allocated input sections after the linker's discarded-section listing."""
    body = path.read_text().split('Linker script and memory map', 1)
    if len(body) != 2:
        raise ValueError('Missing allocated map listing')
    found, section = [], None
    for line in body[1].splitlines():
        match = re.match(r'^ (\.[^ ]+)(?:\s|$)', line)
        if match:
            section = match.group(1)
            line = line[match.end():]
        elif not line.startswith('                0x'):
            section = None
        item = re.match(r'\s*(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)\s*$', line)
        if item and section and item[3] in objects:
            address, size = int(item[1], 16), int(item[2], 16)
            if size:
                found.append({'section': section, 'address': address,
                              'size': size, 'object': item[3]})
    if not found:
        raise ValueError('Missing product map contributions')
    return found


def validate_sections(entries, display_descriptors=False):
    """Check product input ranges, including anonymous constants and local symbols."""
    sram, descriptors = 0, 0
    for entry in entries:
        name, address, size = entry['section'], entry['address'], entry['size']
        if name.startswith(('.text', '.rodata', '.srodata', '.data.rel.ro', '.tiku.identity')):
            obj = entry.get('object', '')
            selected = STORAGE_FLASH_SECTIONS.get(obj.rsplit('/', 1)[-1]) if '/arch/esp32s31/' in obj else None
            resident = (name.startswith('.text') and selected is not None and
                        not any(fnmatchcase(name, pattern) for pattern in selected))
            low, high = SRAM if resident else IROM
            if not low <= address < address + size <= high:
                raise ValueError('UI section outside selected region: ' + name)
        elif name.startswith(('.data', '.bss', '.sdata', '.sbss')):
            if not SRAM[0] <= address < address + size <= SRAM[1]:
                raise ValueError('UI writable section outside SRAM: ' + name)
            if (display_descriptors and name == '.bss.descriptors' and
                    entry.get('object', '').endswith('/arch/esp32s31/tiku_display_arch.o')):
                if size != 6112 or descriptors:
                    raise ValueError('Display descriptor reservation changed')
                descriptors = size
            else:
                sram += size
        elif not name.startswith('.debug') and name != '.riscv.attributes':
            raise ValueError('Unclassified product section: ' + name)
    if sram > 2048:
        raise ValueError('UI static SRAM budget exceeded')
    if display_descriptors and descriptors != 6112:
        raise ValueError('Missing display descriptor reservation')
    return sram


def validate_tier_sections(entries):
    """Require selected allocation/query code in flash and other tier code in SRAM."""
    moved = []
    for entry in entries:
        if not entry['section'].startswith('.text'):
            continue
        selected = any(fnmatchcase(entry['section'], pattern) for pattern in TIER_FLASH_SECTIONS)
        low, high = IROM if selected else SRAM
        if not low <= entry['address'] < entry['address'] + entry['size'] <= high:
            raise ValueError('Tier helper outside selected region: ' + entry['section'])
        if selected:
            moved.append(entry)
    required = {'.text.tiku_tier_arena_create_span', '.text.arena_create', '.text.work_fit'}
    if not required <= {e['section'] for e in moved}:
        raise ValueError('Missing tier allocation helper anchors')
    return moved


def validate_i2c_sections(entries):
    """Check the foreground I2C selections and the remaining SRAM functions."""
    moved = []
    for entry in entries:
        if not entry['section'].startswith('.text') or not entry['size']:
            continue
        selected = any(fnmatchcase(entry['section'], pattern) for pattern in I2C_FLASH_SECTIONS)
        low, high = IROM if selected else SRAM
        if not low <= entry['address'] < entry['address'] + entry['size'] <= high:
            raise ValueError('I2C helper outside selected region: ' + entry['section'])
        if selected:
            moved.append(entry)
    if not {'.text.run_batch', '.text.budget_expired', '.text.tiku_i2c_arch_budget_begin'} <= {e['section'] for e in moved}:
        raise ValueError('Missing I2C deadline helper anchors')
    return moved


def validate_media_sections(entries):
    """Require media code/constants in IROM and mutable state in SRAM."""
    moved = []
    for entry in entries:
        section = entry['section']
        if not entry['size'] or section.startswith('.debug') or section == '.riscv.attributes':
            continue
        patterns = MEDIA_FLASH_SECTIONS[entry['object'].rsplit('/', 1)[-1]]
        selected = any(fnmatchcase(section, p) for p in patterns)
        selected |= section.startswith(('.text', '.rodata', '.srodata', '.data.rel.ro'))
        low, high = IROM if selected else SRAM
        if not low <= entry['address'] < entry['address'] + entry['size'] <= high:
            raise ValueError('Media input outside selected region: ' + section)
        if selected:
            moved.append(entry)
    names = {e['section'] for e in moved}
    if not {'.text.tiku_mfs_query_step', '.text.tiku_media_card_read_step'} <= names:
        raise ValueError('Missing resumable media anchors')
    return moved


def validate_nvm_sections(entries):
    """Keep polled storage orchestration in IROM and synchronous core paths/state in SRAM."""
    moved = []
    for entry in entries:
        section = entry['section']
        if not entry['size'] or section.startswith('.debug') or section == '.riscv.attributes':
            continue
        patterns = NVM_FLASH_SECTIONS[entry['object'].rsplit('/', 1)[-1]]
        selected = any(fnmatchcase(section, p) for p in patterns)
        selected |= section.startswith(('.rodata', '.srodata', '.data.rel.ro'))
        low, high = IROM if selected else SRAM
        if not low <= entry['address'] < entry['address'] + entry['size'] <= high:
            raise ValueError('NVM input outside selected region: ' + section)
        if selected:
            moved.append(entry)
    if not {'.text.tiku_s31_storage_write_begin', '.text.tiku_s31_storage_write_poll'} <= {e['section'] for e in moved}:
        raise ValueError('Missing polled NVM anchors')
    return moved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--identity', required=True)
    parser.add_argument('--map', type=Path, required=True)
    parser.add_argument('--unit', type=Path, required=True)
    parser.add_argument('--binding', type=Path, nargs='+', required=True)
    parser.add_argument('--prefix', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--display-descriptors', action='store_true')
    parser.add_argument('--tier-unit', type=Path)
    parser.add_argument('--i2c-unit', type=Path)
    parser.add_argument('--media-unit', type=Path, nargs='+')
    parser.add_argument('--nvm-unit', type=Path, nargs='+')
    args = parser.parse_args()
    inputs = symbols(args.prefix, args.unit)
    for binding in args.binding:
        inputs.update(symbols(args.prefix, binding))
    final = symbols(args.prefix, args.elf)
    result = validate(final, inputs)
    identities = [name for name in final if name.startswith('tiku_portable_sdk_')]
    if identities != ['tiku_portable_sdk_' + args.identity]:
        raise ValueError('Final SDK identity does not match the selected export')
    result['sdk_identity'] = args.identity
    drom = re.search(r'^\.flash.rodata\s+0x[0-9a-f]+\s+(0x[0-9a-f]+)', args.map.read_text(), re.M)
    if not drom or int(drom[1], 16) > 121808 + 6144:
        raise ValueError('DROM admission exceeds the baseline plus product allowance')
    result['drom_bytes'] = int(drom[1], 16)
    entries = contributions(args.map, {str(args.unit), *(str(p) for p in args.binding)})
    result['ui_static_sram_bytes'] = validate_sections(entries, args.display_descriptors)
    result['display_descriptor_bytes'] = 6112 if args.display_descriptors else 0
    result['input_sections'] = entries
    if args.tier_unit:
        result['tier_flash_sections'] = validate_tier_sections(contributions(args.map, {str(args.tier_unit)}))
    if args.nvm_unit:
        result['nvm_flash_sections'] = validate_nvm_sections(contributions(args.map, {str(p) for p in args.nvm_unit}))
    if args.media_unit:
        result['media_flash_sections'] = validate_media_sections(contributions(args.map, {str(p) for p in args.media_unit}))
    if args.i2c_unit:
        result['i2c_flash_sections'] = validate_i2c_sections(contributions(args.map, {str(args.i2c_unit)}))
        if 'strrchr' not in final or not IROM[0] <= final['strrchr']['address'] < IROM[1]:
            raise ValueError('Foreground string helper outside flash')
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('Product placement passed: IROM', result['ui_irom_bytes'], 'SRAM tier', result['sram_tier_bytes'])


if __name__ == '__main__':
    main()
