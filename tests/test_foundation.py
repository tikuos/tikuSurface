# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Dependency checks reject inconsistent product locks before compilation."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from check_foundation import check


class FoundationLockTest(unittest.TestCase):
    def setUp(self):
        self.lock = json.loads((ROOT / 'foundation.lock.json').read_text())

    def test_published_pin(self):
        self.assertEqual(check(ROOT, self.lock), self.lock['source_sha256'])

    def test_hook_environment(self):
        git_dir = subprocess.check_output(
            ['git', '-C', str(ROOT), 'rev-parse', '--absolute-git-dir'],
            text=True).strip()
        with patch.dict(os.environ, {'GIT_DIR': git_dir,
                                     'GIT_WORK_TREE': str(ROOT)}):
            self.assertEqual(check(ROOT, self.lock), self.lock['source_sha256'])

    def test_hook_preserves_product_index(self):
        git_dir = subprocess.check_output(
            ['git', '-C', str(ROOT), 'rev-parse', '--absolute-git-dir'],
            text=True).strip()
        with tempfile.TemporaryDirectory() as directory:
            env = {'GIT_DIR': git_dir, 'GIT_WORK_TREE': str(ROOT),
                   'GIT_INDEX_FILE': str(Path(directory) / 'index')}
            with patch.dict(os.environ, env):
                subprocess.run(['git', '-C', str(ROOT), 'read-tree', '--empty'],
                               check=True)
                with self.assertRaisesRegex(ValueError, 'gitlink differs'):
                    check(ROOT, self.lock)

    def test_wrong_commit(self):
        self.lock['revision'] = '0' * 40
        with self.assertRaisesRegex(ValueError, 'commit differs'):
            check(ROOT, self.lock)

    def test_wrong_content(self):
        self.lock['source_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'content differs'):
            check(ROOT, self.lock)

    def test_wrong_foundation(self):
        self.lock['foundation_id'] = 'another-foundation'
        with self.assertRaisesRegex(ValueError, 'identity differs'):
            check(ROOT, self.lock)

    def test_wrong_profile(self):
        self.lock['profile'] = 'desktop'
        with self.assertRaisesRegex(ValueError, 'portable-ui'):
            check(ROOT, self.lock)

    def test_missing_checkout(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'Initialize foundation'):
                check(Path(directory), self.lock)


if __name__ == '__main__':
    unittest.main()
