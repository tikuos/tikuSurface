#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Verify the product gitlink, foundation revision and source-content lock."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def git(root, *args, isolated=False):
    """Return Git output; isolate foreign checkouts from hook-local settings."""
    env = os.environ.copy()
    if isolated:
        local = subprocess.check_output(['git', 'rev-parse', '--local-env-vars'],
                                        text=True).splitlines()
        for name in local:
            env.pop(name, None)
    return subprocess.check_output(['git', '-C', str(root), *args],
                                   env=env, text=True).strip()


def check(root, lock):
    """Validate a checkout against a parsed product lock; raise on mismatch."""
    foundation = root / 'foundation'
    if not (foundation / '.git').exists():
        raise ValueError('Initialize foundation with git submodule update --init')
    if Path(git(foundation, 'rev-parse', '--show-toplevel', isolated=True)).resolve() != foundation.resolve():
        raise ValueError('Foundation must be a separate checkout')
    if git(foundation, 'rev-parse', 'HEAD', isolated=True) != lock['revision']:
        raise ValueError('Foundation commit differs from the product lock')
    entry = git(root, 'ls-files', '--stage', '--', 'foundation').split()
    if entry != ['160000', lock['revision'], '0', 'foundation']:
        raise ValueError('Foundation gitlink differs from the product lock')
    if git(foundation, 'status', '--porcelain', '--untracked-files=all', isolated=True):
        raise ValueError('Foundation has local source changes')
    if lock['profile'] != 'portable-ui' or lock['revision_kind'] != 'source-content':
        raise ValueError('TikuSurface requires the portable-ui source-content lock')
    metadata = json.loads((foundation / 'foundation.json').read_text())
    if metadata['id'] != lock['foundation_id']:
        raise ValueError('Foundation identity differs from the product lock')
    sys.path.insert(0, str(foundation / 'tools'))
    spec = importlib.util.spec_from_file_location('surface_foundation_bridge',
                                                 foundation / 'tools/product_bridge.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    actual = module.digest_files(module.source_files(foundation))
    if actual != lock['source_sha256']:
        raise ValueError('Foundation content differs from the product lock')
    return actual


def main():
    """Print the verified identity, or a dependency error with a failing status."""
    try:
        identity = check(ROOT, json.loads((ROOT / 'foundation.lock.json').read_text()))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print('Foundation check failed: ' + str(error), file=sys.stderr)
        return 1
    print('Foundation portable-ui: ' + identity)
    return 0


if __name__ == '__main__':
    sys.exit(main())
