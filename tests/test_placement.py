#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Exercise firmware placement refusals without requiring a cross compiler."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('placement', Path(__file__).resolve().parents[1] / 'tools/check_placement.py')
placement = importlib.util.module_from_spec(spec)
spec.loader.exec_module(placement)


class PlacementTests(unittest.TestCase):
    """Reject misplaced product/resident ranges and exhausted budgets."""

    def baseline(self):
        """Construct the required symbol contract inside admitted ranges."""
        names = ('surface_runtime_start', 'surface_runtime_poll', 'tiku_application_start', 'surface_paint')
        inputs = {name: {'address': 0, 'size': 4, 'kind': 'T'} for name in names}
        final = {name: dict(value, address=placement.IROM[0] + i * 8) for i, (name, value) in enumerate(inputs.items())}
        final.update({name: {'address': placement.SRAM[0], 'size': 4, 'kind': 'T'} for name in placement.RESIDENT})
        for name, address in (('__surface_irom_start', placement.IROM[0]),
                              ('__surface_irom_end', placement.IROM[0] + 1000),
                              ('__tier_sram_start', placement.SRAM[0]),
                              ('__tier_sram_end', placement.SRAM[0] + 102720)):
            final[name] = {'address': address, 'size': 0, 'kind': 'A'}
        return final, inputs

    def test_symbol_placement(self):
        final, inputs = self.baseline()
        self.assertEqual(placement.validate(final, inputs)['ui_irom_bytes'], 1000)
        for name, address in (('surface_paint', placement.SRAM[0]), ('memcpy', placement.IROM[0])):
            changed = {key: dict(value) for key, value in final.items()}
            changed[name]['address'] = address
            with self.assertRaises(ValueError):
                placement.validate(changed, inputs)

    def test_budgets_and_missing_anchor(self):
        for name, address in (('__surface_irom_end', placement.IROM[0] + 196609),
                              ('__tier_sram_end', placement.SRAM[0] + 100319)):
            final, inputs = self.baseline()
            final[name]['address'] = address
            with self.assertRaises(ValueError):
                placement.validate(final, inputs)
        final, inputs = self.baseline()
        del final['tiku_s31_time']
        with self.assertRaises(ValueError):
            placement.validate(final, inputs)

    def test_anonymous_constants_and_writable_ranges(self):
        good = {'section': '.srodata.cst4', 'address': placement.IROM[0], 'size': 4}
        self.assertEqual(placement.validate_sections([good]), 0)
        for entry in (dict(good, address=placement.SRAM[0]),
                      dict(good, address=placement.IROM[1] - 2),
                      dict(good, section='.bss.runtime', address=placement.SRAM[0], size=2049),
                      dict(good, section='.unknown')):
            with self.assertRaises(ValueError):
                placement.validate_sections([entry])

    def test_display_descriptor_accounting(self):
        descriptor = {'section': '.bss.descriptors', 'address': placement.SRAM[0],
                      'size': 6112, 'object': 'build/lcd/arch/esp32s31/tiku_display_arch.o'}
        state = dict(descriptor, section='.bss.state', size=1024)
        self.assertEqual(placement.validate_sections([descriptor, state], True), 1024)
        for entries in ([], [dict(descriptor, size=6113)], [descriptor, descriptor],
                        [dict(descriptor, object='unrelated.o')],
                        [descriptor, dict(state, size=2049)]):
            with self.assertRaises(ValueError):
                placement.validate_sections(entries, True)

    def test_nvm_orchestration_placement(self):
        """Reject flash-resident core paths, SRAM-resident async paths and moved writable state."""
        obj='build/lcd/arch/esp32s31/tiku_storage_arch.o'
        entries=[{'section': name, 'address': placement.IROM[0], 'size': 16, 'object': obj}
                 for name in ('.text.tiku_s31_storage_write_begin', '.text.tiku_s31_storage_write_poll')]
        resident={'section': '.text.tiku_mem_arch_nvm_flush_status', 'address': placement.SRAM[0],
                  'size': 16, 'object': obj}
        state=dict(resident,section='.bss.active_write',size=4)
        self.assertEqual(len(placement.validate_nvm_sections(entries+[resident,state])),2)
        for changed in (entries[:-1], [dict(entries[0],address=placement.SRAM[0])]+entries[1:],
                        entries+[dict(resident,address=placement.IROM[0])],
                        entries+[dict(state,address=placement.IROM[0])]):
            with self.assertRaises(ValueError):
                placement.validate_nvm_sections(changed)

    def test_tier_helper_regions(self):
        entries = [{'section': name, 'address': placement.IROM[0], 'size': 16}
                   for name in ('.text.arena_create', '.text.work_fit', '.text.tiku_tier_arena_create_span')]
        resident = {'section': '.text.tiku_tier_init', 'address': placement.SRAM[0], 'size': 16}
        self.assertEqual(len(placement.validate_tier_sections(entries + [resident])), 3)
        for altered in (entries[:-1], [dict(entries[0], address=placement.SRAM[0])] + entries[1:],
                        entries + [dict(resident, address=placement.IROM[0])]):
            with self.assertRaises(ValueError):
                placement.validate_tier_sections(altered)

    def test_sd_helper_regions(self):
        base = {'object': 'build/lcd/arch/esp32s31/tiku_sdcard_arch.o', 'size': 16}
        resident = dict(base, section='.text.crc7', address=placement.SRAM[0])
        flash = dict(base, section='.text.tiku_s31_sd_poll', address=placement.IROM[0])
        self.assertEqual(placement.validate_sections([resident, flash]), 0)
        for entries in ([dict(resident, address=placement.IROM[0]), flash],
                        [resident, dict(flash, address=placement.SRAM[0])],
                        [dict(resident, object='unrelated.o'), flash]):
            with self.assertRaises(ValueError):
                placement.validate_sections(entries)

    def test_media_regions(self):
        entries = [dict(object='tiku_mfs.o', section='.text.tiku_mfs_query_step', address=placement.IROM[0], size=16),
                   dict(object='tiku_media_card.o', section='.text.tiku_media_card_read_step', address=placement.IROM[0], size=16),
                   dict(object='tiku_mfs.o', section='.rodata.table', address=placement.IROM[0], size=16),
                   dict(object='tiku_media_card.o', section='.bss.service', address=placement.SRAM[0], size=16)]
        self.assertEqual(len(placement.validate_media_sections(entries)), 3)
        with self.assertRaises(ValueError):
            placement.validate_media_sections(entries[1:])
        for i in range(4):
            altered = [dict(e) for e in entries]
            altered[i]['address'] = placement.IROM[0] if i == 3 else placement.SRAM[0]
            with self.assertRaises(ValueError):
                placement.validate_media_sections(altered)
        placement.validate_media_sections(entries + [dict(object='tiku_mfs.o', section='.debug_info', address=0, size=16)])

    def test_transfer_snapshot_region(self):
        final, inputs = self.baseline()
        final['tiku_media_transfer_server_status'] = {'address': placement.IROM[0], 'size': 16, 'kind': 'T'}
        placement.validate(final, inputs)
        final['tiku_media_transfer_server_status']['address'] = placement.SRAM[0]
        with self.assertRaisesRegex(ValueError, 'Transfer snapshot outside IROM'):
            placement.validate(final, inputs)

    def test_radio_observation_region(self):
        final, inputs = self.baseline()
        final['tiku_wireless_observe'] = {'address': placement.IROM[0], 'size': 16, 'kind': 'T'}
        placement.validate(final, inputs)
        final['tiku_wireless_observe']['address'] = placement.SRAM[0]
        with self.assertRaisesRegex(ValueError, 'Radio observation outside IROM'):
            placement.validate(final, inputs)

    def test_storage_resident_symbols(self):
        final, inputs = self.baseline()
        for name in placement.STORAGE_RESIDENT_SYMBOLS:
            inputs[name] = {'address': 0, 'size': 4, 'kind': 'T'}
            final[name] = dict(inputs[name], address=placement.SRAM[0])
        placement.validate(final, inputs)
        final['tiku_s31_mfs_block_init']['address'] = placement.IROM[0]
        with self.assertRaises(ValueError):
            placement.validate(final, inputs)

    def test_i2c_helper_regions(self):
        entries = [{'section': name, 'address': placement.IROM[0], 'size': 16}
                   for name in ('.text.run_batch', '.text.budget_expired', '.text.tiku_i2c_arch_budget_begin')]
        resident = {'section': '.text.transact', 'address': placement.SRAM[0], 'size': 16}
        self.assertEqual(len(placement.validate_i2c_sections(entries + [resident])), 3)
        for altered in (entries[:-1], [dict(entries[0], address=placement.SRAM[0])] + entries[1:],
                        entries + [dict(resident, address=placement.IROM[0])]):
            with self.assertRaises(ValueError):
                placement.validate_i2c_sections(altered)


if __name__ == '__main__':
    unittest.main()
