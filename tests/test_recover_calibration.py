# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Operator recovery uses explicit writes and requires observed completion."""
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import recover_calibration as runner

FIELDS = ('state', 'x_min', 'x_max', 'y_min', 'y_max', 'swap', 'invert_x', 'invert_y')
DEFAULT = (0, 799, 0, 479, 0, 0, 0)


class Device:
    """Model a delayed save while the active mapping remains unchanged."""
    def __init__(self, fault=None, state=1, active=(12, 787, 8, 471, 1, 1, 0)):
        self.fault, self.state, self.active = fault, state, active
        self.now, self.pending, self.app = 0.0, 0, 3
        self.commands = []

    def pause(self, seconds):
        self.now += seconds

    def command(self, text):
        self.commands.append(text)
        if text == 'cat /surface/status':
            return (f'running={int(self.fault != "stopped")} stopping=0 turns=12 frames=2 '
                    'allocations=7 live_bytes=1537000 peak_bytes=1550000 last_us=100 max_us=100 '
                    f'overruns=0 failures=0 app={self.app} media_unavailable=0')
        if text == 'cat /surface/display':
            return f'enabled={int(self.fault != "diagnostic")} completed=2 errors=0'
        if text == 'cat /surface/calibration':
            if self.pending:
                self.pending -= 1
                if self.fault == 'timeout':
                    self.pending = 1
                elif self.fault == 'early-map':
                    self.active = DEFAULT
                elif not self.pending:
                    self.state = 5 if self.fault == 'save-failure' else 1
                    if self.state == 1 and self.fault != 'wrong-map':
                        self.active = DEFAULT
            reply = ' '.join(f'{k}={v}' for k, v in zip(FIELDS, (self.state, *self.active)))
            return reply + (' trailing' if self.fault == 'malformed' else '')
        if text == 'write /surface/calibration 0,799,0,479,0,0,0':
            if self.fault == 'refused':
                return "write: cannot write '/surface/calibration' (EIO)"
            if self.fault == 'disconnected':
                raise TimeoutError('Console prompt timeout')
            self.state, self.pending = 2, 3
            return text
        if text == 'write /surface/control launch=0':
            if self.fault != 'home-stall':
                self.app = 0
            return text
        raise AssertionError(text)


class RecoveryTests(unittest.TestCase):
    def run_device(self, device, defaults=False):
        result = {}
        runner.recover(device.command, result, defaults, device.pause, lambda: device.now)
        return result

    def test_inspection_does_not_write(self):
        for state in range(7):
            with self.subTest(state=state):
                device = Device(state=state)
                result = self.run_device(device)
                self.assertTrue(result['completed'])
                self.assertFalse(any(c.startswith('write ') for c in device.commands))
                self.assertEqual(result['save_outcome'], 'not-requested')
                self.assertFalse(result['physical_touch_verified'])

    def test_explicit_defaults_wait_for_save_and_home(self):
        device = Device()
        result = self.run_device(device, True)
        self.assertTrue(result['completed'])
        self.assertEqual(result['save_outcome'], 'acknowledged')
        self.assertEqual(result['after'], [1, *DEFAULT])
        self.assertEqual(device.app, 0)
        self.assertEqual(sum(c.startswith('write /surface/calibration ') for c in device.commands), 1)
        self.assertGreaterEqual(device.commands.count('cat /surface/calibration'), 4)
        self.assertFalse(any('stop' in c or 'start' in c or 'format' in c for c in device.commands))

    def test_saved_defaults_do_not_write_the_record_again(self):
        device = Device(active=DEFAULT)
        result = self.run_device(device, True)
        self.assertEqual(result['save_outcome'], 'already-saved-defaults')
        self.assertFalse(any(c.startswith('write /surface/calibration ') for c in device.commands))

    def test_refuse_before_write(self):
        for device in [Device(state=2), Device(state=4), Device(state=6), Device('stopped'),
                       Device('diagnostic'), Device('malformed'), Device(state=7)]:
            with self.subTest(state=device.state, fault=device.fault):
                with self.assertRaises(ValueError):
                    self.run_device(device, True)
                self.assertFalse(any(c.startswith('write ') for c in device.commands))

    def test_failed_or_unknown_save_is_not_reported_as_recovered(self):
        for fault in ('timeout', 'refused', 'disconnected', 'save-failure', 'early-map', 'wrong-map', 'home-stall'):
            with self.subTest(fault=fault):
                device, result = Device(fault), {}
                with self.assertRaises((ValueError, TimeoutError)):
                    runner.recover(device.command, result, True, device.pause, lambda: device.now)
                self.assertFalse(result['completed'])
                if fault != 'home-stall':
                    self.assertNotIn('write /surface/control launch=0', device.commands)
                self.assertLessEqual(sum(c.startswith('write /surface/calibration ') for c in device.commands), 1)
                self.assertLess(device.now, 14)

    def test_mapping_validation(self):
        for values in [(1, 5, 5, 0, 479, 0, 0, 0), (1, 0, 65536, 0, 479, 0, 0, 0),
                       (1, 0, 799, 0, 479, 2, 0, 0)]:
            with self.assertRaises(ValueError):
                runner.mapping(' '.join(f'{k}={v}' for k, v in zip(FIELDS, values)))

    def test_premature_mapping_change_is_rejected_on_observation(self):
        device, result = Device('early-map'), {}
        with self.assertRaisesRegex(ValueError, 'Unexpected mapping'):
            runner.recover(device.command, result, True, device.pause, lambda: device.now)
        self.assertEqual(device.now, 0)
        self.assertFalse(result['completed'])

    def test_cli_exclusive_no_reset_and_report_preservation(self):
        device = Device()
        class Wire:
            def __init__(self, **options):
                self.options, self.closed = options, False
            def open(self):
                self.open_state = (self.port, self.dtr, self.rts)
            def __enter__(self):
                return self
            def __exit__(self, *args):
                self.closed = True
        wire = Wire()
        def serial_factory(**options):
            wire.options = options
            return wire
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'result.json'
            with patch.dict(sys.modules, {'serial': types.SimpleNamespace(Serial=serial_factory)}), \
                 patch.object(runner, 'Console') as console:
                console.return_value.command.side_effect = lambda c: '' if c == '' else device.command(c)
                self.assertEqual(runner.main(['--port', '/dev/fixture', '--output', str(output)]), 0)
                self.assertEqual(wire.open_state, ('/dev/fixture', False, False))
                self.assertEqual(wire.options, dict(port=None, baudrate=115200, timeout=0.05, exclusive=True))
                self.assertTrue(wire.closed)
                self.assertTrue(json.loads(output.read_text())['completed'])
                with self.assertRaises(FileExistsError):
                    runner.main(['--port', '/dev/fixture', '--output', str(output), '--defaults'])
                self.assertFalse(any(c.startswith('write ') for c in device.commands))

    def test_cli_keeps_failure_report_and_closes_port(self):
        device = Device('disconnected')
        wire = unittest.mock.MagicMock()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'result.json'
            with patch.dict(sys.modules, {'serial': types.SimpleNamespace(Serial=lambda **kw: wire)}), \
                 patch.object(runner, 'Console') as console:
                console.return_value.command.side_effect = lambda c: '' if c == '' else device.command(c)
                self.assertEqual(runner.main(['--port', '/dev/fixture', '--output', str(output), '--defaults']), 1)
                result = json.loads(output.read_text())
                self.assertFalse(result['completed'])
                self.assertEqual(result['save_outcome'], 'unknown')
                self.assertIn('Console prompt timeout', result['error'])
                wire.__exit__.assert_called_once()


if __name__ == '__main__':
    unittest.main()
