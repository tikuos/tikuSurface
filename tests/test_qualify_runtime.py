#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Qualification decisions against simulated console replies; no serial access."""
import importlib.util
from pathlib import Path
import unittest
import sys
import tempfile
import types
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('qualify_runtime', Path(__file__).resolve().parents[1] / 'tools/qualify_runtime.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class Device:
    """Simulate command replies and selected defects independently of firmware code."""

    def __init__(self, defect=None):
        self.defect, self.fail, self.running, self.frames, self.app, self.ticks = defect, 0, 0, 0, 0, 0
        self.failures, self.stops = 0, 0
        self.now = 0.0

    def pause(self, seconds):
        self.now += seconds

    def command(self, text):
        self.now += 0.001
        if text.startswith('write '):
            action = text.split()[-1]
            if action.startswith('fail='):
                self.fail = int(action[5:])
            elif action == 'stop':
                self.running = 0
                self.stops += 1
            elif action == 'start':
                self.failures = int(bool(self.fail))
                self.frames, self.app = 0, 0
                if self.fail:
                    return "write: cannot write '/surface/control' (EIO)"
                self.running = 1
            elif action.startswith('launch='):
                self.app = int(action[7:])
            return text
        if text == 'cat /surface/status':
            self.frames += self.running
            values = dict(running=self.running, stopping=0, turns=self.frames, frames=self.frames,
                          allocations=5 * self.running, live_bytes=1537000 * self.running,
                          peak_bytes=1550000, last_us=100, max_us=6000 if self.defect == 'slow' else 100,
                          overruns=int(self.defect == 'slow'), failures=self.failures,
                          app=self.app, media_unavailable=int(self.app == 2 and self.defect != 'media'))
            if self.defect == 'retained' and self.fail:
                values['live_bytes'] = 32
            return ' '.join(f'{k}={v}' for k, v in values.items()) + '\n'
        if text == 'cat /surface/psram_free':
            return str(16000000 - (32 if self.defect == 'leak' and self.stops > 1 else 0)) + '\n'
        if text == 'cat /sys/clock/ticks':
            self.ticks += 0 if self.defect == 'timer' else 1
            return str(self.ticks) + '\n'
        if text == 'cat /sys/mem/stack_free':
            return '1024\n' if self.defect == 'stack' else '12000\n'
        if text.startswith('echo '):
            return text if self.defect == 'echo' else text[5:] + '\n'
        raise AssertionError(text)


class LCDDevice(Device):
    """Independently expose completed handoffs and LCD error/stall fixtures."""

    def command(self, text):
        if text == 'cat /surface/display':
            self.now += 0.001
            completed = 0 if self.defect == 'display-stall' else self.frames
            return f'enabled={int(self.defect != "display-disabled")} completed={completed} errors={int(self.defect == "display-error")}\n'
        return super().command(text)


class QualificationTests(unittest.TestCase):
    """Distinguish lifecycle evidence, timing/stack gates and incomplete cycle counts."""

    def run_device(self, defect=None, cycles=2):
        device, result = Device(defect), {}
        passed = runner.qualify(device.command, result, cycles, device.pause, lambda: device.now)
        self.assertFalse(device.running)
        self.assertEqual(device.fail, 0)
        return passed, result

    def test_lcd_lifecycle_scope_and_refusals(self):
        device, result = LCDDevice('media'), {}
        self.assertTrue(runner.qualify(device.command, result, 100, device.pause, lambda: device.now, lcd=True))
        self.assertTrue(result['qualification_complete'])
        self.assertEqual(result['scope'], 'lcd-lifecycle')
        self.assertFalse(result['touch_qualified'] or result['physical_latency_qualified'])
        self.assertTrue(any(r['command'].endswith('fail=7') for r in result['transcript']))
        for defect in ('display-stall', 'display-disabled', 'display-error'):
            device = LCDDevice(defect)
            with self.assertRaises((ValueError, TimeoutError)):
                runner.qualify(device.command, {}, 1, device.pause, lambda: device.now, lcd=True)
            self.assertFalse(device.running)

    def test_sustained_runtime_and_refusals(self):
        device, result = LCDDevice(), {}
        self.assertTrue(runner.qualify(device.command, result, 100, device.pause,
                                      lambda: device.now, lcd=True, active_seconds=7200))
        self.assertTrue(result['qualification_complete'])
        self.assertGreaterEqual(result['active_seconds'], 7200)
        self.assertEqual(result['scope'], 'lcd-sustained-lifecycle')
        self.assertFalse(result['physical_latency_qualified'])
        self.assertGreaterEqual(result['soak_samples'], 7200)
        device = LCDDevice('slow')
        with self.assertRaisesRegex(ValueError, 'dispatch limit'):
            runner.qualify(device.command, {}, 1, device.pause,
                           lambda: device.now, lcd=True, active_seconds=5)
        for defect in ('reset', 'allocation-growth', 'display-error', 'timer-stall'):
            device, result = LCDDevice(), {}
            original = device.command
            def command(text):
                if device.now > 2 and device.running:
                    if defect == 'reset':
                        device.running = 0
                    if defect == 'display-error':
                        device.defect = 'display-error'
                    if defect == 'timer-stall':
                        device.defect = 'timer'
                reply = original(text)
                if defect == 'allocation-growth' and device.now > 2 and device.running:
                    reply = reply.replace('live_bytes=1537000', 'live_bytes=1537032')
                return reply
            with self.subTest(defect=defect), self.assertRaises((ValueError, TimeoutError)):
                runner.qualify(command, result, 1, device.pause,
                               lambda: device.now, lcd=True, active_seconds=5)
            self.assertFalse(device.running)
            self.assertFalse(result['qualification_complete'])
        device, result = LCDDevice(), {}
        self.assertTrue(runner.qualify(device.command, result, 100, device.pause,
                                      lambda: device.now, lcd=True, active_seconds=1))
        self.assertFalse(result['qualification_complete'])
        self.assertLess(result['active_seconds'], 7200)

    def test_cycle_count(self):
        passed, result = self.run_device()
        self.assertTrue(passed)
        self.assertFalse(result['qualification_complete'])
        passed, result = self.run_device(cycles=100)
        self.assertTrue(passed and result['qualification_complete'])
        self.assertEqual(result['cycles'], 100)

    def test_timing_and_stack_fail(self):
        for defect in ('slow', 'stack'):
            passed, result = self.run_device(defect)
            self.assertFalse(passed)
            self.assertTrue(result['lifecycle_passed'])

    def test_progress_reports_completed_cycles(self):
        device, result, cycles = Device(), {}, []
        self.assertTrue(runner.qualify(device.command, result, 2, device.pause,
                                      lambda: device.now, lambda r: cycles.append(r['cycles'])))
        self.assertEqual(cycles, [1, 2])

    def test_runtime_defects_fail(self):
        for defect in ('media', 'timer', 'retained', 'leak', 'echo'):
            with self.subTest(defect=defect), self.assertRaises(ValueError):
                self.run_device(defect)

    def test_malformed_replies(self):
        reply = Device().command('cat /surface/status')
        self.assertEqual(runner.status(reply)['running'], 0)
        for bad in ('', reply + reply, reply.replace('frames=0', 'frames=x'), reply.rstrip() + ' junk',
                    reply.replace('running=0', 'running=0 running=0')):
            with self.assertRaises(ValueError):
                runner.status(bad)
        for bad in ('cat 123', '1\n2', '123x'):
            with self.assertRaises(ValueError):
                runner.scalar(bad)

    def test_cleanup_failure_invalidates_pass(self):
        device, result = Device(), {}
        original = device.command
        def command(text):
            if text == 'write /surface/control fail=0' and result.get('lifecycle_passed'):
                raise OSError('disconnected')
            return original(text)
        self.assertFalse(runner.qualify(command, result, 100, device.pause, lambda: device.now))
        self.assertIn('cleanup_error', result)
        self.assertFalse(result['qualification_complete'])

    def test_cli_configures_port_before_open_and_preserves_output(self):
        device, opened = Device(), []
        class Wire:
            def __init__(self, **kwargs):
                self.port, self.dtr, self.rts, self.data = kwargs['port'], None, None, b''
                assert self.port is None and kwargs['exclusive']
            def open(self):
                assert self.port == '/fake/uart' and self.dtr is False and self.rts is False
                opened.append(self.port)
            def __enter__(self):
                assert opened
                return self
            def __exit__(self, *args):
                pass
            def write(self, data):
                text = data.decode().strip('\r')
                reply = device.command(text) if text else ''
                self.data = (reply + '\r\ntikuOS:/> ').encode()
            def read(self, size):
                data, self.data = self.data, b''
                return data
        with tempfile.TemporaryDirectory() as directory:
            image, output = Path(directory) / 'image.bin', Path(directory) / 'result.json'
            image.write_bytes(b'fixture-image')
            argv = ['qualify_runtime.py', '--port', '/fake/uart', '--board', 'fixture',
                    '--image', str(image), '--output', str(output), '--cycles', '1']
            with patch.dict(sys.modules, serial=types.SimpleNamespace(Serial=Wire)), patch.object(sys, 'argv', argv):
                self.assertEqual(runner.main(), 0)
                with self.assertRaises(FileExistsError):
                    runner.main()
            self.assertEqual(opened, ['/fake/uart'])
            self.assertIn('operator-supplied', output.read_text())

    def test_flash_adapter_boot_runtime_and_fault_capture(self):
        boot = (b'ESP-ROM:esp32s31-test\r\nLoaded app from partition at offset 0x10000\r\n'
                b'TIKU S31 SRAM shell v1\r\nflash_project=tikusurface_s31_diag\r\n'
                b'application_started=0x00000001\r\ntikuOS:/> ')
        class Wire:
            def __init__(self, initial, reset=False):
                self.data, self.device, self.writes, self.reset = initial, Device(), [], reset
                self.timeout = 3.0
            def read(self, count):
                if self.timeout > 0.05:
                    raise TimeoutError('Inherited flasher timeout exceeds console read budget')
                data, self.data = self.data, b''
                return data
            def write(self, data):
                self.writes.append(data)
                reply = self.device.command(data.decode().strip('\r'))
                self.data = (reply + '\r\ntikuOS:/> ').encode()
                if self.reset:
                    self.data = b'ESP-ROM:esp32s31-test\r\n' + self.data
                    self.reset = False
        adapter, wire = runner.ApplicationQualification(), Wire(boot)
        captured, _ = adapter.capture(wire, 4, 0)
        self.assertEqual(wire.timeout, 3.0)
        self.assertTrue(adapter.passed())
        self.assertEqual(adapter.results[0]['cycles'], 100)
        self.assertIn(b'surface-cycle-99-app-0', captured)
        wire = Wire(boot, reset=True)
        adapter.capture(wire, 4, 1)
        self.assertFalse(adapter.passed())
        for invalid in (boot.replace(b'0x00000001', b'0x00000000'),
                        boot.replace(b'tikusurface_s31_diag', b'other'), boot + b'FATAL'):
            wire = Wire(invalid)
            with self.assertRaises(ValueError):
                adapter.capture(wire, 4, 2)
            self.assertEqual(wire.timeout, 3.0)
            self.assertFalse(wire.writes)
            self.assertFalse(adapter.passed())
            self.assertIn('error', adapter.results[-1])
            self.assertEqual(bytes(adapter.console), invalid)

    def test_boot_log_after_prompt_requests_one_fresh_prompt(self):
        boot = (b'ESP-ROM:esp32s31-test\r\nLoaded app from partition at offset 0x10000\r\n'
                b'TIKU S31 SRAM shell v1\r\nflash_project=tikusurface_s31_diag\r\n'
                b'application_started=0x00000001\r\ntikuOS:/> radio initialized\r\n')

        class Wire:
            def __init__(self):
                self.data, self.writes, self.timeout = boot, [], 3.0
                self.pending = 0

            def read(self, count):
                if self.pending:
                    self.pending -= 1
                    if not self.pending:
                        self.data = b'\r\ntikuOS:/> '
                data, self.data = self.data, b''
                return data

            def write(self, data):
                self.writes.append(data)
                self.pending = 2

        adapter, wire = runner.ApplicationQualification(), Wire()
        with patch.object(runner.time, 'monotonic', side_effect=range(0, 200, 10)), \
             patch.object(adapter, 'qualify') as qualify:
            captured, _ = adapter.capture(wire, 45, 0)
        self.assertEqual(wire.writes, [b'\r'])
        self.assertIn(b'radio initialized', captured)
        self.assertEqual(wire.timeout, 3.0)
        qualify.assert_called_once()
        for invalid in (boot.replace(b'tikusurface_s31_diag', b'other'),
                        boot.replace(b'0x00000001', b'0x00000000')):
            wire = Wire()
            wire.data = invalid
            with self.assertRaises(ValueError):
                adapter.capture(wire, 45, 1)
            self.assertFalse(wire.writes)

    def test_split_ansi_prompt(self):
        class Wire:
            def __init__(self):
                self.parts = [b'echo hi\r\nhi\r\n\x1b[', b'32mtikuOS:/> \x1b[0m']
            def write(self, data):
                self.sent = data
            def read(self, size):
                return self.parts.pop(0)
        wire = Wire()
        self.assertEqual(runner.Console(wire).command('echo hi'), 'echo hi\nhi')
        self.assertEqual(wire.sent, b'echo hi\r')


if __name__ == '__main__':
    unittest.main()
