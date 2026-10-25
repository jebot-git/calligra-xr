#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Install a reviewed, verified FLTK port stage. Never changes defaults or sessions."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def allowed(path):
    if path.is_absolute() or '..' in path.parts: return False
    return path.parts[:2] == ('opt', 'calligra-fltk') or str(path) in {
        'usr/local/bin/calligra-' + app + '-fltk' for app in ('words', 'sheets', 'stage')} or str(path) in {
        'usr/local/share/applications/org.domed.Calligra.' + app + '.desktop' for app in ('Words', 'Sheets', 'Stage')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', type=Path)
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args(); stage = args.stage.resolve()
    manifest = json.loads((stage / 'install-manifest.json').read_text())
    seen = set()
    for item in manifest:
        relative = Path(item['path']); source = stage / relative
        if not allowed(relative) or str(relative) in seen: raise SystemExit('Invalid install path')
        seen.add(str(relative))
        if 'link' in item:
            if not source.is_symlink() or str(source.readlink()) != item['link'] or not source.resolve().is_relative_to(stage):
                raise SystemExit('Invalid staged link')
        elif source.is_symlink() or hashlib.sha256(source.read_bytes()).hexdigest() != item['sha256'] or item['mode'] not in (0o644, 0o755):
            raise SystemExit('Invalid staged file: ' + str(relative))
    for app in ('words', 'sheets', 'stage'):
        if 'opt/calligra-fltk/bin/calligra-' + app + '-fltk' not in seen:
            raise SystemExit('All three applications are required')
    if args.verify_only: print('Verified', len(manifest), 'staged artifacts'); return
    if os.geteuid() != 0: raise SystemExit('Use sudo -A only for this prepared installation')
    backup = Path('/var/backups/calligra-fltk') / datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    for item in manifest:
        target = Path('/') / item['path']
        if target.exists() or target.is_symlink():
            saved = backup / item['path']; saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, saved, follow_symlinks=False)
    for item in manifest:
        source = stage / item['path']; target = Path('/') / item['path']
        target.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
        fd, temporary = tempfile.mkstemp(prefix='.' + target.name + '.install-', dir=target.parent)
        os.close(fd)
        try:
            if 'link' in item:
                os.unlink(temporary); os.symlink(item['link'], temporary)
            else:
                shutil.copy2(source, temporary); os.chmod(temporary, item['mode'])
            os.chown(temporary, 0, 0, follow_symlinks=False); os.replace(temporary, target)
            if 'sha256' in item and hashlib.sha256(target.read_bytes()).hexdigest() != item['sha256']:
                raise SystemExit('Installed checksum mismatch: ' + str(target))
        finally:
            if os.path.lexists(temporary): os.unlink(temporary)
    receipt = Path('/opt/calligra-fltk/install-manifest.json')
    shutil.copy2(stage / 'install-manifest.json', receipt); receipt.chmod(0o644); os.chown(receipt, 0, 0)
    if shutil.which('restorecon'):
        subprocess.run(['restorecon', '-R', '/opt/calligra-fltk', *['/usr/local/bin/calligra-' + a + '-fltk' for a in ('words','sheets','stage')]], check=True)
    subprocess.run(['update-desktop-database', '/usr/local/share/applications'], check=True)
    print('Installed Words, Sheets and Stage FLTK port. Backup directory:', backup)
    print('No MIME defaults or running sessions were changed.')


if __name__ == '__main__': main()
