#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare native glyph IDs, source clusters and positions to the hb-shape CLI."""
import argparse
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--test-bin-dir', type=Path, default=root/'build-native/fltk')
args = parser.parse_args()
cases = [('Noto Sans', 'office'), ('Noto Serif', 'AVATAR'), ('Noto Sans Arabic', 'سلام عليكم'),
         ('Noto Sans Devanagari', 'क्षि नमस्ते'), ('Noto Sans Hebrew', 'שָׁלוֹם'), ('Noto Sans', 'á Česky')]
for family, text in cases:
    actual = json.loads(subprocess.check_output([str(args.test_bin_dir/'calligra-native-text-shaper-test'), '--dump', family, text]))
    glyphs = actual['glyphs']; paths = {g['file'] for g in glyphs}
    assert len(paths) == 1, paths
    file = paths.pop()
    expected = json.loads(subprocess.check_output(['hb-shape', file, text, '--no-glyph-names', '--font-funcs=ft',
        '--font-size=4096', '--font-ppem=64', '--ft-load-flags=10', '--cluster-level=0',
        '--direction='+('rtl' if actual['rtl'] else 'ltr'), '--output-format=json', '--face-index='+str(glyphs[0]['face_index'])]))
    assert len(glyphs) == len(expected), (family, actual, expected)
    pen = 0
    for a, e in zip(glyphs, expected):
        assert (a['id'], a['begin']) == (e['g'], e['cl']), (family, a, e)
        assert abs(a['advance']*4096/12-e['ax']) < .01, (family, a, e)
        assert abs(a['x']*4096/12-(pen+e['dx'])) < .01, (family, a, e, pen)
        assert abs(-a['y']*4096/12-e['dy']) < .01, (family, a, e)
        pen += e['ax']
    print('PASS independent hb-shape:', family, repr(text), len(glyphs), 'glyphs', flush=True)
