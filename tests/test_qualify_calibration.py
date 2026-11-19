#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Calibration qualification against independent persistent/reset console fixtures."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('calibration_qualification',
                                             Path(__file__).resolve().parents[1] / 'tools/qualify_calibration.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class Device:
    """Model a saved record, delayed acknowledgement and a separate boot mapping."""

    def __init__(self, defect=None):
        self.defect, self.now, self.saved = defect, 0.0, None
        self.writes, self.stops, self.pending = 0, 0, 0
        self.result = None
        self.boot()

    def boot(self):
        self.running = 1
        self.active = (0, 799, 0, 479, 0, 0, 0) if self.saved is None or self.defect == 'lost-on-reset' else self.saved
        self.state = 0 if self.saved is None or self.defect == 'lost-on-reset' else 1
        self.pending = 0
        self.slow_active = False

    def pause(self, seconds):
        self.now += seconds

    def command(self, text):
        self.now += 0.001
        if text == 'write /surface/control fail=0':
            return ''
        if text == 'write /surface/control stop':
            if self.defect == 'cleanup' and self.result.get('phase_complete'):
                raise OSError('cleanup refused')
            self.running = 0
            self.stops += 1
            return ''
        if text == 'write /surface/control start':
            self.running = 1
            self.active = self.saved
            self.state = 1
            return ''
        if text.startswith('write /surface/calibration '):
            if self.defect == 'write-refused':
                return "write: cannot write '/surface/calibration' (EIO)"
            self.writes += 1
            self.slow_active = True
            self.requested = tuple(map(int, text.split()[-1].split(',')))
            self.pending, self.state = 3, 2
            return ''
        if text == 'cat /surface/calibration':
            if self.pending:
                if self.defect != 'stalled':
                    self.pending -= 1
                if self.defect == 'early-map':
                    self.active = self.requested
                if not self.pending:
                    if self.defect == 'save-failed':
                        self.state = 5
                    else:
                        self.saved = self.active = self.requested
                        self.state = 1
            if self.defect == 'wrong-map' and self.state == 1:
                self.active = (13, 787, 8, 471, 0, 1, 0)
            return ' '.join(f'{k}={v}' for k, v in zip(
                ('state', 'x_min', 'x_max', 'y_min', 'y_max', 'swap', 'invert_x', 'invert_y'),
                (self.state, *self.active))) + '\n'
        if text == 'cat /surface/status':
            maximum = 6001 if self.defect == 'slow' and self.slow_active else 100
            values = dict(running=self.running, stopping=0, turns=5, frames=2,
                          allocations=7*self.running, live_bytes=3072000*self.running,
                          peak_bytes=3200000, last_us=100, max_us=maximum,
                          overruns=int(maximum > 5000), failures=0, app=0, media_unavailable=0)
            if self.defect == 'retained' and not self.running:
                values['live_bytes'] = 32
            return ' '.join(f'{k}={v}' for k, v in values.items()) + '\n'
        if text == 'cat /surface/psram_free':
            return str(15000000 - (32 if self.defect == 'leak' and self.stops > 1 else 0)) + '\n'
        if text == 'cat /sys/mem/stack_free':
            return '1024\n' if self.defect == 'stack' else '12000\n'
        raise AssertionError(text)


class CalibrationTests(unittest.TestCase):
    """Require two distinct boots, matching records, resource bounds and cleanup."""

    def phase(self, provider, device, phase):
        result = {'run': phase}
        provider.results.append(result)
        device.result = result
        passed = provider.qualify(device.command, result, device.pause, lambda: device.now)
        return passed, result

    def test_two_boots(self):
        provider, device = runner.ApplicationQualification(), Device()
        passed, first = self.phase(provider, device, 1)
        self.assertTrue(passed and provider.passed())
        self.assertFalse(first['qualification_complete'] or first['calibration_persistence_qualified'])
        self.assertTrue(first['save_acknowledged'] and first['runtime_restart_verified'])
        device.boot()
        passed, last = self.phase(provider, device, 2)
        self.assertTrue(passed and provider.passed() and last['qualification_complete'])
        self.assertTrue(last['calibration_persistence_qualified'])
        self.assertFalse(last['physical_touch_qualified'] or last['power_loss_qualified'])
        self.assertEqual(device.writes, 1)
        self.assertEqual(device.saved, (12, 787, 8, 471, 0, 1, 0))
        self.assertFalse(device.running)

    def test_persistence_and_resource_defects(self):
        for defect in ('lost-on-reset', 'slow', 'stack'):
            with self.subTest(defect=defect):
                provider, device = runner.ApplicationQualification(), Device(defect)
                self.assertTrue(self.phase(provider, device, 1)[0])
                device.boot()
                if defect == 'lost-on-reset':
                    with self.assertRaises(ValueError):
                        self.phase(provider, device, 2)
                else:
                    self.assertFalse(self.phase(provider, device, 2)[0])
                    self.assertTrue(provider.results[-1]['calibration_persistence_qualified'])
                self.assertFalse(provider.passed())
                self.assertFalse(device.running)

    def test_save_and_cleanup_refusals(self):
        for defect in ('write-refused', 'early-map', 'save-failed', 'wrong-map', 'stalled', 'retained', 'leak'):
            with self.subTest(defect=defect):
                provider, device = runner.ApplicationQualification(), Device(defect)
                with self.assertRaises((ValueError, TimeoutError)):
                    self.phase(provider, device, 1)
                self.assertFalse(provider.passed())
        provider, device = runner.ApplicationQualification(), Device('cleanup')
        self.assertFalse(self.phase(provider, device, 1)[0])
        self.assertFalse(provider.passed())
        self.assertIn('cleanup_error', provider.results[-1])

    def test_boot_order_and_strict_records(self):
        provider, device = runner.ApplicationQualification(), Device()
        with self.assertRaises(ValueError):
            self.phase(provider, device, 2)
        reply = device.command('cat /surface/calibration')
        self.assertEqual(runner.calibration(reply), (0, 0, 799, 0, 479, 0, 0, 0))
        for bad in ('', reply+reply, reply.rstrip()+' extra', reply.replace('state=0', 'state=x'),
                    reply.replace('swap=0', 'swap=-1'), reply.replace('invert_y=0', '')):
            with self.assertRaises(ValueError):
                runner.calibration(bad)


if __name__ == '__main__':
    unittest.main()
