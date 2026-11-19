#!/bin/sh
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Install this checkout's dependency and commit-message checks.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
python3 "$root/tools/check_foundation.py"
git -C "$root" config --local core.hooksPath "$root/hygiene/githooks"
printf 'TikuSurface commit hook: %s\n' "$root/hygiene/githooks"
