#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Exercise the S31 boot clock transition over modeled registers and ROM timing."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ['TIKUOS_ROOT']).resolve()
FIXTURE = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define CLOCK 0x20587000u
static uint32_t clocks[6], pll, calibration;
static unsigned reads, stuck, corrupt, ticks, calls;
static uint32_t *reg(uintptr_t address)
{
    if(address==0x2070104cu)return &pll;
    if(address==0x20587174u)return &calibration;
    assert(address>=CLOCK&&address<=CLOCK+20u&&(address-CLOCK)%4==0);
    if(address==CLOCK+20u){
        ++reads;
        if((clocks[5]&1u)&&!stuck){clocks[5]&=~1u;if(corrupt)clocks[1]^=1u;}
    }
    return &clocks[(address-CLOCK)/4];
}
#define REG(a) (*reg(a))
static void rom_ticks(unsigned value){ticks=value;++calls;}
#include "provider.c"
static void reset(void)
{
    for(unsigned i=0;i<6;++i)clocks[i]=0xa5000000u;
    pll=0x81;calibration=4;reads=stuck=corrupt=ticks=calls=0;
}
int main(void)
{
    reset();pll=0x82;assert(tiku_s31_runtime_clock_ready()==-1);assert(!calls&&!reads);
    reset();calibration=0;assert(tiku_s31_runtime_clock_ready()==-1);assert(!calls&&!reads);
    reset();stuck=1;assert(tiku_s31_runtime_clock_ready()==-1);assert(!calls&&reads<=100004);
    reset();corrupt=1;assert(tiku_s31_runtime_clock_ready()==-1);assert(!calls);
    reset();assert(!tiku_s31_runtime_clock_ready());
    assert(clocks[0]==0xa5000001u&&clocks[1]==0xa5000001u&&clocks[2]==0xa5000000u);
    assert(clocks[3]==0xa5000001u&&clocks[4]==0xa5000001u&&clocks[5]==0xa5000000u);
    assert(calls==1&&ticks==160&&reads<10);
    assert(!tiku_s31_runtime_clock_ready()&&calls==2&&ticks==160);
    return 0;
}
'''


def provider():
    """Select the production transition and replace its fixed ROM entry point."""
    source = (ROOT / 'arch/esp32s31/tiku_runtime_arch.c').read_text()
    marker = '__attribute__((section(".flash.text")))\nint tiku_s31_runtime_clock_ready(void)'
    assert source.count(marker) == 1
    source = source[source.index(marker):source.rindex('#endif')]
    entry = '((void (*)(unsigned))0x2f800044u)(160);'
    assert source.count(entry) == 1
    return source.replace(entry, 'rom_ticks(160);')


class ClockTests(unittest.TestCase):
    """Keep bus dividers, refusal and ROM delay calibration tied to boot selection."""

    def run_provider(self, source):
        """Compile the transition against the register model and return execution status."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / 'provider.c').write_text(source)
            (directory / 'test.c').write_text(FIXTURE)
            command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1',
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie',
                       str(directory / 'test.c'), '-o', str(directory / 'test')]
            build = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            return subprocess.run([str(directory / 'test')], capture_output=True, text=True)

    def test_transition(self):
        run = self.run_provider(provider())
        self.assertEqual(run.returncode, 0, run.stderr)

    def test_guards_reject_mutations(self):
        source = provider()
        changes = [
            ('(REG(CLOCK + 4u) & ~0x3fffu) | 1u', '(REG(CLOCK + 4u) & ~0x3fffu) | 3u'),
            ('(REG(CLOCK + 16u) & ~0x3fffu) | 1u', '(REG(CLOCK + 16u) & ~0x3fffu) | 0u'),
            ('!(REG(0x20587174u) & 4u)) return -1;', '!(REG(0x20587174u) & 4u)) return 0;'),
            ('i == 100000u || ', ''),
            ('rom_ticks(160);', 'rom_ticks(40);'),
        ]
        for before, after in changes:
            with self.subTest(change=before):
                self.assertEqual(source.count(before), 1)
                run = self.run_provider(source.replace(before, after))
                self.assertNotEqual(run.returncode, 0, 'Faulty clock transition passed')
                self.assertIn('Assertion', run.stderr)

    def test_build_refusals(self):
        for options, message in [
            (['S31_CPU_MHZ=80'], 'S31_CPU_MHZ must be 40 or 160'),
            (['S31_CPU_MHZ=160'], 'requires a flash firmware profile'),
            (['S31_CPU_MHZ=160', 'S31_PROFILE=firmware'], 'requires CPLL-backed PSRAM'),
        ]:
            with self.subTest(options=options):
                run = subprocess.run(['make', '-n', 'MCU=esp32s31'] + options,
                                     cwd=ROOT, capture_output=True, text=True)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(message, run.stderr)


if __name__ == '__main__':
    unittest.main()
