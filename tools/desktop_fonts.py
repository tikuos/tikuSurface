#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Select the baked faces the panel desktop draws from Stomme's font data.

The panel draws at scale 1 in the Sans family, so the double-resolution
faces and the Serif and Mono families are dropped: their table entries name
the Sans face of the same size, and each face's double-resolution link is
cleared.  Monospaced sizes and styles map to the nearest kept face.  With
--last-codepoint, each kept face keeps its glyphs up to that code point; the
renderer already falls back for code points beyond a face's count.
"""
import argparse
import re
from pathlib import Path

SANS = ('plain10', 'bold10', 'plain', 'bold', 'plain14', 'bold14', 'plain16', 'bold16')
MONO = ('mono11', 'monobold11', 'mono13', 'monobold13')


def mono_alias(name):
    """Map a monospaced face to a kept one: style first, then the nearest size."""
    m = re.fullmatch(r'mono(bold)?(italic)?(\d+)', name)
    if not m:
        return None
    bold, size = m.group(1), int(m.group(3))
    kept = 11 if size <= 12 else 13
    return f'mono{"bold" if bold else ""}{kept}'


def alias(name):
    """The kept face a dropped face's table entry names."""
    if name in SANS or name in MONO:
        return name
    m = re.fullmatch(r'f\d_(.+)', name)
    if m and m.group(1) in SANS:
        return m.group(1)
    return mono_alias(name)


def truncate(text, face, last):
    """Cut one face's glyph table and coverage blob at code point @p last."""
    gl = re.search(r'static const tiku_glyph_t ' + face + r'_glyphs\[\] = \{\n(.*?)\n\};', text, re.S)
    rows = re.findall(r'\{(-?\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+)\}', gl.group(1))
    keep = [r for r in rows if int(r[0]) <= last]
    if len(keep) == len(rows):
        return text
    end = max(int(r[6]) + int(r[2]) * int(r[3]) for r in keep)
    glyphs = 'static const tiku_glyph_t ' + face + '_glyphs[] = {\n' + \
        '\n'.join('    {' + ','.join(r) + '},' for r in keep) + '\n};'
    text = text[:gl.start()] + glyphs + text[gl.end():]
    bl = re.search(r'static const unsigned char ' + face + r'_bits\[\] = \{\n(.*?)\n\};', text, re.S)
    values = [v for v in re.split(r'[,\s]+', bl.group(1)) if v][:end]
    lines = [','.join(values[i:i + 24]) + ',' for i in range(0, len(values), 24)]
    bits = 'static const unsigned char ' + face + '_bits[] = {\n    ' + '\n    '.join(lines) + '\n};'
    text = text[:bl.start()] + bits + text[bl.end():]
    head = re.compile(r'(static const tiku_font_t ' + face + r'_font = \{\n\s*' + face +
                      r'_glyphs, ' + face + r'_bits, )(\d+)')
    return head.sub(lambda m: m.group(1) + str(len(keep)), text, count=1)


def select(text, last):
    """Return the font data with only the kept faces and aliased tables."""
    names = re.findall(r'^static const tiku_font_t (\w+)_font = \{', text, re.M)
    kept = set(SANS + MONO)
    if not kept <= set(names):
        raise ValueError('Expected faces missing from the font data')
    for name in names:
        if name in kept:
            continue
        for kind in ('tiku_glyph_t ' + name + '_glyphs', 'unsigned char ' + name + '_bits',
                     'tiku_font_t ' + name + '_font'):
            block = re.compile(r'(/\* ' + name + r':[^\n]*\*/\n)?static const ' +
                               re.escape(kind) + r'(\[\])? = \{\n.*?\n\};\n\n?', re.S)
            text, n = block.subn('', text, count=1)
            if n != 1:
                raise ValueError('Could not drop ' + kind)
    text = re.sub(r'(_bits, \d+, \d+, \d+, \d+, )&\w+_font(, )', r'\1NULL\2', text)

    def entry(m):
        target = alias(m.group(1))
        if target is None:
            raise ValueError('No kept face for ' + m.group(1))
        return '&' + target + '_font'
    tables = text.index('/* The fixed-advance faces')
    text = text[:tables] + re.sub(r'&(\w+)_font', entry, text[tables:])
    if last is not None:
        for face in sorted(kept):
            text = truncate(text, face, last)
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--last-codepoint', type=lambda v: int(v, 0))
    args = parser.parse_args()
    out = select(args.input.read_text(), args.last_codepoint)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(out)


if __name__ == '__main__':
    main()
