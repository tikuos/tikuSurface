#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""The desktop build copies the font's sources again when any of them changes (tikuSurface#11)."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ('tiku_font.c', 'tiku_font_utf8.inl', 'tiku_font_blend.inl')


def plan(changed):
    """What a make dry run does for the copied font when only @p changed, if
    any, is newer than the copy: the foundation's files, then the generated
    font data, then the copies, then the changed file."""
    base = (ROOT / 'tools/desktop_fonts.py').stat().st_mtime + 100
    with tempfile.TemporaryDirectory(prefix='surface-font-') as tmp:
        tmp = Path(tmp)
        interface = tmp / 'foundation/kits/interface'
        fonts = tmp / 'build/generated/fonts'
        interface.mkdir(parents=True)
        fonts.mkdir(parents=True)
        (tmp / 'assets').mkdir()
        for name in SOURCES + ('tiku_font_data.h',):
            (interface / name).write_text('/* ' + name + ' */\n')
            os.utime(interface / name, (base, base))
            (fonts / name).write_text('/* copy */\n')
            os.utime(fonts / name, (base + 20, base + 20))
        os.utime(fonts / 'tiku_font_data.h', (base + 10, base + 10))
        if changed:
            os.utime(interface / changed, (base + 30, base + 30))
        return subprocess.run(
            ['make', '-n', '-f', str(ROOT / 'desktop/desktop.mk'),
             'DESKTOP_FOUNDATION=' + str(tmp / 'foundation'),
             'DESKTOP_ASSETS=' + str(tmp / 'assets'),
             'DESKTOP_BUILD=' + str(tmp / 'build'), str(fonts / 'tiku_font.c')],
            capture_output=True, text=True, cwd=tmp).stdout


class FontCopy(unittest.TestCase):
    """The copy follows every file it copies, and only them."""

    def test_unchanged_copy_stays(self):
        self.assertNotIn('cp ', plan(None))

    def test_each_copied_source_is_followed(self):
        for name in SOURCES:
            with self.subTest(changed=name):
                self.assertIn('cp ', plan(name))


if __name__ == '__main__':
    unittest.main()
