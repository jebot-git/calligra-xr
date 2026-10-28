#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check native build prerequisites. Qt/KDE extraction is an explicit legacy option."""
import argparse,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--legacy-qt-backend',action='store_true');args,rest=p.parse_known_args()
if args.legacy_qt_backend:
 raise SystemExit(subprocess.call([sys.executable,str(Path(__file__).with_name('prepare-legacy-qt-deps.py')),*rest]))
if rest:p.error('Native build checks do not accept legacy backend arguments')
subprocess.run(['pkg-config','--atleast-version=2.17','fontconfig'],check=True)
subprocess.run(['pkg-config','--exists','fontconfig','freetype2','harfbuzz','zlib','libzip','libxml-2.0','glib-2.0','icu-uc','icu-i18n','gio-2.0','wayland-client','x11','glu'],check=True)
subprocess.run(['fltk-config','--version'],check=True)
print('Native dependencies available. No Qt/KDE development packages are required.')
