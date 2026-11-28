# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Pack the product's stock pictures into the board filesystem image."""
from pathlib import Path
import json
import sys

pictures = sorted(map(Path, sys.argv[1:]))
for index, path in enumerate(pictures):
    data = path.read_bytes()
    print(f"static const unsigned char wallpaper_{index}[] = {{")
    for offset in range(0, len(data), 16):
        print(", ".join(f"0x{b:02x}" for b in data[offset:offset+16]) + ",")
    print("};")
print("static const struct { const char *path; const unsigned char *data; size_t size; } wallpapers[] = {")
for index, path in enumerate(pictures):
    name = json.dumps("/system/wallpapers/" + path.name)
    print(f"    {{{name}, wallpaper_{index}, sizeof wallpaper_{index}}},")
print("};")
