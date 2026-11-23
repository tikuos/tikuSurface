#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Refuse an image that runs a C-library system call from libnosys, or whose
heap hooks come from anywhere but TikuOS's library and the C library.

The S31 link ends in `-lc -lnosys`: a system call that no object defines is
taken from libnosys, whose definitions set errno to ENOSYS and return -1.
The link succeeds and the call fails at run time.  This check reads the link
map for the libnosys members the link pulled in and kept, confirms each
member's symbol in the image, and refuses any not named in ALLOWED.

The desktop's heap is TikuOS's compat/posix heap group: `_sbrk_r` and the
allocator's lock, `__malloc_lock` and `__malloc_unlock`, must come from the
library's object.  A definition in the product would win the link silently,
and newlib's own lock would let the kernel thread trap on a thread's
allocation; HEAP_HOOKS names where each must come from.
"""
import argparse
import re
import subprocess
import sys

# Members kept in every native image before this check existed: newlib's
# process calls, which the desktop's own process stubs do not reach.
ALLOWED = ('_execve', '_fork', '_kill', '_wait')

MEMBER = re.compile(r'(\S*libnosys\.a\(([^)]+)\))')
REFERENCE = re.compile(r'\((\w+)\)\s*$')
SECTION = re.compile(r'^\s*(\S+)?\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S*libnosys\.a\(([^)]+)\))\s*$')
CODE = ('.text', '.iram', '.literal')
# Each heap hook and the object its code must come from.
HEAP_HOOKS = (('_sbrk_r', 'compat/posix/tiku_posix_sbrk.o'),
              ('__malloc_lock', 'compat/posix/tiku_posix_sbrk.o'),
              ('__malloc_unlock', 'compat/posix/tiku_posix_sbrk.o'))
PLACED = re.compile(r'^\s*(\S+)?\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S+)\s*$')


def members(text):
    """The libnosys members the link pulled in: {member: symbol}.

    Reads the map's table of archive members and the symbol each was pulled
    in for.  Raises ValueError when the map has no such table.
    """
    lines = text.splitlines()
    try:
        start = lines.index('Archive member included to satisfy reference by file (symbol)')
    except ValueError:
        raise ValueError('the map has no archive member table')
    found, pending = {}, None
    for line in lines[start + 1:]:
        if line.startswith(('Discarded input sections', 'Allocating common symbols',
                            'Memory Configuration')):
            break
        if line and not line[0].isspace():
            m = MEMBER.match(line)
            pending = m.group(2) if m else None
            if pending is None:
                continue
            line = line[m.end():]
        if pending is not None:
            r = REFERENCE.search(line)
            if r:
                found[pending] = r.group(1)
                pending = None
    return found


def kept(text):
    """The code each libnosys member keeps in the image: {member: (address, size)}.

    Reads only the map's memory map, which lists sections the link placed;
    the discarded input sections come before it.  Only code sections count:
    a member's attribute and comment sections are listed there whether or
    not its code was kept.  A section name that is too long sits on the line
    above its address.
    """
    lines = text.splitlines()
    try:
        start = lines.index('Linker script and memory map')
    except ValueError:
        raise ValueError('the map has no memory map')
    found, name = {}, None
    for line in lines[start + 1:]:
        m = SECTION.match(line)
        if m is None:
            s = line.strip()
            name = s if s.startswith('.') and len(s.split()) == 1 else None
            continue
        section = m.group(1) or name
        name = None
        address, size = int(m.group(2), 16), int(m.group(3), 16)
        if section is None or not section.startswith(CODE) or size == 0:
            continue
        member = m.group(5)
        low, high = found.get(member, (address, address + size))
        found[member] = (min(low, address), max(high, address + size))
    return {k: (low, high - low) for k, (low, high) in found.items()}


def check(text, symbols=None, allowed=ALLOWED):
    """The live libnosys calls outside @p allowed, as messages.

    @p symbols maps a name to its address in the image; when given, each
    kept member's symbol must lie in that member's code, and a map that says
    otherwise is reported.  An empty list passes.
    """
    pulled, code = members(text), kept(text)
    errors = []
    for member, symbol in sorted(pulled.items()):
        if member not in code:
            continue
        address, size = code[member]
        if symbols is not None:
            at = symbols.get(symbol)
            if at is None or not address <= at < address + size:
                errors.append('%s: the image does not place %s in libnosys(%s)'
                              % (symbol, symbol, member))
                continue
        if symbol not in allowed:
            errors.append('%s comes from libnosys(%s) and returns ENOSYS' % (symbol, member))
    return errors


def hook_owners(text):
    """The object whose code each function section comes from: {section: object}.

    Reads the map's memory map; a section name that is too long sits on the
    line above its address.  Only sections of nonzero size count.
    """
    lines = text.splitlines()
    try:
        start = lines.index('Linker script and memory map')
    except ValueError:
        raise ValueError('the map has no memory map')
    found, name = {}, None
    for line in lines[start + 1:]:
        m = PLACED.match(line)
        if m is None:
            s = line.strip()
            name = s if s.startswith('.') and len(s.split()) == 1 else None
            continue
        section = m.group(1) or name
        name = None
        if section is not None and section.startswith(CODE) and int(m.group(3), 16) > 0:
            found.setdefault(section, m.group(4))
    return found


def check_heap(text, hooks=HEAP_HOOKS):
    """Each heap hook missing or from another object, as messages."""
    owners = hook_owners(text)
    errors = []
    for symbol, want in hooks:
        owner = owners.get('.text.' + symbol)
        if owner is None:
            errors.append('%s is not in the image' % symbol)
        elif not owner.endswith(want):
            errors.append('%s comes from %s, not %s' % (symbol, owner, want))
    return errors


def image_symbols(elf, nm):
    """Defined symbols of @p elf by address, read with @p nm."""
    out = subprocess.run([nm, '--defined-only', elf], capture_output=True, text=True,
                         check=True).stdout
    found = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            found[parts[2]] = int(parts[0], 16)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--map', required=True)
    parser.add_argument('--elf', default=None)
    parser.add_argument('--nm', default=None)
    args = parser.parse_args()
    text = open(args.map, errors='replace').read()
    symbols = None
    if args.elf is not None:
        nm = args.nm
        if nm is None:
            m = re.search(r'(\S*riscv32-esp-elf/bin/)', text)
            nm = (m.group(1) + 'riscv32-esp-elf-nm') if m else 'riscv32-esp-elf-nm'
        symbols = image_symbols(args.elf, nm)
    try:
        errors = check(text, symbols)
        heap = check_heap(text)
    except ValueError as e:
        print('libc stubs: ' + str(e))
        return 1
    for e in errors:
        print('libc stubs: ' + e)
    if not errors:
        print('libc stubs: none outside ' + ' '.join(ALLOWED))
    for e in heap:
        print('heap hooks: ' + e)
    if not heap:
        print('heap hooks: ' + ', '.join('%s from %s' % h for h in HEAP_HOOKS))
    return 1 if errors or heap else 0


if __name__ == '__main__':
    sys.exit(main())
