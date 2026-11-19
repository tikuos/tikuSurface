#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Apply the pinned foundation's prose and comment rules to tracked product files."""
import importlib.util
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Report prose, comment and whitespace findings in tracked product files."""
    spec = importlib.util.spec_from_file_location('surface_prose',
            ROOT / 'foundation/tools/check_prose_style.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.ROOT = str(ROOT)
    files = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '-z'], text=True)
    problems = []
    for path in files.split('\0'):
        if path.endswith('.md'):
            problems.extend(module.check(path))
    for problem in problems:
        print(problem)
    whitespace = subprocess.run(['git', '-C', str(ROOT), 'diff', '--check']).returncode
    staged = subprocess.run(['git', '-C', str(ROOT), 'diff', '--cached', '--check']).returncode
    sources = [path for path in files.split('\0') if path.endswith(('.c', '.h'))]
    comments = subprocess.run([sys.executable, str(ROOT / 'foundation/tools/check_comment_style.py'),
        '--root=' + str(ROOT), '--prose=6', '--header=20', *sources], cwd=ROOT).returncode if sources else 0
    return int(bool(problems or whitespace or staged or comments))


if __name__ == '__main__':
    sys.exit(main())
