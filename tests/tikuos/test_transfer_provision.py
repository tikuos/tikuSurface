#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Provisioning and pending-listener commands over an isolated console fixture."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(os.environ.get('TIKUOS_ROOT', Path(__file__).resolve().parents[3] / 'firmware')).resolve()
spec = importlib.util.spec_from_file_location('transfer_host', ROOT / 'tools/tiku_media_transfer.py')
mt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mt)


class Commands(unittest.TestCase):
    def test_console_results(self):
        for ending in (b'\n', b'\r\n'):
            for code in (b'0', b'1', b'-6', b'10'):
                console = mt.USB.__new__(mt.USB)
                console.wire = Mock()
                console.wire.read.return_value = b'media: result=' + code + ending + b'tikuOS:/> '
                if code == b'0':
                    self.assertEqual(console.command('media ready'), 0)
                else:
                    with self.assertRaises(RuntimeError):
                        console.command('media ready')
                if code in (b'0', b'1'):
                    self.assertEqual(console.command('media ready', allow_pending=True), int(code))
                else:
                    with self.assertRaises(RuntimeError):
                        console.command('media ready', allow_pending=True)

    def test_readiness(self):
        wire = Mock()
        wire.command.side_effect = [1, 1, 0]
        with patch.object(mt.time, 'sleep'):
            mt.enable_tcp(wire, 7777, bytes([7]) * 32)
        self.assertEqual(wire.command.call_count, 3)
        self.assertEqual(wire.command.call_args_list[-1].args, ('media ready',))
        self.assertTrue(wire.command.call_args_list[-1].kwargs['allow_pending'])
        wire.reset_mock()
        wire.command.side_effect = [0]
        mt.enable_tcp(wire, 7777, bytes([7]) * 32)
        self.assertEqual(wire.command.call_count, 1)

    def test_readiness_failures(self):
        wire = Mock()
        wire.command.side_effect = [1, RuntimeError('refused')]
        with self.assertRaisesRegex(RuntimeError, 'refused'):
            mt.enable_tcp(wire, 7777, bytes([7]) * 32)
        wire.command.side_effect = [1, 1]
        with patch.object(mt.time, 'monotonic', side_effect=[0, 1, 31]), patch.object(mt.time, 'sleep'):
            with self.assertRaises(TimeoutError):
                mt.enable_tcp(wire, 7777, bytes([7]) * 32)

    def test_provision_does_not_enable(self):
        with tempfile.TemporaryDirectory() as directory:
            key_file = Path(directory) / 'key'
            key = bytes([7]) * 32
            wire = Mock()
            wire.command.return_value = 0
            output = io.StringIO()
            args = ['media', '--serial', 'fixture', '--key-file', str(key_file), 'provision', '--port', '7777']
            with patch.object(mt.sys, 'argv', args), patch.object(mt, 'USB', return_value=wire), \
                    patch.object(mt.os, 'urandom', return_value=key), contextlib.redirect_stdout(output):
                self.assertEqual(mt.main(), 0)
            wire.command.assert_called_once_with('media profile 7777 ' + key.hex())
            self.assertEqual(key_file.read_bytes(), key)
            self.assertEqual(key_file.stat().st_mode & 0o777, 0o600)
            self.assertNotIn(key.hex(), output.getvalue())
            wire.close.assert_called_once()


if __name__ == '__main__':
    unittest.main()
