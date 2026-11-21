#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Core desktop lifecycle and request-handoff checks for the protected flash runner."""
import importlib.util
from pathlib import Path
import re
import time

_spec = importlib.util.spec_from_file_location('surface_core_qualification',
                                              Path(__file__).with_name('qualify_runtime.py'))
_runtime = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_runtime)
TIMING_FIELDS = {'frame_us', 'max_frame_us', 'frame_app', 'frame_count', 'pending',
                 'request_us', 'max_request_us', 'requested', 'presented_request'}


def timing(reply):
    """Require one complete timing record with the declared counter names."""
    lines = [line for line in reply.splitlines() if line.startswith('frame_us=')]
    if len(lines) != 1:
        raise ValueError('Missing or ambiguous frame timing')
    pairs = re.findall(r'([a-z_]+)=([0-9]+)', lines[0])
    values = {key: int(value) for key, value in pairs}
    if len(pairs) != len(TIMING_FIELDS) or set(values) != TIMING_FIELDS or \
            ' '.join(f'{key}={value}' for key, value in pairs) != lines[0]:
        raise ValueError('Malformed frame timing')
    return values


def qualify(command, result, cycles=100, pause=time.sleep, clock=time.monotonic, progress=None):
    """Wait for each requested foreground to reach LCD handoff within 500 ms."""
    expected, previous = 0, 0
    result.update(request_limit_us=500000, max_request_us=0, request_samples=[],
                  request_timing_passed=False, timing_transcript=[])

    def checked(text):
        nonlocal expected, previous
        reply = command(text)
        if text == 'write /surface/control stop':
            expected = None
        elif text == 'write /surface/control start':
            expected = None if 'cannot write' in reply else 0
            previous = 0
        elif text.startswith('write /surface/control launch=') and 'cannot write' not in reply:
            expected = int(text.rsplit('=', 1)[1])
        elif text == 'cat /surface/display' and expected is not None:
            match = re.search(r'^enabled=1 completed=([0-9]+) errors=0$', reply, re.M)
            if not match or not int(match[1]):
                return reply
            deadline = clock() + 3.0
            while True:
                observed = command('cat /surface/timing')
                result['timing_transcript'].append(observed)
                value = timing(observed)
                result['max_request_us'] = max(result['max_request_us'], value['max_request_us'])
                if value['max_request_us'] > result['request_limit_us']:
                    raise ValueError('Request-to-handoff exceeded 500 ms')
                if value['requested'] > previous and value['presented_request'] == value['requested'] and \
                        value['frame_app'] == expected and value['frame_count']:
                    if not (0 < value['request_us'] <= value['max_request_us'] and
                            0 < value['frame_us'] <= value['max_frame_us']):
                        raise ValueError('Inconsistent request timing')
                    result['request_samples'].append(value)
                    previous, expected = value['requested'], None
                    break
                if clock() >= deadline:
                    raise TimeoutError('Requested foreground did not reach LCD handoff')
                pause(0.01)
        return reply

    passed = _runtime.qualify(checked, result, cycles=cycles, pause=pause, clock=clock,
                              progress=progress, lcd=True, desktop=True)
    result['request_timing_passed'] = passed and len(result['request_samples']) == 4 + 7 * cycles
    result['passed'] = passed and result['request_timing_passed']
    result['qualification_complete'] &= result['request_timing_passed']
    return result['passed']


class ApplicationQualification(_runtime.ApplicationQualification):
    """Check core lifecycle, first matching frames, allocation release and request timing."""

    project = b'tikusurface_s31_lcd'

    def qualify(self, command, result):
        """Run 100 cycles; physical touch and optical latency remain outside this provider."""
        return qualify(command, result, progress=self.report_progress)
