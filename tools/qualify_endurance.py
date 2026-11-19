#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Two-hour LCD idle and periodic application-switch qualification through protected flashing."""
import importlib.util
import json
from pathlib import Path

_spec = importlib.util.spec_from_file_location('surface_runtime_qualification',
                                              Path(__file__).with_name('qualify_runtime.py'))
_runtime = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_runtime)


class ApplicationQualification(_runtime.ApplicationQualification):
    """Require 100 lifecycle cycles and two hours of active LCD runtime observation."""

    project = b'tikusurface_s31_lcd'
    required_runs = 1

    def qualify(self, command, result):
        """Alternate application switches and Home dwell; qualify no transfer or physical input."""
        return _runtime.qualify(command, result, cycles=100, progress=self.report_progress,
                                lcd=True, active_seconds=7200)

    def report_progress(self, result):
        """Emit each completed cycle's elapsed observation and measured limits."""
        print(json.dumps({key: result[key] for key in
                          ('run', 'cycles', 'active_seconds', 'soak_samples',
                           'max_dispatch_us', 'minimum_stack_free')}), flush=True)
