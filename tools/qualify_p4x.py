#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Load the P4X desktop into RAM, capture the LCD front buffer and optionally leave the desktop running."""
import argparse
import re
from pathlib import Path
import sys
import time
import struct
import zlib

import serial
from serial.tools import list_ports
import esptool
# The loader and target qualification rules come from the selected TikuOS checkout.
sys.path.insert(0, str(Path("tools").resolve()))
import esp32p4_ram as ram


def until(wire, marker, seconds):
    end = time.monotonic() + seconds
    seen = bytearray()
    while time.monotonic() < end and marker not in seen:
        part = wire.read(wire.in_waiting or 1)
        seen.extend(part)
        if part:
            print(part.decode(errors="replace"), end="", flush=True)
        if b"TRAP " in seen or b"TikuSurface: the desktop ended" in seen:
            break
        if b"FAIL " in seen and not marker.startswith(b"FAIL "):
            break
    print(flush=True)
    if marker not in seen:
        raise RuntimeError(f"board did not reach {marker!r}")

def capture(wire, path):
    end = time.monotonic() + 95
    pixels = bytearray()
    rows = 0
    complete = False
    retry = 0
    while time.monotonic() < end:
        if time.monotonic() >= retry:
            wire.write(b"c")
            retry = time.monotonic() + 2
        if wire.readline().startswith(b"CAPTURE BEGIN 1024 600 "):
            break
    else:
        raise RuntimeError("capture header missing")
    while time.monotonic() < end:
        line = wire.readline().decode("ascii", errors="replace").strip()
        if line == "CAPTURE END":
            complete = True
            break
        if not line.startswith(f"F{rows:03d} "):
            continue
        runs = bytes.fromhex(line[5:])
        if len(runs) % 3:
            raise ValueError("truncated pixel run")
        row = bytearray()
        for i in range(0, len(runs), 3):
            n, high, low = runs[i:i+3]
            rgb = high << 8 | low
            row.extend(bytes([((rgb >> 11) & 31) * 255 // 31, ((rgb >> 5) & 63) * 255 // 63,
                              (rgb & 31) * 255 // 31]) * n)
        if len(row) != 1024 * 3:
            raise ValueError(f"short capture row {rows}")
        pixels.append(0)
        pixels.extend(row)
        rows += 1
    if rows != 600 or not complete:
        raise RuntimeError(f"received {rows} capture rows")
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    header = struct.pack(">2I5B", 1024, 600, 8, 2, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
                     chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))
    print(f"Captured {rows} rows in {path}", flush=True)
    return pixels

def region_changes(before, after, left, top, width, height):
    stride = 1 + 1024 * 3
    count = 0
    for y in range(top, top + height):
        start = y * stride + 1 + left * 3
        for offset in range(start, start + width * 3, 3):
            if before[offset:offset + 3] != after[offset:offset + 3]:
                count += 1
    return count

def tap(wire, key):
    wire.write(key)
    until(wire, b"Pointer tap queued\r\n", 5)
    time.sleep(4)


def menu_state(wire):
    deadline = time.monotonic() + 5
    wire.write(b"?")
    while time.monotonic() < deadline:
        line = wire.readline().strip()
        if line in (b"MENU OPEN", b"MENU CLOSED"):
            print(line.decode(), flush=True)
            return line
    raise RuntimeError("Deskbar did not report its painted state")


def open_menu(wire):
    if menu_state(wire) != b"MENU CLOSED":
        raise RuntimeError("Deskbar menu must start closed")
    tap(wire, b"o")
    if menu_state(wire) != b"MENU OPEN":
        raise RuntimeError("one tap on the TikuOS button did not open its menu")


def check_menu(wire, path, closed):
    open_menu(wire)
    opened = capture(wire, path.with_name(path.stem + "-menu.png"))
    tap(wire, b"x")
    if menu_state(wire) != b"MENU CLOSED":
        raise RuntimeError("Deskbar menu did not close")
    restored = capture(wire, path.with_name(path.stem + "-closed.png"))
    changed = region_changes(closed, opened, 520, 60, 500, 420)
    remaining = region_changes(closed, restored, 520, 60, 500, 420)
    print(f"Deskbar region changed {changed} pixels, {remaining} after dismissal", flush=True)
    if changed < 3000 or remaining > changed // 8:
        raise RuntimeError("Deskbar popup did not appear and clear")


def tap_at(wire, x, y):
    tap(wire, b"T" + struct.pack(">HH", x, y))


def pick_row(wire, key, magnify):
    wire.write(key)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        row = wire.readline().split()
        if row[:1] == [b"ROW"] and len(row) == 3:
            x, y = map(int, row[1:])
            if not (0 < x < 1024 and 0 < y < 600):
                break
            tap_at(wire, x, y)
            if not magnify:
                return
            # Pick the same row halfway across the 192 by 120 touch lens.
            lens_x = max(0, min(x - 98, 1024 - 196))
            lens_y = y - 152 if y >= 152 else min(y + 28, 600 - 124)
            tap_at(wire, lens_x + 98, lens_y + 62)
            return
    raise RuntimeError(f"missing Deskbar row for {key!r}")


def launch_touch(wire, magnify):
    open_menu(wire)
    pick_row(wire, b"s", magnify)
    pick_row(wire, b"a", magnify)
    if menu_state(wire) != b"MENU CLOSED":
        raise RuntimeError("Deskbar remained open after launching Touch")


def check_app(wire, path, desktop):
    def preference(expected):
        wire.write(b"g")
        until(wire, b"MAGNIFIER " + expected + b"\r\n", 5)

    def frame(suffix):
        return capture(wire, path.with_name(path.stem + suffix + ".png"))

    # The window and tab occupy this rectangle on the 1024 by 600 RAM desktop.
    bounds = (350, 220, 330, 185)
    preference(b"unset")
    launch_touch(wire, magnify=True)
    opened = frame("-touch")
    changed = region_changes(desktop, opened, *bounds)
    if changed < 15000:
        raise RuntimeError("Touch Preferences did not appear")
    tap_at(wire, 445, 284)
    preference(b"off")
    tap_at(wire, 368, 239)
    if region_changes(desktop, frame("-dismissed"), *bounds) > changed // 8:
        raise RuntimeError("Touch Preferences did not close")
    launch_touch(wire, magnify=False)
    preference(b"off")
    tap_at(wire, 445, 284)
    preference(b"on")
    tap_at(wire, 368, 239)
    tap_at(wire, 368, 149)
    remaining = region_changes(desktop, frame("-restored"), *bounds)
    if remaining > changed // 8:
        raise RuntimeError("Tracker did not return after restoring the magnifier")
    print(f"Touch Preferences changed {changed} pixels, {remaining} after close", flush=True)


def recorder_report(wire, key, tag, count):
    wire.write(key)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        line = wire.readline()
        match = re.search(tag + rb" ((?:[0-9]+ ){%d}[0-9]+)" % (count - 1), line)
        if match:
            return tuple(map(int, match[1].split()))
    raise RuntimeError(f"missing {tag.decode()} report")


def check_recorder(wire):
    open_menu(wire)
    pick_row(wire, b"u", True)
    pick_row(wire, b"R", True)
    saved = 0
    for take in (1, 2):
        tap_at(wire, 61, 281)
        state, starts, first, lost, faults, peak = recorder_report(wire, b"m", b"MIC", 6)
        start = time.monotonic()
        time.sleep(1)
        state2, _, last, lost2, faults2, peak2 = recorder_report(wire, b"m", b"MIC", 6)
        elapsed = time.monotonic() - start
        rate = (last - first) / elapsed
        if ((state, state2, starts) != (1, 1, take) or lost or faults or lost2 or faults2 or
                not max(peak, peak2) or not 14000 <= rate <= 18000):
            raise RuntimeError(f"capture {take} failed: {state}/{state2}, {starts} starts, "
                               f"{rate:.0f} samples/s, peaks {peak}/{peak2}, "
                               f"lost {lost}/{lost2}, faults {faults}/{faults2}")
        tap_at(wire, 151, 281)
        state, _, captured, lost, faults, _ = recorder_report(wire, b"m", b"MIC", 6)
        if state or lost or faults or captured < last:
            raise RuntimeError(f"capture {take} did not stop cleanly")
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            files, valid, samples = recorder_report(wire, b"W", b"WAV", 3)
            if files == valid == take and abs(samples - saved - captured) <= 1024:
                break
            time.sleep(.5)
        else:
            raise RuntimeError(f"capture {take}: {valid}/{files} WAVs, {samples} saved samples, "
                               f"expected about {saved + captured}")
        print(f"Take {take}: {rate:.0f} samples/s, {samples - saved} samples in WAV, "
              f"block peaks {peak}/{peak2}, zero drops and faults", flush=True)
        saved = samples


def check_live_row(wire):
    """Check the conversion workload of successive steady Tracker clock frames."""
    end = time.monotonic() + 12
    frames = {}
    while len(frames) < 3 and time.monotonic() < end:
        line = wire.readline()
        match = re.search(rb"desktop: .*?frames=(\d+) .*? px=(\d+) ", line)
        if match:
            frame, pixels = map(int, match.groups())
            frames[frame] = pixels
    if len(frames) < 3 or any(not 0 < n < 1024 * 600 // 2 for n in frames.values()):
        raise RuntimeError(f"expected bounded live-row frames, observed {frames}")
    print(f"Tracker clock frame conversion pixels: {list(frames.values())}", flush=True)


def check_ppa(wire):
    wire.write(b"A")
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        fields = wire.readline().split()
        if len(fields) == 5 and fields[0] == b"PPA":
            jobs, failed, wakes, usec = map(int, fields[1:])
            print(f"PPA jobs={jobs} failed={failed} completions={wakes} last_us={usec}", flush=True)
            if failed or not wakes or jobs - wakes not in (0, 1):
                raise RuntimeError("hardware conversion did not run cleanly")
            break
    else:
        raise RuntimeError("missing hardware conversion counters")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, default=Path("build/esp32p4/p4x/surface-ram"))
    parser.add_argument("--port", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--corrupt-segment", action="store_true", help="verify rejection of one modified byte")
    parser.add_argument("--test-menu", action="store_true", help="tap the Deskbar in software and compare its open and closed frames")
    parser.add_argument("--test-recorder", action="store_true", help="record two takes and verify their WAV files on the board")
    parser.add_argument("--test-live-row", action="store_true", help="check conversion bounds while Tracker displays the live clock row")
    parser.add_argument("--test-app", action="store_true", help="launch Touch from the Deskbar and verify its saved preference")
    parser.add_argument("--require-ppa", action="store_true",
                        help="require completed hardware conversions and zero failures")
    parser.add_argument("--capture", type=Path, help="save the native RGB565 scanout as PNG")
    parser.add_argument("--hold", type=int, choices=range(0, 181), default=0)
    parser.add_argument("--leave-running", action="store_true",
                        help="release the USB console and run the desktop until board reset")
    args = parser.parse_args()
    if args.corrupt_segment and args.require_ppa:
        parser.error("--require-ppa requires a valid desktop payload")
    if args.corrupt_segment and args.leave_running:
        parser.error("--leave-running requires a valid desktop payload")
    if (args.test_menu or args.test_app) and not args.capture:
        parser.error("--test-menu and --test-app require --capture")
    build = args.build
    assets = (build / "assets.bin").read_bytes()
    if not 0 < len(assets) <= 4 * 1024**2:
        sys.exit("invalid linked segment size")
    image = (build / "worker.bin").read_bytes()
    meta = ram.validate_image(image, revision=302)
    ram.validate_elf((build / "worker.elf").read_bytes(), meta)
    if sum(data.count(b"P4X SEGMENT READY") for _, data in meta["segments"]) != 1:
        sys.exit("unexpected test image")
    if args.corrupt_segment:
        assets = bytes([assets[0] ^ 1]) + assets[1:]
    ram.require_port(args.port, args.serial, list(list_ports.comports()))

    with ram.usb_console(serial, args.port) as wire:
        esp = ram.connect_rom(esptool, wire)
        running = False
        try:
            ram.validate_target(esp, revision=302, eco=7)
            esptool.load_ram(esp, image)
            wire.timeout = .2
            until(wire, b"P4X SEGMENT READY\r\n", 6)
            for offset in range(0, len(assets), 512):
                chunk = assets[offset:offset + 512]
                wire.write(chunk)
                if len(chunk) == 512 and wire.read(1) != b"+":
                    raise RuntimeError(f"linked segment transfer stopped at {offset}")
            if args.corrupt_segment:
                until(wire, b"FAIL segment\r\n", 8)
                print("PASS corrupted segment rejected before Deskbar startup")
                sys.exit(0)
            until(wire, b"PASS native desktop frame\r\n", 80)
            until(wire, b"PASS native desktop ready\r\n", 155)
            if args.test_live_row:
                check_live_row(wire)
            if args.capture:
                closed = capture(wire, args.capture)
                if args.test_menu:
                    check_menu(wire, args.capture, closed)
                if args.test_app:
                    check_app(wire, args.capture, closed)

            if args.test_recorder:
                check_recorder(wire)
                if args.capture:
                    capture(wire, args.capture.with_name(args.capture.stem + "-recorder.png"))
            if args.require_ppa:
                check_ppa(wire)
            if args.hold:
                print(f"Holding desktop on the panel for {args.hold} seconds", flush=True)
                deadline = time.monotonic() + args.hold
                while time.monotonic() < deadline:
                    line = wire.readline()
                    if line:
                        print(line.decode(errors="replace").rstrip(), flush=True)
                        if b"FAIL" in line or b"TRAP " in line:
                            raise RuntimeError("Deskbar stopped during the touch session")
            if args.leave_running:
                wire.write(b"r")
                until(wire, b"Desktop running from RAM\r\n", 5)
                running = True
            else:
                wire.write(b"q")
                until(wire, b"PASS native desktop session ended", 10)
        finally:
            if not running:
                esp.hard_reset()


if __name__ == "__main__":
    main()
