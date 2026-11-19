#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""LCD lifecycle provider for the firmware flash/restore runner; physical touch is separate."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location('surface_runtime_qualification',
                                              Path(__file__).with_name('qualify_runtime.py'))
_runtime = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_runtime)


class ApplicationQualification(_runtime.ApplicationQualification):
    """Require LCD identity, seven allocation refusals and completed DMA handoffs."""

    project = b'tikusurface_s31_lcd'

    def qualify(self, command, result):
        """Check 100 LCD lifecycle cycles without declaring touch or physical latency qualified."""
        return _runtime.qualify(command, result, cycles=100, progress=self.report_progress, lcd=True)
