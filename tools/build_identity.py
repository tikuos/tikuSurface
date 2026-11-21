#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Generate the selected product and firmware revision label."""
import argparse
import json
import os
from pathlib import Path
import subprocess


def revision(root):
    """Read a repository revision without inherited Git worktree overrides."""
    env = dict(os.environ)
    for key in subprocess.check_output(['git', 'rev-parse', '--local-env-vars'], text=True).splitlines():
        env.pop(key, None)
    command = ['git', '-C', str(root)]
    sha = subprocess.check_output(command + ['rev-parse', '--short=12', 'HEAD'], env=env, text=True).strip()
    changes = subprocess.check_output(command + ['status', '--porcelain', '--untracked-files=normal'], env=env, text=True)
    return sha + ('-modified' if changes else '')


def main():
    """Write the label when its contents change, preserving incremental builds."""
    parser = argparse.ArgumentParser()
    parser.add_argument('--product', type=Path, required=True)
    parser.add_argument('--firmware', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    label = 'Surface ' + revision(args.product)
    label += ' / TikuOS ' + revision(args.firmware) if args.firmware else ' / host'
    content = '#define SURFACE_BUILD_ID ' + json.dumps(label) + '\n'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != content:
        args.output.write_text(content)


if __name__ == '__main__':
    main()
