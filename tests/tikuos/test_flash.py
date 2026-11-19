#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Exercise the firmware flash/restore orchestration with simulated hardware."""
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

PRODUCT = Path(__file__).resolve().parents[2]
FIRMWARE = Path(os.environ['TIKUOS_ROOT']).resolve()
sys.path.insert(0, str(FIRMWARE / 'tools'))
import esp32s31_flash as flash

BOOT = (b'ESP-ROM:esp32s31-test\r\nLoaded app from partition at offset 0x10000\r\n'
        b'TIKU S31 SRAM shell v1\r\nflash_project=tikusurface_s31_diag\r\n'
        b'application_started=0x00000001\r\ntikuOS:/> ')


class FlashTests(unittest.TestCase):
    """Require identity/refusal guards, failure evidence and verified restoration."""

    def fixture(self):
        image = bytearray(4096)
        image[80:112] = b'tikusurface_s31_diag'.ljust(32, b'\0')
        backup = b'\xff' * 0x1000000
        sdk, application = Mock(), Mock()
        sdk.read_flash.side_effect = [backup[0x10000:0x11000], backup[0x8000:0x9000],
                                     backup[flash.STORAGE_START:flash.STORAGE_END], backup[0xffe000:]]
        application.project = b'tikusurface_s31_diag'
        application.required_runs = None
        application.console = BOOT
        application.results = [{'qualification_complete': True}]
        application.capture.return_value = (BOOT, None)
        application.passed.return_value = True
        return bytes(image), backup, sdk, application

    def test_provider_source_and_descriptor_identity(self):
        path = PRODUCT / 'tools/qualify_runtime.py'
        provider, source = flash.load_application(path)
        self.assertEqual(source['sha256'], hashlib.sha256(path.read_bytes()).hexdigest())
        image, _, _, _ = self.fixture()
        flash.validate_application_image(image, provider.project)
        with self.assertRaises(ValueError):
            flash.validate_application_image(image, b'tikuos_s31_wifi')
        for data in (BOOT + b'FATAL', BOOT + BOOT,
                     BOOT.replace(b'0x00000001', b'0x00000000'),
                     BOOT.replace(b'tikusurface_s31_diag', b'other')):
            self.assertFalse(flash.check_application_boot(data, provider.project))
        self.assertTrue(flash.check_application_boot(BOOT, provider.project))

    def test_lcd_provider_has_separate_identity(self):
        lcd, _ = flash.load_application(PRODUCT / 'tools/qualify_lcd.py')
        diagnostic, _ = flash.load_application(PRODUCT / 'tools/qualify_runtime.py')
        image, _, _, _ = self.fixture()
        with self.assertRaises(ValueError):
            flash.validate_application_image(image, lcd.project)
        image = bytearray(image)
        image[80:112] = lcd.project.ljust(32, b'\0')
        flash.validate_application_image(image, lcd.project)
        with self.assertRaises(ValueError):
            flash.validate_application_image(image, diagnostic.project)

    def test_calibration_requires_two_boots_before_board_access(self):
        provider, _ = flash.load_application(PRODUCT / 'tools/qualify_calibration.py')
        self.assertEqual(provider.required_runs, 2)
        flash.validate_application_runs(provider, 2)
        for count in (1, 3):
            with self.assertRaises(ValueError):
                flash.validate_application_runs(provider, count)
        image, backup, sdk, _ = self.fixture()
        image = bytearray(image)
        image[80:112] = provider.project.ljust(32, b'\0')
        with tempfile.TemporaryDirectory() as directory, patch.object(flash, 'connect') as connect:
            with self.assertRaises(ValueError):
                flash.qualify(sdk, Mock(), bytes(image), backup, io.BytesIO(), 1, 5,
                              dict(mutation_started=False), Path(directory) / 'journal.json',
                              firmware=True, settings=True, application=provider)
            connect.assert_not_called()
        for required in (True, 0, 101, '2'):
            provider.required_runs = required
            with self.assertRaises(ValueError):
                flash.validate_application_runs(provider, 2)

    def test_calibration_two_boot_flash_and_restore(self):
        spec = importlib.util.spec_from_file_location('calibration_fixture', PRODUCT / 'tests/test_qualify_calibration.py')
        fixture = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixture)
        boot = BOOT.replace(b'tikusurface_s31_diag', b'tikusurface_s31_lcd')

        class Wire:
            def __init__(self, defect):
                self.device = fixture.Device(defect)
                self.timeout, self.baudrate, self.data, self.resets = 3.0, 115200, b'', 0

            def reset(self):
                self.resets += 1
                self.device.boot()
                self.data = boot

            def write(self, data):
                self.data = (self.device.command(data.decode().strip('\r')) + '\r\ntikuOS:/> ').encode()

            def read(self, count):
                chunk, self.data = self.data[:count], self.data[count:]
                return chunk

        for defect in (None, 'lost-on-reset', 'slow'):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory() as directory:
                image, backup, sdk, _ = self.fixture()
                provider, _ = flash.load_application(PRODUCT / 'tools/qualify_calibration.py')
                image = bytearray(image)
                image[80:112] = provider.project.ljust(32, b'\0')
                wire, esp = Wire(defect), Mock()
                esp.hard_reset.side_effect = wire.reset
                state = dict(mutation_started=False, restored=False, boots=[])
                with patch.object(flash, 'connect', return_value=esp), \
                     patch.object(flash.ram, 'capture', return_value=(b'factory_demo: is ready', None)):
                    def run():
                        flash.qualify(sdk, wire, bytes(image), backup, io.BytesIO(), 2, 5, state,
                                      Path(directory) / 'journal.json', firmware=True, settings=True,
                                      application=provider)
                    if defect == 'lost-on-reset':
                        with self.assertRaises(ValueError):
                            run()
                    else:
                        run()
                self.assertEqual(wire.resets, 3)
                self.assertEqual(wire.timeout, 3.0)
                self.assertTrue(state['restored'] and state['storage_restored'] and state['settings_restored'] and state['factory_booted'])
                self.assertFalse(provider.results[0]['qualification_complete'])
                self.assertEqual(provider.passed(), defect is None)
                if defect is None:
                    self.assertTrue(provider.results[1]['calibration_persistence_qualified'])
                if defect == 'slow':
                    self.assertEqual([b['passed'] for b in state['boots']], [True, False])
                    self.assertTrue(provider.results[1]['calibration_persistence_qualified'])

    def test_application_requires_all_restoration_regions(self):
        image, backup, sdk, application = self.fixture()
        for flags in ({}, {'firmware': True}, {'settings': True},
                      {'firmware': True, 'settings': True, 'services': True}):
            with tempfile.TemporaryDirectory() as directory, patch.object(flash, 'connect') as connect:
                connect.side_effect = ValueError('serial connection reached')
                with self.assertRaises(ValueError):
                    flash.qualify(sdk, Mock(), image, backup, io.BytesIO(), 1, 5,
                                  dict(mutation_started=False, restored=False, boots=[]), Path(directory) / 'unused',
                                  application=application, **flags)
                connect.assert_not_called()
        sdk.write_flash.assert_not_called()

    def test_restore_success_failure_interrupt_and_partial_write(self):
        for fault in ('none', 'capture', 'interrupt', 'test-failed', 'reset', 'write', 'verify', 'restore-once'):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as directory:
                image, backup, sdk, application = self.fixture()
                state = dict(mutation_started=False, restored=False, boots=[])
                journal, log = Path(directory) / 'journal.json', io.BytesIO()
                if fault == 'capture': application.capture.side_effect = OSError('lost UART')
                if fault == 'interrupt': application.capture.side_effect = KeyboardInterrupt('cancelled')
                if fault == 'test-failed': application.passed.return_value = False
                if fault == 'reset': application.capture.return_value = (BOOT + BOOT, None)
                if fault == 'write': sdk.write_flash.side_effect = [OSError('partial write'), None, None, None]
                if fault == 'verify': sdk.verify_flash.side_effect = [OSError('verify failed'), None, None, None]
                if fault == 'restore-once': sdk.write_flash.side_effect = [None, OSError('retry'), None, None, None]
                with patch.object(flash, 'connect'), patch.object(flash.ram, 'capture', return_value=(b'factory_demo: is ready', None)):
                    try:
                        flash.qualify(sdk, Mock(), image, backup, log, 1, 5, state, journal,
                                      firmware=True, settings=True, application=application)
                    except (OSError, KeyboardInterrupt):
                        self.assertIn(fault, ('capture', 'interrupt', 'write', 'verify'))
                saved = json.loads(journal.read_text())
                self.assertTrue(all(saved[k] for k in ('restored', 'storage_restored', 'settings_restored', 'factory_booted')))
                expected = [(0x10000, backup[0x10000:0x11000]),
                            (flash.STORAGE_START, backup[flash.STORAGE_START:flash.STORAGE_END]),
                            (0xffe000, backup[0xffe000:])]
                for address, content in expected:
                    self.assertTrue(any((address, content) in call.args[1] for call in sdk.verify_flash.call_args_list))
                if fault in ('capture', 'interrupt'):
                    self.assertIn('application_error', saved)
                    self.assertIn(BOOT, log.getvalue())
                if fault in ('test-failed', 'reset'):
                    self.assertFalse(saved['boots'][0]['passed'])
                if fault in ('none', 'restore-once'):
                    self.assertTrue(saved['boots'][0]['passed'])

    def test_nonblank_reservations_refuse_flash(self):
        for offset in (flash.STORAGE_START, 0xffe000):
            image, backup, sdk, application = self.fixture()
            changed = bytearray(backup)
            changed[offset] = 0
            state = dict(mutation_started=False, restored=False, boots=[])
            with tempfile.TemporaryDirectory() as directory, patch.object(flash, 'connect'), \
                 patch.object(flash.ram, 'capture', return_value=(b'factory_demo: is ready', None)):
                with self.assertRaises(ValueError):
                    flash.qualify(sdk, Mock(), image, bytes(changed), io.BytesIO(), 1, 5,
                                  state, Path(directory) / 'journal.json', firmware=True,
                                  settings=True, application=application)
            self.assertFalse(state['mutation_started'])
            sdk.write_flash.assert_not_called()

    def test_restore_exhaustion_remains_failed(self):
        image, backup, sdk, application = self.fixture()
        sdk.write_flash.side_effect = [None, OSError('offline'), OSError('offline'), OSError('offline')]
        state = dict(mutation_started=False, restored=False, boots=[])
        with tempfile.TemporaryDirectory() as directory, patch.object(flash, 'connect'):
            journal = Path(directory) / 'journal.json'
            with self.assertRaisesRegex(RuntimeError, 'restoration failed'):
                flash.qualify(sdk, Mock(), image, backup, io.BytesIO(), 1, 5, state, journal,
                              firmware=True, settings=True, application=application)
            saved = json.loads(journal.read_text())
            self.assertTrue(saved['mutation_started'])
            self.assertFalse(saved['restored'])
            self.assertIn('restore_error', saved)


if __name__ == '__main__':
    unittest.main()
