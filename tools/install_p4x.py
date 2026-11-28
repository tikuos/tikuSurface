#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Install and verify a standalone TikuSurface desktop on an ESP32-P4X."""
import argparse
import hashlib
from pathlib import Path
import re
import sys
import time

import esptool
from esptool import cmds
import serial
from serial.tools import list_ports

sys.path.insert(0, str(Path("tools").resolve()))
import esp32p4_ram as ram

BOOT = 0x2000
ASSETS = 0x50000
CAPACITY = 16 * 1024**2


def image_pair(build):
    boot = (build / "worker.bin").read_bytes()
    assets = (build / "assets.bin").read_bytes()
    image = ram.validate_image(boot, revision=302)
    ram.validate_elf((build / "worker.elf").read_bytes(), image)
    if not 0 < len(assets) <= 4 * 1024**2 or len(assets) % 4 or BOOT + len(boot) > ASSETS:
        raise ValueError("image exceeds the P4X boot or desktop region")
    payloads = [part for _, part in image["segments"]]
    for marker in (b"TikuSurface flash frames=", hashlib.sha256(assets).digest()):
        if sum(part.count(marker) for part in payloads) != 1:
            raise ValueError("standalone boot image and desktop do not match")
    return boot, assets


def await_desktop(wire):
    wire.timeout = .15
    deadline = time.monotonic() + 65
    next_query = 0
    seen = bytearray()
    while time.monotonic() < deadline:
        if time.monotonic() >= next_query:
            wire.write(b"v")
            next_query = time.monotonic() + 1
        chunk = wire.read(wire.in_waiting or 1)
        if chunk:
            print(chunk.decode(errors="replace"), end="", flush=True)
            seen.extend(chunk)
            match = re.search(rb"TikuOS [^\r\n]+ TikuSurface flash frames=([1-9][0-9]*) ready=1\r?\n", seen)
            if match:
                return
            if b"FAIL " in seen or b"TRAP " in seen:
                break
            del seen[:-2048]
    raise RuntimeError("flash boot did not report a ready desktop")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--port", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--erase-all", action="store_true",
                        help="erase the whole flash before provisioning the board")
    args = parser.parse_args()
    boot, assets = image_pair(args.build)
    ram.require_port(args.port, args.serial, list(list_ports.comports()))
    with ram.usb_console(serial, args.port) as wire:
        rom = ram.connect_rom(esptool, wire)
        ram.validate_target(rom, revision=302, eco=7)
        chip = cmds.run_stub(rom)
        cmds.attach_flash(chip)
        if cmds.detect_flash_size(chip) != "16MB":
            raise ValueError("expected a 16 MiB P4X flash")
        chip.flash_set_parameters(CAPACITY)
        if args.erase_all:
            cmds.erase_flash(chip)
        for addr, data in ((ASSETS, assets), (BOOT, boot)):
            cmds.write_flash(chip, [(addr, data)], flash_mode="keep", flash_size="keep",
                             flash_freq="keep")
            cmds.verify_flash(chip, [(addr, data)], flash_mode="keep", flash_size="keep",
                              flash_freq="keep")
        cmds.reset_chip(chip)
        await_desktop(wire)


if __name__ == "__main__":
    main()
