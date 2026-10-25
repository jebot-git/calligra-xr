#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare a signed Fedora development prefix without installing system files."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

PACKAGES = [('qt6-qtbase', 'x86_64'), ('qt6-qtbase-common', 'noarch'),
            ('qt6-qtbase-devel', 'x86_64'), ('qt6-qtbase-gui', 'x86_64'),
            ('qt6-qtdeclarative', 'x86_64'), ('qt6-qtdeclarative-devel', 'x86_64'),
            ('kf6-kcoreaddons', 'x86_64'), ('kf6-kcoreaddons-devel', 'x86_64'),
            ('kf6-kconfig', 'x86_64'), ('kf6-kconfig-devel', 'x86_64'),
            ('kf6-ki18n', 'x86_64'), ('kf6-ki18n-devel', 'x86_64'),
            ('extra-cmake-modules', 'noarch'), ('eigen3-devel', 'noarch')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--rpm-cache', type=Path, required=True)
    args = parser.parse_args()
    if os.geteuid() == 0: parser.error('Prepare dependencies as the regular user')
    stage = args.stage.resolve(); cache = args.rpm_cache.resolve()
    if stage.exists(): parser.error('Choose a new private prefix')
    cache.mkdir(parents=True, exist_ok=True)
    subprocess.run(['dnf', 'download', '--destdir', str(cache),
                    *[name + '.' + arch for name, arch in PACKAGES]], check=True)
    stage.mkdir(parents=True)
    receipts = []
    for name, arch in PACKAGES:
        matches = []
        for rpm in cache.glob(name + '-*.' + arch + '.rpm'):
            info = subprocess.check_output(['rpm', '-qp', '--qf', '%{NAME}', str(rpm)],
                                           text=True, stderr=subprocess.DEVNULL)
            if info == name: matches.append(rpm)
        if len(matches) != 1: raise SystemExit('Use a cache with exactly one version of ' + name)
        rpm = matches[0]
        subprocess.run(['rpmkeys', '--checksig', str(rpm)], check=True)
        process = subprocess.Popen(['rpm2cpio', str(rpm)], stdout=subprocess.PIPE)
        subprocess.run(['cpio', '-idm', '--quiet', '--no-absolute-filenames'],
                       cwd=stage, stdin=process.stdout, check=True)
        process.stdout.close()
        if process.wait(): raise SystemExit('RPM extraction failed')
        receipts.append({'file': rpm.name, 'sha256': hashlib.sha256(rpm.read_bytes()).hexdigest()})
    # Fedora framework export files contain absolute installation paths.
    for path in (stage / 'usr/lib64/cmake').glob('KF6*/*.cmake'):
        path.write_text(path.read_text().replace('/usr/', str(stage) + '/usr/'))
    (stage / 'development-packages.json').write_text(json.dumps(receipts, indent=2) + '\n')
    print('Prepared:', stage / 'usr')
    print('Use CALLIGRA_BACKEND_PREFIX=' + str(stage / 'usr') + ' ./fltk/scripts/build.sh')


if __name__ == '__main__': main()
