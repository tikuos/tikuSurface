#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Exercise the real S31 radio admission function at mapped-image boundaries."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class RadioImage(unittest.TestCase):
    def test_mapped_and_legacy_entry_bounds(self):
        root = Path(os.environ.get('TIKUOS_ROOT', '../firmware'))
        source = (root / 'drivers/wifi/esp/s31/wifi_s31_phy.c').read_text()
        function = source[source.index('int espw_arch_xip_ok(void)'):]
        for mapped, ble in ((1, 0), (0, 0), (0, 1)):
            with self.subTest(mapped=mapped, ble=ble), tempfile.TemporaryDirectory() as directory:
                path = Path(directory)
                body = '#include <stdint.h>\n#include <assert.h>\nstatic uintptr_t entry_address;\n'
                body += '#define esp_wifi_init_internal entry_address\n' + function
                accepted = [0x40020020, 0x4009ffff]
                rejected = [0, 0x4002001f, 0x40120000]
                if mapped:
                    accepted += [0x400a3d60, 0x400bd46f]
                    rejected += [0x400bd470, 0x400c0000]
                elif ble:
                    accepted += [0x4011ffff]
                else:
                    rejected += [0x400a0000, 0x400a3d60]
                body += '\nint main(void) {\n'
                for address in accepted:
                    body += f'entry_address={address}u; assert(espw_arch_xip_ok());\n'
                for address in rejected:
                    body += f'entry_address={address}u; assert(!espw_arch_xip_ok());\n'
                body += 'return 0; }\n'
                (path / 'test.c').write_text(body)
                command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-no-pie',
                           f'-DTIKU_S31_RADIO_IMAGE_BOUNDS_ENABLE={mapped}', f'-DTIKU_S31_BLE_ENABLE={ble}',
                           '-Wl,--defsym=__tiku_s31_irom_start=0x40020020',
                           '-Wl,--defsym=__tiku_s31_irom_end=0x400bd470',
                           str(path / 'test.c'), '-o', str(path / 'test')]
                subprocess.run(command, check=True, capture_output=True)
                subprocess.run([str(path / 'test')], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
