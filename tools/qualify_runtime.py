#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Exercise an installed diagnostic image through an explicitly selected UART."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import time

ANSI = re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')
PROMPT = re.compile(r'tikuOS:[^\r\n]*> $')
FIELDS = {'running', 'stopping', 'turns', 'frames', 'allocations', 'live_bytes',
          'peak_bytes', 'last_us', 'max_us', 'overruns', 'failures', 'app', 'media_unavailable'}


def status(reply):
    """Require exactly the known counter fields on one complete status line."""
    lines = [line.strip() for line in reply.splitlines() if line.startswith('running=')]
    if len(lines) != 1:
        raise ValueError('Missing or ambiguous runtime status')
    pairs = re.findall(r'([a-z_]+)=([0-9]+)', lines[0])
    values = {key: int(value) for key, value in pairs}
    if len(pairs) != len(FIELDS) or set(values) != FIELDS:
        raise ValueError('Runtime status fields changed or truncated')
    if ' '.join(f'{key}={value}' for key, value in pairs) != lines[0]:
        raise ValueError('Malformed runtime status')
    return values


def scalar(reply):
    """Extract one complete decimal value without treating command echo as data."""
    values = [int(line) for line in reply.splitlines() if re.fullmatch(r'[0-9]+', line)]
    if len(values) != 1:
        raise ValueError('Missing or ambiguous scalar reply')
    return values[0]


def qualify(command, result, cycles=100, pause=time.sleep, clock=time.monotonic, progress=None, lcd=False, active_seconds=0):
    """Collect lifecycle, failure, console and timer evidence; leave the runtime stopped."""
    if not 1 <= cycles <= 1000:
        raise ValueError('cycles must be between 1 and 1000')
    if not 0 <= active_seconds <= 28800 or (active_seconds and not lcd):
        raise ValueError('active_seconds requires LCD and must be between 0 and 28800')
    result.update(active_seconds=0.0, soak_samples=0)
    result.update(scope='lcd-sustained-lifecycle' if active_seconds else 'lcd-lifecycle' if lcd else 'diagnostic-runtime', touch_qualified=False,
                  physical_latency_qualified=False)
    result.update(passed=False, qualification_complete=False, cycles=0, max_dispatch_us=0, minimum_stack_free=16384,
                  timing_limit_us=5000, transcript=[])

    def call(text):
        reply = command(text)
        result['transcript'].append({'command': text, 'reply': reply})
        return reply

    def control(text, failure=False):
        reply = call('write /surface/control ' + text)
        rejected = "write: cannot write '/surface/control' (EIO)" in reply
        if failure != rejected or ('cannot write' in reply and not rejected):
            raise ValueError('Unexpected control result: ' + text)

    def sample():
        value = status(call('cat /surface/status'))
        result['max_dispatch_us'] = max(result['max_dispatch_us'], value['max_us'])
        if active_seconds and value['max_us'] > result['timing_limit_us']:
            raise ValueError('Sustained workload exceeded dispatch limit')
        return value

    def until(predicate):
        deadline = clock() + 3.0
        while True:
            value = sample()
            if predicate(value):
                return value
            if clock() >= deadline:
                raise TimeoutError('Runtime did not reach the required state')
            pause(0.01)

    def display_after(previous):
        deadline = clock() + 3.0
        while True:
            reply = call('cat /surface/display')
            rows = [line for line in reply.splitlines() if line.startswith('enabled=')]
            match = re.fullmatch(r'enabled=1 completed=([0-9]+) errors=0', rows[0]) if len(rows) == 1 else None
            if not match:
                raise ValueError('LCD disabled, faulted or malformed display status')
            completed = int(match[1])
            if completed > previous:
                return completed
            if clock() >= deadline:
                raise TimeoutError('LCD did not complete a handoff')
            pause(0.01)

    def stopped():
        control('stop')
        value = until(lambda s: not s['running'])
        if any(value[k] for k in ('stopping', 'allocations', 'live_bytes')):
            raise ValueError('Stopped runtime retains allocations')

    try:
        control('fail=0')
        stopped()
        baseline = scalar(call('cat /surface/psram_free'))
        result['baseline_psram_free'] = baseline
        for allocation in range(1, 8 if lcd else 6):
            control('fail=' + str(allocation))
            control('start', failure=True)
            value = until(lambda s: not s['running'])
            if value['failures'] != 1 or value['live_bytes'] or value['allocations']:
                raise ValueError('Startup failure did not unwind')
            if scalar(call('cat /surface/psram_free')) != baseline:
                raise ValueError('Startup failure leaked PSRAM')
        control('fail=0')
        for cycle in range(cycles):
            control('start')
            first = until(lambda s: s['running'] and s['frames'] > 0)
            completed = display_after(0) if lcd else 0
            ticks = scalar(call('cat /sys/clock/ticks'))
            for app in (1, 2, 3, 0):
                control('launch=' + str(app))
                value = until(lambda s: s['app'] == app and s['frames'] > first['frames'])
                if not lcd and app == 2 and not value['media_unavailable']:
                    raise ValueError('Diagnostic browser did not report unavailable media')
                if lcd:
                    completed = display_after(completed)
                first = value
                marker = f'surface-cycle-{cycle}-app-{app}'
                if marker not in call('echo ' + marker).splitlines():
                    raise ValueError('Console echo did not progress')
            if active_seconds:
                home = sample()
                if home['app'] != 0 or not home['running'] or home['stopping']:
                    raise ValueError('Sustained workload did not reach Home')
                began = clock()
                previous_ticks = scalar(call('cat /sys/clock/ticks'))
                while clock() - began < active_seconds / cycles:
                    pause(min(1.0, active_seconds / cycles - (clock() - began)))
                    observed = sample()
                    if (not observed['running'] or observed['stopping'] or observed['app'] != 0 or
                            observed['frames'] < home['frames'] or
                            any(observed[k] != home[k] for k in ('allocations', 'live_bytes', 'failures'))):
                        raise ValueError('Sustained runtime reset or changed retained allocations')
                    seen = display_after(-1)
                    if seen < completed:
                        raise ValueError('Display counters reset during sustained operation')
                    completed = seen
                    current_ticks = scalar(call('cat /sys/clock/ticks'))
                    if not 0 < ((current_ticks - previous_ticks) & 0xffffffff) < 0x80000000:
                        raise ValueError('Timer stalled during sustained operation')
                    previous_ticks = current_ticks
                    result['soak_samples'] += 1
                result['active_seconds'] += clock() - began
            later = scalar(call('cat /sys/clock/ticks'))
            if not 0 < ((later - ticks) & 0xffffffff) < 0x80000000:
                raise ValueError('Timer did not progress')
            free = scalar(call('cat /sys/mem/stack_free'))
            result['minimum_stack_free'] = min(result['minimum_stack_free'], free)
            stopped()
            if scalar(call('cat /surface/psram_free')) != baseline:
                raise ValueError('Runtime stop leaked PSRAM')
            result['cycles'] += 1
            if progress:
                progress(result)
        result['lifecycle_passed'] = True
        result['timing_passed'] = result['max_dispatch_us'] <= result['timing_limit_us']
        result['stack_passed'] = result['minimum_stack_free'] >= 4096
        result['passed'] = result['timing_passed'] and result['stack_passed']
        result['qualification_complete'] = (result['passed'] and result['cycles'] >= 100 and
                                            (not active_seconds or result['active_seconds'] >= 7200))
    finally:
        try:
            control('fail=0')
            stopped()
        except Exception as error:
            result['cleanup_error'] = str(error)
            result['passed'] = False
            result['qualification_complete'] = False
    return result['passed']


class Console:
    """Read bounded replies ending in the TikuOS prompt without resetting the board."""

    def __init__(self, wire):
        self.wire = wire

    def command(self, text):
        self.wire.write((text + '\r').encode('ascii'))
        deadline = time.monotonic() + 3.0
        data = bytearray()
        while time.monotonic() < deadline:
            data.extend(self.wire.read(4096))
            if len(data) > 65536:
                raise ValueError('Console reply exceeded 64 KiB')
            clean = ANSI.sub('', data.decode('utf-8', errors='replace')).replace('\r', '')
            if PROMPT.search(clean):
                return PROMPT.sub('', clean).strip('\n')
        raise TimeoutError('Console prompt timeout')


class ApplicationQualification:
    """Supply boot capture and runtime evidence to TikuOS's flash/restore runner."""

    project = b'tikusurface_s31_diag'

    def __init__(self):
        self.results = []
        self.console = bytearray()

    def capture(self, wire, seconds, run):
        """Reject an incomplete boot before commands; retain replies even on failure."""
        self.console = bytearray()
        result = {'run': run + 1, 'passed': False, 'qualification_complete': False}
        self.results.append(result)
        owner = self

        class RecordingWire:
            def write(self, data):
                return wire.write(data)

            def read(self, count):
                chunk = wire.read(count)
                owner.console.extend(chunk)
                if len(owner.console) > 16 * 1024 * 1024:
                    raise ValueError('Application console exceeded 16 MiB')
                return chunk

        recorded = RecordingWire()
        saved_timeout = wire.timeout
        try:
            wire.timeout = 0.05
            deadline = time.monotonic() + max(45, seconds)
            prompt_requested = False
            while time.monotonic() < deadline:
                recorded.read(4096)
                clean = ANSI.sub('', self.console.decode('utf-8', errors='replace')).replace('\r', '')
                if 'FATAL' in clean or 'FAIL S31' in clean:
                    raise ValueError('Application boot failed')
                if re.search(r'(?m)^tikuOS:[^\r\n]*> ', clean):
                    boot = bytes(self.console)
                    if (boot.count(b'ESP-ROM:esp32s31-') != 1 or
                            boot.count(b'Loaded app from partition at offset 0x10000') != 1 or
                            boot.count(b'TIKU S31 SRAM shell v1') != 1 or
                            b'flash_project=' + self.project + b'\r\n' not in boot or
                            b'application_started=0x00000001\r\n' not in boot):
                        raise ValueError('Application boot identity/startup mismatch')
                    if PROMPT.search(clean):
                        break
                    if not prompt_requested:
                        recorded.write(b'\r')
                        prompt_requested = True
            else:
                raise TimeoutError('Application boot prompt timeout')
            self.qualify(Console(recorded).command, result)
            if bytes(self.console).count(b'ESP-ROM:esp32s31-') != 1 or b'FATAL' in self.console or b'FAIL S31' in self.console:
                result['passed'] = result['qualification_complete'] = False
            return bytes(self.console), None
        except BaseException as error:
            result['passed'] = result['qualification_complete'] = False
            result['error'] = str(error)
            raise
        finally:
            wire.timeout = saved_timeout

    def qualify(self, command, result):
        """Run this provider's diagnostic scope after boot identity validation."""
        return qualify(command, result, cycles=100, progress=self.report_progress)

    def passed(self):
        """Require the latest boot to complete the full runtime qualification."""
        return bool(self.results and self.results[-1].get('qualification_complete'))

    def report_progress(self, result):
        """Emit completed-cycle counts and observed bounds without UART writes."""
        if result['cycles'] % 10 == 0:
            print(json.dumps({key: result[key] for key in
                              ('run', 'cycles', 'max_dispatch_us', 'minimum_stack_free')}), flush=True)


def main():
    """Require explicit device, image provenance and a new evidence output path."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--board', required=True)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cycles', type=int, default=100)
    args = parser.parse_args()
    if not 1 <= args.cycles <= 1000:
        parser.error('--cycles must be between 1 and 1000')
    digest = hashlib.sha256(args.image.read_bytes()).hexdigest()
    result = {'board': args.board, 'port': args.port, 'image_file_sha256': digest,
              'image_identity': 'operator-supplied; flash contents are not read back',
              'started_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'passed': False}
    with args.output.open('x') as output:
        try:
            import serial
            wire = serial.Serial(port=None, baudrate=115200, timeout=0.05, exclusive=True)
            wire.dtr = False
            wire.rts = False
            wire.port = args.port
            wire.open()
            with wire:
                console = Console(wire)
                console.command('')
                qualify(console.command, result, args.cycles)
        except (Exception, KeyboardInterrupt) as error:
            result['passed'] = False
            result['qualification_complete'] = False
            result['error'] = str(error)
        finally:
            json.dump(result, output, indent=2)
            output.write('\n')
    print('PASS' if result['passed'] else 'FAIL', args.output)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
