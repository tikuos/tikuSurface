#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Check core request identity and latency decisions without board access."""
import importlib.util
from pathlib import Path
import unittest
from test_qualify_runtime import LCDDevice

spec = importlib.util.spec_from_file_location('qualify_core', Path(__file__).resolve().parents[1] / 'tools/qualify_core.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class Device(LCDDevice):
    """Expose request counters independently from foreground status and frame count."""

    def __init__(self, defect=None):
        super().__init__(defect)
        self.requested = 0

    def command(self, text):
        if text == 'cat /surface/timing':
            self.now += 0.001
            values = dict(frame_us=20000, max_frame_us=20000, frame_app=self.app,
                          frame_count=self.frames, pending=0, request_us=30000,
                          max_request_us=600000 if self.defect == 'request-slow' else 30000,
                          requested=self.requested, presented_request=self.requested)
            if self.defect == 'later-frame':
                values['frame_us'] = values['max_frame_us'] = 40000
            if self.defect == 'stale-frame':
                values['presented_request'] -= 1
            if self.defect == 'wrong-frame':
                values['frame_app'] = 99
            if self.defect == 'malformed':
                del values['presented_request']
            return ' '.join(f'{key}={value}' for key, value in values.items()) + '\n'
        reply = super().command(text)
        if text == 'write /surface/control start' and self.running:
            self.requested = 1
        elif text.startswith('write /surface/control launch='):
            self.requested += 1
        return reply


class CoreTests(unittest.TestCase):
    """Reject stale completed frames and requests beyond the full-frame goal."""

    def test_complete_requests(self):
        device, result = Device(), {}
        self.assertTrue(runner.qualify(device.command, result, 2, device.pause, lambda: device.now))
        self.assertEqual(len(result['request_samples']), 18)
        self.assertTrue(result['request_timing_passed'])
        self.assertFalse(result['physical_latency_qualified'])
        self.assertFalse(result['qualification_complete'])

    def test_later_repaint_keeps_request_timing(self):
        device, result = Device('later-frame'), {}
        self.assertTrue(runner.qualify(device.command, result, 2, device.pause, lambda: device.now))

    def test_request_failures(self):
        for defect in ['request-slow', 'stale-frame', 'wrong-frame', 'malformed']:
            with self.subTest(defect=defect):
                device, result = Device(defect), {}
                with self.assertRaises((ValueError, TimeoutError)):
                    runner.qualify(device.command, result, 2, device.pause, lambda: device.now)
                self.assertFalse(result['passed'])
                self.assertFalse(device.running)


if __name__ == '__main__':
    unittest.main()
