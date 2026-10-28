#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Stage the optimized three-application FLTK port without writing system files."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PREFIX = Path('opt/calligra-fltk')
OFFICE_MIME = {'words': 'wordprocessingml.document', 'sheets': 'spreadsheetml.sheet',
               'stage': 'presentationml.presentation'}
APPS = {'words': ('Words', 'Word processor', 'WordProcessor', 'text'),
        'sheets': ('Sheets', 'Spreadsheet', 'Spreadsheet', 'spreadsheet'),
        'stage': ('Stage', 'Presentation editor', 'Presentation', 'presentation')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--build', type=Path, default=ROOT/'build-native')
    args = parser.parse_args(); stage = args.stage.resolve()
    if stage.exists(): parser.error('Choose a new staging directory')
    build = args.build.resolve()
    cache = (build / 'CMakeCache.txt').read_text()
    if 'CMAKE_BUILD_TYPE:STRING=Release' not in cache: raise SystemExit('Use a Release build')
    stage.mkdir(parents=True)
    subprocess.run(['cmake', '--install', str(build)],
                   env={**os.environ, 'DESTDIR': str(stage)}, check=True)
    for app, (name, generic, category, mime) in APPS.items():
        wrapper = stage / ('usr/local/bin/calligra-' + app + '-fltk')
        wrapper.parent.mkdir(parents=True, exist_ok=True)
        wrapper.write_text('#!/bin/sh\nset -eu\n'
            # Keep the installed system FLTK when the desktop exports its optional SDK.
            'fltk_native_path=\"\"\nfltk_saved_ifs=$IFS\nIFS=:\n'
            'for fltk_dir in ${LD_LIBRARY_PATH-}; do\n'
            '  case \"$fltk_dir\" in /opt/fltk-vr/toolkit/lib*|\"$HOME/.local/lib/domed/fltk\"*) ;;\n'
            '    *) fltk_native_path=${fltk_native_path:+$fltk_native_path:}$fltk_dir ;; esac\n'
            'done\nIFS=$fltk_saved_ifs\n'
            'if [ -n \"$fltk_native_path\" ]; then export LD_LIBRARY_PATH=\"$fltk_native_path\"; else unset LD_LIBRARY_PATH; fi\n'
            'export FLTK_BACKEND="${FLTK_BACKEND:-wayland}"\n'
            'export XDG_DATA_DIRS="/opt/calligra-fltk/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"\n'
            'exec /opt/calligra-fltk/bin/calligra-' + app + '-fltk "$@"\n')
        wrapper.chmod(0o755)
        entry = stage / ('usr/local/share/applications/org.domed.Calligra.' + name + '.desktop')
        entry.parent.mkdir(parents=True, exist_ok=True)
        entry.write_text('[Desktop Entry]\nType=Application\nName=Calligra ' + name + ' (FLTK)\n'
            'GenericName=' + generic + '\nComment=Native DomeD FLTK port — basic OpenDocument editing'
            + ' and ' + {'words': 'DOCX text/table', 'sheets': 'XLSX cell-data', 'stage': 'PPTX text/image'}[app] + ' import' + '\n'
            'Exec=/usr/local/bin/dome-link --app-id calligra-' + app + '-fltk -- /usr/local/bin/calligra-' + app + '-fltk %f\n'
            'TryExec=/usr/local/bin/calligra-' + app + '-fltk\n'
            'Icon=application-vnd.oasis.opendocument.' + mime + '\nTerminal=false\n'
            'Categories=Office;' + category + ';\nMimeType=application/vnd.oasis.opendocument.' + mime + ';'
            + 'application/vnd.openxmlformats-officedocument.' + OFFICE_MIME[app] + ';' + '\n'
            'StartupWMClass=calligra-' + app + '-fltk\n')
        subprocess.run(['desktop-file-validate', str(entry)], check=True)
        executable = stage / PREFIX / 'bin' / ('calligra-' + app + '-fltk')
        # Check installed paths using only the stage's own libraries and system backends.
        env = {**os.environ, 'LD_LIBRARY_PATH': str(stage / PREFIX / 'lib64') + ':' +
               str(stage / PREFIX / 'lib')}
        links = subprocess.check_output(['ldd', str(executable)], env=env, text=True)
        if 'not found' in links or 'libQt' in links or 'libgtk-' in links:
            raise SystemExit('Unexpected runtime linkage:\n' + links)
        subprocess.run([str(executable), '--help'], env=env, check=True)
    manifest = []
    for path in sorted(stage.rglob('*')):
        if path.is_symlink():
            if path.readlink().is_absolute() or not path.resolve().is_relative_to(stage):
                raise SystemExit('External staged symlink: ' + str(path))
            manifest.append({'path': str(path.relative_to(stage)), 'link': str(path.readlink())})
        elif path.is_file():
            path.chmod(0o755 if os.access(path, os.X_OK) else 0o644)
            manifest.append({'path': str(path.relative_to(stage)), 'mode': path.stat().st_mode & 0o777,
                             'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
        elif path.is_dir(): path.chmod(0o755)
    (stage / 'install-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Staged and verified', len(manifest), 'artifacts at', stage)


if __name__ == '__main__': main()
