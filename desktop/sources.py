#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Print the Stomme sources the TikuSurface desktop compiles for TikuOS.

The shell sources are the shell package's manifest less the parts TikuOS
has no use for; the kit sources are every kit source less the files written
for another host.  Each exclusion carries its reason.
"""
import argparse
import json
from pathlib import Path

SHELL_EXCLUDED = {
    'system/runner/tiku_run.c': 'application process host',
    'system/supervisor/tiku_desk.c': 'process supervisor',
    'system/shared/shell/script/tiku_trk_script.c': 'test interpreter; the stub links',
    'system/shared/shell/script/tiku_trk_script_expect.c': 'test interpreter',
    'system/shared/shell/script/tiku_trk_script_gesture.c': 'test interpreter',
    'system/shared/shell/adapters/tiku_trk_script_adapter.c': 'test interpreter',
    'system/shared/ui/tiku_trk_navigator.c': 'not linked by the desktop',
    'system/desktop/tiku_desktop_lifecycle_vfs.c': 'process lifecycle service; desktop/absent.c',
    'system/desktop/tiku_trk_desktop_grab.c': 'host image files; desktop/absent.c',
}
SHELL_EXCLUDED_DIRS = ('system/tracker/',)   # the single-window test Tracker

KIT_EXCLUDED = {
    'application/tiku_app_load.c': 'loadable modules',
    'application/tiku_client.c': 'process sessions',
    'application/tiku_conduct.c': 'development conductor; desktop/absent.c',
    'application/tiku_remote.c': 'process sessions; desktop/absent.c',
    'application/tiku_resource_channel.c': 'process resource protocol; desktop/absent.c',
    'application/tiku_resource_endpoint.c': 'process resource protocol; desktop/absent.c',
    'device/tiku_channel.c': 'board link over a host port',
    'device/tiku_link_tcp.c': 'board link over TCP',
    'device/tiku_tx.c': 'board link transmitter',
    'interface/tiku_x11.c': 'X11 host; desktop/host.c',
    'interface/tiku_x11_clipboard.c': 'X11 host',
    'interface/tiku_print_linux.c': 'Linux printing',
    'media/tiku_sound_pulse.c': 'PulseAudio; desktop/absent.c',
    'support/tiku_report.c': 'crash reports by signal; desktop/absent.c',
    'support/tiku_sys_linux.c': 'Linux system layer; desktop/sys.c',
    'support/tiku_watch.c': 'stall watchdog by signal; desktop/absent.c',
}
KIT_EXCLUDED_MARKS = ('_linux', '_darwin', '_cocoa', '_coreaudio', '_x11')
KITS = ('support', 'device', 'storage', 'interface', 'translation',
        'application', 'media')


def shell_sources(root):
    manifest = json.loads((root / 'packages/shell/package.json').read_text())
    return [s for s in manifest['sources'] if s.endswith('.c') and
            s not in SHELL_EXCLUDED and not s.startswith(SHELL_EXCLUDED_DIRS)]


def kit_sources(root):
    out = []
    for kit in KITS:
        for path in sorted((root / 'kits' / kit).glob('*.c')):
            rel = f'{kit}/{path.name}'
            if rel in KIT_EXCLUDED or any(m in path.name for m in KIT_EXCLUDED_MARKS):
                continue
            out.append(rel)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--foundation', required=True, type=Path)
    parser.add_argument('--kind', choices=('shell', 'kits'), required=True)
    args = parser.parse_args()
    names = shell_sources(args.foundation) if args.kind == 'shell' else kit_sources(args.foundation)
    print('\n'.join(names))


if __name__ == '__main__':
    main()
