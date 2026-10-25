#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Assign supported Calligra-XR formats and retire legacy Office preferences in Dome."""
import argparse
import datetime
import os
from pathlib import Path
import shutil
import tempfile

DEFAULTS = {
    'application/vnd.oasis.opendocument.text': 'org.domed.Calligra.Words.desktop',
    'application/vnd.oasis.opendocument.spreadsheet': 'org.domed.Calligra.Sheets.desktop',
    'application/vnd.oasis.opendocument.presentation': 'org.domed.Calligra.Stage.desktop',
    'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet': 'org.domed.Calligra.Sheets.desktop',
}
# Remove preferences, not shared-mime-info definitions. Binary XLS remains
# unsupported; modern XLSX is import-only and saves to ODS.
LEGACY = {
    'application/msword', 'application/x-msword', 'application/vnd.msword',
    'application/msword-template', 'application/vnd.ms-excel',
    'application/msexcel', 'application/x-msexcel', 'application/x-ms-excel',
    'application/vnd.ms-powerpoint', 'application/mspowerpoint',
    'application/x-mspowerpoint', 'application/vnd.ms-works',
    'application/x-mswrite',
}


def merge(text):
    lines = text.splitlines(keepends=True)
    result = []
    section = None
    found = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith('[') and stripped.endswith(']'):
            section = stripped
            result.append(line)
            if section == '[Default Applications]' and not found:
                if not line.endswith('\n'):
                    result.append('\n')
                result.extend(f'{mime}={app};\n' for mime, app in DEFAULTS.items())
                found = True
            continue
        key = stripped.split('=', 1)[0].strip() if '=' in stripped else None
        if section in ('[Default Applications]', '[Added Associations]', '[Removed Associations]'):
            if key in LEGACY or key in DEFAULTS:
                continue
        result.append(line)
    if not found:
        if result and not result[-1].endswith('\n'):
            result.append('\n')
        result.append('[Default Applications]\n')
        result.extend(f'{mime}={app};\n' for mime, app in DEFAULTS.items())
    return ''.join(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--system', action='store_true', help='Update system Dome defaults (requires sudo)')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    if args.system and not args.dry_run and os.geteuid() != 0:
        parser.error('Use sudo -A for --system')
    for desktop in DEFAULTS.values():
        path = Path('/usr/local/share/applications') / desktop
        if not path.is_file():
            parser.error('Install Calligra-XR first: ' + str(path))
    paths = [Path('/etc/xdg/dome-mimeapps.list'),
             Path('/usr/local/share/applications/dome-mimeapps.list')] if args.system else [
                 Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config')) / 'dome-mimeapps.list']
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    for path in paths:
        before = path.read_text() if path.exists() else ''
        after = merge(before)
        if args.dry_run:
            print(str(path) + ':\n' + after)
            continue
        if before == after:
            print('Already configured:', path)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.exists():
            backup = path.with_name(path.name + '.before-calligra-' + stamp)
            shutil.copy2(path, backup)
            print('Backup:', backup)
        fd, temporary = tempfile.mkstemp(prefix='.' + path.name + '-', dir=path.parent)
        try:
            with os.fdopen(fd, 'w') as output:
                output.write(after)
                output.flush()
                os.fsync(output.fileno())
            os.chmod(temporary, 0o644 if args.system else 0o600)
            os.replace(temporary, path)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)
        print('Updated:', path)


if __name__ == '__main__':
    main()
