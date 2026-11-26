#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Compare the desktop hash command with independent SHA-256 file digests."""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile



def main():
    root = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp)
        tool = directory / "hash"
        main = '#include "desktop/bridge_os.h"\nint main(int n, char **v) {\n' \
               '    for (int i = 1; i < n; i++) surface_desktop_hash_file(v[i]);\n}\n'
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-I.", "-I" + str(root),
                        "platform/tikuos/desktop_hash.c",
                        root / "tikukits/crypto/sha256/tiku_kits_crypto_sha256.c",
                        "-x", "c", "-", "-o", tool], input=main, text=True, check=True)
        paths = []
        expected = []
        for length in (0, 3, 55, 56, 64, 65, 1024, 1048593):
            path = directory / str(length)
            data = (bytes(range(256)) * (length // 256 + 1))[:length]
            path.write_bytes(data)
            paths.append(path)
            expected.append(f"hash {path}: bytes={length} sha256={hashlib.sha256(data).hexdigest()}")
        errors = [directory / "missing", directory, Path("/dev/zero")]
        result = subprocess.run([tool, *paths, *errors], capture_output=True, text=True,
                                timeout=15, check=True).stderr.splitlines()
        assert result[:len(paths)] == expected, result
        assert len(result) == len(paths) + len(errors), result
        for path, line in zip(errors, result[len(paths):]):
            assert line.startswith(f"hash {path}: errno=") and int(line.rsplit("=", 1)[1]) > 0, line
    print("PASS desktop SHA-256 vectors, multi-chunk file and errors")


if __name__ == "__main__":
    main()
