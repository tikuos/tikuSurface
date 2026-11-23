# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Check published scheduling limits against the selected runtime."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CapabilityTests(unittest.TestCase):
    def test_paint_rows(self):
        """The declared LCD render budget matches the runtime configuration."""
        manifest = json.loads((ROOT / 'product/capabilities.json').read_text())
        source = (ROOT / 'platform/tikuos/runtime.c').read_text()
        rows = re.findall(r'\.paint_rows\s*=\s*SURFACE_DESKTOP\s*\?\s*(\d+)\s*:\s*(\d+)', source)
        limits = manifest['development_profiles']['core-desktop']['limits']
        self.assertEqual(rows, [(str(limits['paint_rows']), str(manifest['firmware_lcd']['paint_rows']))])
        self.assertEqual(limits['cpu_mhz'], 160)
        self.assertIn('ifeq ($(filter $(S31_CPU_MHZ),160 320),)', (ROOT / 'product/tikuos.mk').read_text())
