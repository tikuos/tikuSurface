#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Qualify a temporary calibration record over two protected LCD image boots."""
import importlib.util
from pathlib import Path
import re
import time

_spec = importlib.util.spec_from_file_location('surface_runtime_qualification',
                                              Path(__file__).with_name('qualify_runtime.py'))
_runtime = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_runtime)
FIELDS = ('state', 'x_min', 'x_max', 'y_min', 'y_max', 'swap', 'invert_x', 'invert_y')
DEFAULT = (0, 799, 0, 479, 0, 0, 0)
MAPPING = (12, 787, 8, 471, 0, 1, 0)


def calibration(reply):
    """Require one complete calibration observation with ordered decimal fields."""
    rows = [line.strip() for line in reply.splitlines() if line.startswith('state=')]
    pattern = ' '.join(name + r'=([0-9]+)' for name in FIELDS)
    match = re.fullmatch(pattern, rows[0]) if len(rows) == 1 else None
    if not match:
        raise ValueError('Missing or malformed calibration status')
    return tuple(int(value) for value in match.groups())


class ApplicationQualification(_runtime.ApplicationQualification):
    """Save on the first boot and verify persistence after the runner's second reset."""

    project = b'tikusurface_s31_lcd'
    required_runs = 2

    def qualify(self, command, result, pause=time.sleep, clock=time.monotonic):
        """Keep persistence, timing and stack gates distinct; stop before each reset/restoration."""
        phase = result['run']
        if phase not in (1, 2) or len(self.results) != phase:
            raise ValueError('Calibration qualification requires two ordered boots')
        if phase == 2 and not self.results[0].get('phase_complete'):
            raise ValueError('Save phase did not complete')
        result.update(scope='calibration-reset', phase='save' if phase == 1 else 'reload',
                      passed=False, phase_complete=False, qualification_complete=False,
                      calibration_persistence_qualified=False, physical_touch_qualified=False,
                      power_loss_qualified=False, max_dispatch_us=0, minimum_stack_free=16384,
                      timing_limit_us=5000, transcript=[])

        def call(text):
            reply = command(text)
            result['transcript'].append({'command': text, 'reply': reply})
            if 'cannot write' in reply:
                raise ValueError('Calibration control refused: ' + text)
            return reply

        def sample():
            value = _runtime.status(call('cat /surface/status'))
            result['max_dispatch_us'] = max(result['max_dispatch_us'], value['max_us'])
            return value

        def stopped():
            call('write /surface/control stop')
            deadline = clock() + 3
            while True:
                value = sample()
                if not value['running']:
                    if any(value[k] for k in ('stopping', 'allocations', 'live_bytes')):
                        raise ValueError('Stopped calibration runtime retains allocations')
                    return
                if clock() >= deadline:
                    raise TimeoutError('Calibration runtime did not stop')
                pause(0.01)

        def expect(state, mapping):
            value = calibration(call('cat /surface/calibration'))
            if value != (state, *mapping):
                raise ValueError('Unexpected calibration record: ' + str(value))

        try:
            call('write /surface/control fail=0')
            if not sample()['running']:
                raise ValueError('Boot did not start the LCD runtime')
            expect(0 if phase == 1 else 1, DEFAULT if phase == 1 else MAPPING)
            result['loaded_mapping'] = list(DEFAULT if phase == 1 else MAPPING)
            if phase == 1:
                call('write /surface/calibration ' + ','.join(map(str, MAPPING)))
                deadline = clock() + 10
                while True:
                    value = calibration(call('cat /surface/calibration'))
                    sample()
                    if value == (1, *MAPPING):
                        break
                    if value != (2, *DEFAULT):
                        raise ValueError('Calibration save failed or changed active mapping before completion')
                    if clock() >= deadline:
                        raise TimeoutError('Calibration save did not complete')
                    pause(0.01)
                result['save_acknowledged'] = True
            result['minimum_stack_free'] = _runtime.scalar(call('cat /sys/mem/stack_free'))
            stopped()
            result['psram_free'] = _runtime.scalar(call('cat /surface/psram_free'))
            if phase == 1:
                call('write /surface/control start')
                expect(1, MAPPING)
                stopped()
                if _runtime.scalar(call('cat /surface/psram_free')) != result['psram_free']:
                    raise ValueError('Calibration restart leaked PSRAM')
                result['runtime_restart_verified'] = True
            elif result['psram_free'] != self.results[0]['psram_free']:
                raise ValueError('Calibration reboot changed free PSRAM')
            result['phase_complete'] = True
        finally:
            try:
                stopped()
            except Exception as error:
                result['cleanup_error'] = str(error)
                result['phase_complete'] = False
        result['timing_passed'] = result['max_dispatch_us'] <= result['timing_limit_us']
        result['stack_passed'] = result['minimum_stack_free'] >= 4096
        if phase == 2:
            result['calibration_persistence_qualified'] = result['phase_complete']
            result['qualification_complete'] = all(
                r.get('phase_complete') and r.get('timing_passed') and r.get('stack_passed') for r in self.results)
        result['passed'] = result['phase_complete'] if phase == 1 else result['qualification_complete']
        return result['passed']

    def passed(self):
        """Permit the reset after a completed save; require all gates after reload."""
        return bool(self.results and self.results[-1].get('passed') and
                    not self.results[-1].get('error'))
