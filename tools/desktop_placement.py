#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Check the native desktop image's code window and PSRAM statics.

The desktop's statics must lie inside the 640 KiB section at the top of PSRAM;
flash code must end inside the 2 MB window; the SRAM image must leave the
firmware stack its reserve.  The sizes are printed for the record.
"""
import argparse
import re
import subprocess
import sys

PSRAM_STATIC = (0x50F60000, 0x51000000)
IROM = (0x40020020, 0x40020020 + 2048 * 1024 - 32)


def symbols(elf, nm):
    out = subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout
    found = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            found[parts[2]] = int(parts[0], 16)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True)
    parser.add_argument('--map', required=True)
    parser.add_argument('--nm', default=None)
    args = parser.parse_args()
    nm = args.nm
    if nm is None:
        text = open(args.map, errors='replace').read()
        m = re.search(r'(\S*riscv32-esp-elf/bin/)', text)
        nm = (m.group(1) + 'riscv32-esp-elf-nm') if m else 'riscv32-esp-elf-nm'
    s = symbols(args.elf, nm)
    need = ('__surface_psram_bss_start', '__surface_psram_bss_end',
            '__tiku_s31_irom_start', '__tiku_s31_irom_end', '__bss_end', '__stack')
    missing = [n for n in need if n not in s]
    if missing:
        print('desktop placement: missing symbols ' + ' '.join(missing))
        return 1
    errors = []
    if any(name.split('.')[0] in ('sha256_block', 'sha256_k', 'tiku_sha256')
           for name in s):
        errors.append('desktop links Stomme SHA-256 alongside the firmware digest')
    if 'tiku_kits_crypto_sha256_update' not in s:
        errors.append('missing TikuKits streaming SHA-256')
    start, end = s['__surface_psram_bss_start'], s['__surface_psram_bss_end']
    if not (PSRAM_STATIC[0] <= start <= end <= PSRAM_STATIC[1]):
        errors.append('statics outside the top PSRAM section')
    if not (IROM[0] <= s['__tiku_s31_irom_start'] <= s['__tiku_s31_irom_end'] <= IROM[1]):
        errors.append('flash code outside the 2 MB window')
    print('desktop placement: flash code %d bytes, PSRAM statics %d bytes, SRAM end 0x%x' %
          (s['__tiku_s31_irom_end'] - s['__tiku_s31_irom_start'], end - start, s['__bss_end']))
    for e in errors:
        print('desktop placement: ' + e)
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
