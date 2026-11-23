#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""The S31 boot rates the TikuOS build accepts for the desktop.  The rate
switch itself is TikuBench's test_esp32s31_clock, with the port."""
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(os.environ['TIKUOS_ROOT']).resolve()


class ClockTests(unittest.TestCase):
    """The build refuses a rate the port does not offer and a CPLL rate
    without a flash firmware profile and CPLL-backed PSRAM."""

    def test_build_refusals(self):
        for options, message in [
            (['S31_CPU_MHZ=100'], 'S31_CPU_MHZ must be 40, 80, 160 or 320'),
            (['S31_CPU_MHZ=320'], 'requires a flash firmware profile'),
            (['S31_CPU_MHZ=320', 'S31_PROFILE=firmware'], 'requires CPLL-backed PSRAM'),
        ]:
            with self.subTest(options=options):
                run = subprocess.run(['make', '-n', 'MCU=esp32s31'] + options,
                                     cwd=ROOT, capture_output=True, text=True)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(message, run.stderr)


if __name__ == '__main__':
    unittest.main()
