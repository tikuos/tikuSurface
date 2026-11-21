#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Inspect or explicitly restore touch defaults over an operator-selected UART."""
import argparse
import json
from pathlib import Path
import re
import time

from qualify_calibration import calibration, DEFAULT
from qualify_runtime import Console, status


def mapping(reply):
    """Validate the status and bounds supplied by the calibration endpoint."""
    value = calibration(reply)
    state, xmin, xmax, ymin, ymax, swap, invx, invy = value
    if not (0 <= state <= 6 and 0 <= xmin < xmax <= 65535 and
            0 <= ymin < ymax <= 65535 and swap <= 1 and invx <= 1 and invy <= 1):
        raise ValueError('Unsupported calibration status or mapping')
    return value


def recover(command, result, defaults=False, pause=time.sleep, clock=time.monotonic):
    """Inspect state, or save panel defaults and return to Home after acknowledgement."""
    result.update(scope='operator-calibration-recovery', operation='defaults' if defaults else 'inspect',
                  completed=False, physical_touch_verified=False, save_outcome='not-requested',
                  commands=[])

    def call(text):
        result['commands'].append(text)
        reply = command(text)
        if 'cannot write' in reply or 'cannot read' in reply:
            raise ValueError('Calibration recovery endpoint refused the operation')
        return reply

    initial = status(call('cat /surface/status'))
    result['runtime'] = initial
    if initial['running'] != 1 or initial['stopping'] != 0:
        raise ValueError('Recovery requires a running Surface runtime that is not stopping')
    reply = call('cat /surface/display')
    rows = [line.strip() for line in reply.splitlines() if line.startswith('enabled=')]
    display = re.fullmatch(r'enabled=1 completed=([0-9]+) errors=([0-9]+)', rows[0]) if len(rows) == 1 else None
    if not display:
        raise ValueError('Recovery requires the LCD runtime')
    result['display'] = dict(completed=int(display[1]), errors=int(display[2]))
    before = mapping(call('cat /surface/calibration'))
    result['before'] = list(before)
    if not defaults:
        result['completed'] = True
        return
    if before[0] in (2, 4, 6):
        raise ValueError('Pending, unavailable or incompatible settings cannot be replaced')
    if before == (1, *DEFAULT):
        result['save_outcome'] = 'already-saved-defaults'
        result['after'] = list(before)
    else:
        result['save_outcome'] = 'unknown'
        call('write /surface/calibration ' + ','.join(map(str, DEFAULT)))
        deadline = clock() + 10
        while True:
            current = mapping(call('cat /surface/calibration'))
            result['after'] = list(current)
            if current == (1, *DEFAULT):
                result['save_outcome'] = 'acknowledged'
                break
            if current[0] == 5:
                result['save_outcome'] = 'failed'
                raise ValueError('Calibration save failed; inspect state before another request')
            if current[0] != 2 or current[1:] != before[1:]:
                raise ValueError('Unexpected mapping or status during recovery')
            if clock() >= deadline:
                raise TimeoutError('Save outcome remains unknown; inspect before another request')
            pause(0.05)
    call('write /surface/control launch=0')
    deadline = clock() + 3
    while True:
        current = status(call('cat /surface/status'))
        if current['running'] != 1 or current['stopping'] != 0:
            raise ValueError('Runtime stopped during recovery')
        if current['app'] == 0:
            result['completed'] = True
            return
        if clock() >= deadline:
            raise TimeoutError('Saved mapping acknowledged; Home transition not confirmed')
        pause(0.05)


def main(argv=None):
    """Use exclusive UART access without reset and preserve a new JSON result file."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='explicit UART0 device path')
    parser.add_argument('--output', type=Path, required=True, help='new result file')
    parser.add_argument('--defaults', action='store_true', help='persist panel defaults and return to Home')
    args = parser.parse_args(argv)
    result = {'port': args.port, 'completed': False,
              'started_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}
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
                recover(console.command, result, defaults=args.defaults)
        except (Exception, KeyboardInterrupt) as error:
            result['completed'] = False
            result['error'] = str(error)
        finally:
            json.dump(result, output, indent=2)
            output.write('\n')
    print('COMPLETE' if result['completed'] else 'INCOMPLETE', args.output)
    return 0 if result['completed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
