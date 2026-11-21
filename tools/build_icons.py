#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Reproduce frozen icon pixels from the attributed HVIF source assets."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Verify source and raster hashes before generating the C asset header."""
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--grid', action='store_true')
    args = parser.parse_args()
    metadata = json.loads((ROOT / 'assets/icons' / ('provenance-grid.json' if args.grid else 'provenance.json')).read_text())
    width = metadata.get('width', 48)
    guard = 'SURFACE_GRID_ICONS_H' if args.grid else 'SURFACE_ICONS_H'
    symbol = 'surface_grid_icons' if args.grid else 'surface_icons'
    subprocess.run(['python3', str(ROOT / 'tools/check_foundation.py')], check=True)
    output = ['/*', ' * Authors: Ambuj Varshney <ambuj@tiku-os.org>',
              f' * icons{"_grid" if args.grid else ""}.h - frozen {width}-pixel TikuDesktop application artwork.',
              ' * SPDX-License-Identifier: Apache-2.0', ' */', '#ifndef ' + guard,
              '#define ' + guard, '#include <stdint.h>',
              f'static const uint32_t {symbol}[{len(metadata["icons"])}][{width} * {width}] = {{']
    with tempfile.TemporaryDirectory() as directory:
        exe = Path(directory) / 'raster'
        raster = Path(directory) / 'icon.argb'
        subprocess.run(['cc', '-O2', '-ffunction-sections', '-fdata-sections',
                        '-I' + str(ROOT / 'foundation/kits/interface'),
                        '-I' + str(ROOT / 'foundation/kits/translation'),
                        str(ROOT / 'tools/raster_icon.c'),
                        str(ROOT / 'foundation/kits/translation/tiku_hvif.c'),
                        '-Wl,--gc-sections', '-lm', '-o', str(exe)], check=True)
        for icon in metadata['icons']:
            source = ROOT / 'assets/icons' / (icon['name'] + '.hvif')
            if hashlib.sha256(source.read_bytes()).hexdigest() != icon['blob_sha256']:
                raise SystemExit('Icon source hash mismatch: ' + icon['name'])
            subprocess.run([str(exe), str(source), str(raster), str(width)], check=True)
            data = raster.read_bytes()
            if hashlib.sha256(data).hexdigest() != icon['raster_sha256']:
                raise SystemExit('Icon raster hash mismatch: ' + icon['name'])
            pixels = struct.unpack(f'<{width * width}I', data)
            output.append('{')
            for offset in range(0, len(pixels), 8):
                output.append('    ' + ','.join(f'0x{p:08x}u' for p in pixels[offset:offset+8]) + ',')
            output.append('},')
    output.extend(['};', '#endif', ''])
    target = ROOT / 'shell' / ('icons_grid.h' if args.grid else 'icons.h')
    content = '\n'.join(output)
    if args.check:
        if target.read_text() != content:
            raise SystemExit('Frozen icon header differs from source assets')
    else:
        target.write_text(content)


if __name__ == '__main__':
    main()
