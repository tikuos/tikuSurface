# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Build labels identify selected repositories under Git hook environments."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BuildIdentityTests(unittest.TestCase):
    def test_selected_repositories_and_stable_header(self):
        """Foreign Git overrides cannot replace product or firmware identity."""
        expected = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', '--short=12', 'HEAD'], text=True).strip()
        foundation = ROOT / 'foundation'
        other = subprocess.check_output(['git', '-C', str(foundation), 'rev-parse', '--short=12', 'HEAD'], text=True).strip()
        git_dir = subprocess.check_output(['git', '-C', str(foundation), 'rev-parse', '--absolute-git-dir'], text=True).strip()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'identity.h'
            env = dict(os.environ, GIT_DIR=git_dir, GIT_WORK_TREE=str(foundation),
                       GIT_INDEX_FILE=str(Path(directory) / 'absent-index'))
            command = [sys.executable, str(ROOT / 'tools/build_identity.py'), '--product', str(ROOT),
                       '--firmware', str(foundation), '--output', str(output)]
            subprocess.run(command, env=env, check=True)
            content = output.read_text()
            self.assertIn('Surface ' + expected, content)
            self.assertIn(' / TikuOS ' + other, content)
            self.assertNotIn(other + '-modified', content)
            changed = subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True)
            self.assertEqual(expected + '-modified' in content, bool(changed))
            stamp = output.stat().st_mtime_ns
            subprocess.run(command, env=env, check=True)
            self.assertEqual(stamp, output.stat().st_mtime_ns)
