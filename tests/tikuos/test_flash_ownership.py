#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Exercise S31 ROM flash exclusion with host models for the two IRQ instructions."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ['TIKUOS_ROOT']).resolve()
ARCH = ROOT / 'arch/esp32s31'
FIXTURE = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <arch/esp32s31/tiku_watchdog_arch.h>
static unsigned occupied, callbacks, masked, owner=1;
static uint32_t mock_irq_disable(void) { assert(!masked); masked=1; return 8; }
static void mock_irq_restore(uint32_t saved) { assert(masked && saved==8); masked=0; }
#include "provider.c"
volatile uint8_t tiku_s31_in_trap;
int tiku_thread_in_kernel(void) { return owner; }
int tiku_s31_storage_io_busy(void) { return occupied; }
int esp_rom_spiflash_read(uint32_t a,uint32_t *p,int32_t n)
{ (void)a;(void)p;(void)n;assert(masked && !occupied);++callbacks;return 0; }
int esp_rom_spiflash_write(uint32_t a,const uint32_t *p,int32_t n)
{ (void)a;(void)p;(void)n;assert(masked && !occupied);++callbacks;return 0; }
int esp_rom_spiflash_erase_sector(uint32_t s)
{ (void)s;assert(masked && !occupied);++callbacks;return 0; }
int esp_rom_spiflash_unlock(void) { assert(masked && !occupied);++callbacks;return 0; }
uint32_t Cache_Suspend_L1_CORE0_ICache(void) { assert(masked && !occupied);++callbacks;return 3; }
uint32_t Cache_Suspend_L1_DCache(void) { assert(masked && !occupied);++callbacks;return 4; }
void Cache_Resume_L1_CORE0_ICache(uint32_t s) { assert(masked && s==3);++callbacks; }
void Cache_Resume_L1_DCache(uint32_t s) { assert(masked && s==4);++callbacks; }
#if STORAGE
void tiku_watchdog_kick(void) {}
tiku_nvm_mark_t tiku_nvm_meter_begin(void) { return 0; }
void tiku_nvm_meter_end(tiku_nvm_mark_t t,uint32_t b,uint32_t p,uint32_t e)
{ (void)t;(void)b;(void)p;(void)e; }
uint32_t tiku_nvm_units(uintptr_t a,uint32_t n,uint32_t unit)
{ (void)a;return (n+unit-1)/unit; }
#endif
int main(void) {
 for(unsigned op=0;op<=2;op++) {
  occupied=1;callbacks=0;
  assert(PROVIDER(op,ADDRESS,(void *)(uintptr_t)0x2f010000,op==1?0:4)==-1);
  assert(!callbacks && !masked);
  occupied=0;owner=0;
  assert(PROVIDER(op,ADDRESS,(void *)(uintptr_t)0x2f010000,op==1?0:4)==-1);
  assert(!callbacks && !masked);
  owner=1;
  assert(PROVIDER(op,ADDRESS,(void *)(uintptr_t)0x2f010000,op==1?0:4)==0);
  assert(callbacks>=5 && !masked);
 }
 return 0;
}
'''


class OwnershipTests(unittest.TestCase):
    """Refuse ROM reads and writes before changing caches when automatic flash owns SPI1."""

    def test_both_reservations_respect_controller_owner(self):
        for kind, address in (('storage', '0xe40000'), ('settings', '0xffe000')):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                source = (ARCH / f'tiku_{kind}_io.c').read_text()
                changes = {
                    '__asm__ volatile("csrrc %0,mstatus,%1" : "=r"(irq) : "r"(8u) : "memory");':
                        'irq = mock_irq_disable();',
                    '__asm__ volatile("csrw mstatus,%0" :: "r"(irq) : "memory");':
                        'mock_irq_restore(irq);',
                }
                for instruction, model in changes.items():
                    self.assertEqual(source.count(instruction), 1)
                    source = source.replace(instruction, model)
                (directory / 'provider.c').write_text(source)
                (directory / 'test.c').write_text(FIXTURE)
                command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1',
                           '-DTIKU_S31_STORAGE_ASYNC_ENABLE=1', '-DSTORAGE=' + str(int(kind == 'storage')),
                           '-DPROVIDER=tiku_s31_' + kind + '_io', '-DADDRESS=' + address,
                           '-I' + str(ARCH), '-I' + str(ROOT / 'TikuBench/tests/host/kernel/include'),
                           '-I' + str(ROOT), str(directory / 'test.c'), '-o', str(directory / 'test')]
                build = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                run = subprocess.run([str(directory / 'test')], capture_output=True, text=True)
                self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == '__main__':
    unittest.main()
