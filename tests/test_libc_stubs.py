#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Refuse kept libnosys system calls, read from link maps in the linker's format."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('libc_stubs', Path(__file__).resolve().parents[1] / 'tools/libc_stubs.py')
stubs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stubs)

LIB = '/opt/tc/riscv32-esp-elf/lib/rv32imafc/ilp32f/libnosys.a'
LIBC = '/opt/tc/riscv32-esp-elf/lib/rv32imafc/ilp32f/libc.a'


def table(*rows):
    """An archive member table: each row a (member, referrer, symbol)."""
    out = ['Archive member included to satisfy reference by file (symbol)', '']
    for member, referrer, symbol in rows:
        name = '%s(%s)' % (LIB, member)
        out += [name, '%30s%s(%s) (%s)' % ('', LIBC, referrer, symbol)]
    return out


def section(name, address, size, member):
    """One placed input section line of the memory map."""
    return ' %-14s 0x%08x %#10x %s(%s)' % (name, address, size, LIB, member)


def text_map(rows, discarded=(), placed=()):
    """A whole map: the member table, the discarded inputs, the memory map."""
    lines = table(*rows)
    lines += ['', 'Discarded input sections', '']
    lines += [section('.text', 0, 0x10, m) for m in discarded]
    lines += ['', 'Memory Configuration', '', 'Linker script and memory map', '']
    lines += list(placed)
    return '\n'.join(lines) + '\n'


class LibcStubTests(unittest.TestCase):
    """The image before the fdopen fix kept _fcntl from libnosys; after, none."""

    def baseline(self):
        """The four process calls every native image keeps."""
        return [('execve.o', 'libc_a-execr.o', '_execve'), ('fork.o', 'libc_a-execr.o', '_fork'),
                ('kill.o', 'libc_a-signalr.o', '_kill'), ('wait.o', 'libc_a-execr.o', '_wait')]

    def test_allowed_calls_pass(self):
        """The process calls are kept and allowed."""
        text = text_map(self.baseline(), placed=[section('.text', 0x2f017908, 0x10, 'fork.o')])
        self.assertEqual(stubs.check(text, {'_fork': 0x2f017908}), [])

    def test_kept_fcntl_is_refused(self):
        """The case behind the dead theme switch: fdopen reached libnosys's _fcntl."""
        rows = self.baseline() + [('getpid.o', 'libc_a-signalr.o', '_getpid'),
                                  ('fcntl.o', 'libc_a-fcntlr.o', '_fcntl')]
        placed = [section('.text', 0x2f01783c, 0x10, 'getpid.o'),
                  section('.text', 0x2f01784c, 0x20, 'fcntl.o')]
        errors = stubs.check(text_map(rows, placed=placed),
                             {'_getpid': 0x2f01783c, '_fcntl': 0x2f01784c})
        self.assertEqual(len(errors), 2)
        self.assertIn('_fcntl comes from libnosys(fcntl.o)', errors[0])
        self.assertIn('_getpid comes from libnosys(getpid.o)', errors[1])

    def test_discarded_member_passes(self):
        """A member pulled in but whose code the link discarded runs nothing,
        though its attribute and comment sections are still in the memory map."""
        rows = self.baseline() + [('fcntl.o', 'libc_a-fcntlr.o', '_fcntl')]
        placed = [' .riscv.attributes',
                  '                0x00007f97       0x63 %s(fcntl.o)' % LIB,
                  section('.comment', 0, 0x30, 'fcntl.o')]
        self.assertEqual(stubs.check(text_map(rows, discarded=['fcntl.o'], placed=placed)), [])

    def test_long_section_name(self):
        """A section name on its own line still places its member."""
        rows = self.baseline() + [('open.o', 'libc_a-openr.o', '_open')]
        placed = [' .text._open', '                0x40020000       0x18 %s(open.o)' % LIB]
        errors = stubs.check(text_map(rows, placed=placed))
        self.assertEqual(errors, ['_open comes from libnosys(open.o) and returns ENOSYS'])

    def test_member_on_one_line(self):
        """A short member name shares its line with the referrer."""
        text = '\n'.join(['Archive member included to satisfy reference by file (symbol)', '',
                          'libnosys.a(fcntl.o)           libc.a(libc_a-fcntlr.o) (_fcntl)', '',
                          'Linker script and memory map', '',
                          ' .text          0x2f01784c       0x20 libnosys.a(fcntl.o)']) + '\n'
        self.assertEqual(stubs.check(text), ['_fcntl comes from libnosys(fcntl.o) and returns ENOSYS'])

    def test_symbol_outside_member(self):
        """The image must place the symbol in the member's code."""
        rows = self.baseline() + [('fcntl.o', 'libc_a-fcntlr.o', '_fcntl')]
        text = text_map(rows, placed=[section('.text', 0x2f01784c, 0x20, 'fcntl.o')])
        errors = stubs.check(text, {'_fcntl': 0x40021000})
        self.assertEqual(errors, ['_fcntl: the image does not place _fcntl in libnosys(fcntl.o)'])

    def test_unreadable_map(self):
        """A map without the member table or the memory map is refused, not passed."""
        with self.assertRaisesRegex(ValueError, 'archive member table'):
            stubs.check('Linker script and memory map\n')
        with self.assertRaisesRegex(ValueError, 'memory map'):
            stubs.check('\n'.join(table(*self.baseline())) + '\n')


if __name__ == '__main__':
    unittest.main()
