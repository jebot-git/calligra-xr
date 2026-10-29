#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Print/export the actual Words formatting UI snapshots and the suite baseline.

uv run --no-project --with odfpy --with Pillow --with pypdf python \
  fltk/tests/words_format_print.py --ui-artifacts /path/to/words-ui \
  --artifacts /new/print-results [--bin-dir /usr/local/bin]
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import zipfile

from PIL import Image
from pypdf import PdfReader

from cups_runtime import PrivateCups
from print_producer import ROOT, verify


def render(pdf, prefix):
    subprocess.run(['pdftoppm', '-r', '100', '-png', str(pdf), str(prefix)],
                   check=True, capture_output=True)
    return sorted(prefix.parent.glob(prefix.name + '-*.png'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ui-artifacts', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--bin-dir', type=Path, default=ROOT / 'build-native/fltk')
    args = parser.parse_args()
    art = args.artifacts.resolve()
    art.mkdir(parents=True, exist_ok=False)
    results = {}
    with PrivateCups(art / 'cups') as cups:
        results['baseline'] = verify(args.bin_dir, art, cups)
        completed_jobs = 3
        env = {**os.environ, 'LD_LIBRARY_PATH': '/usr/local/lib/domed/bitmap',
               'FLTK_BACKEND': 'wayland', 'CUPS_SERVER': cups.server, 'LC_ALL': 'C'}
        env.pop('DISPLAY', None)
        env.pop('WAYLAND_DISPLAY', None)
        binary = args.bin_dir.resolve() / 'calligra-words-fltk'
        for name in ('font', 'paragraph', 'typed'):
            source = args.ui_artifacts.resolve() / ('format-' + name + '.odt')
            before = hashlib.sha256(source.read_bytes()).hexdigest()
            known = set(cups.completed(completed_jobs))
            output = art / ('formatted-' + name + '.pdf')
            exported = art / ('formatted-' + name + '.odt')
            run = subprocess.run([str(binary), str(source), '--export-odf', str(exported),
                                  '--print-pdf', str(output), '--print-to', cups.name],
                                 env=env, text=True, capture_output=True)
            (art / (name + '-print.log')).write_text(run.stdout + run.stderr)
            run.check_returncode()
            assert all(int(n) == 0 for n in re.findall(r'(\d+) missing glyphs', run.stderr)), run.stderr
            assert hashlib.sha256(source.read_bytes()).hexdigest() == before
            with zipfile.ZipFile(source) as old, zipfile.ZipFile(exported) as new:
                for entry in ('styles.xml', 'Extras/opaque.bin'):
                    assert old.read(entry) == new.read(entry), (name, entry)
            reader = PdfReader(output)
            assert len(reader.pages) == 1
            assert 'Český' in (reader.pages[0].extract_text() or '')
            pages = render(output, art / ('formatted-' + name + '-page'))
            assert len(pages) == 1
            if name == 'font':
                faces = subprocess.check_output(['pdffonts', str(output)], text=True)
                (art / 'formatted-font-faces.txt').write_text(faces)
                assert 'Hack-Italic' in faces, faces
                picture = Image.open(pages[0]).convert('RGB')
                red = [(x, y) for y in range(picture.height)
                       for x in range(picture.width)
                       if all(abs(picture.getpixel((x, y))[i] - v) < 8
                              for i, v in enumerate((176, 32, 64)))]
                assert len(red) > 100
                height = max(y for _, y in red) - min(y for _, y in red) + 1
                assert 20 <= height < 80, height
            if name == 'paragraph':
                bbox = art / 'paragraph-bounds.html'
                subprocess.run(['pdftotext', '-bbox', str(output), str(bbox)], check=True)
                xml = ET.fromstring(bbox.read_text())
                ns = '{http://www.w3.org/1999/xhtml}'
                page = xml.find('.//' + ns + 'page')
                section = next(w for w in page.iter(ns + 'word') if w.text == 'Section')
                alpha = next(w for w in page.iter(ns + 'word') if w.text == 'alpha')
                midpoint = (float(section.get('xMin')) + float(alpha.get('xMax'))) / 2
                assert abs(midpoint - (float(page.get('width')) / 2 + 3)) < 3, midpoint
            reopened = art / ('formatted-' + name + '-reopened.pdf')
            subprocess.run([str(binary), str(exported), '--print-pdf', str(reopened)],
                           env=env, capture_output=True, check=True)
            again = render(reopened, art / ('reopened-' + name + '-page'))
            assert [p.read_bytes() for p in pages] == [p.read_bytes() for p in again]
            completed_jobs += 1
            all_jobs = set(cups.completed(completed_jobs))
            new_jobs = all_jobs - known
            assert len(new_jobs) == 1
            captured = next(iter(new_jobs))
            printed = render(captured, art / ('cups-' + name + '-page'))
            assert [p.read_bytes() for p in pages] == [p.read_bytes() for p in printed]
            results[name] = {'source_unchanged': True, 'pages': 1,
                             'exported_odf': str(exported), 'pdf': str(output),
                             'reopened_pixels_identical': True,
                             'cups_pixels_identical': True, 'captured': str(captured)}
            print('PASS formatted', name, 'ODF, native PDF, reopen and CUPS raster', flush=True)
        assert len(cups.completed(6)) == 6
    results['completed_cups_jobs'] = 6
    results['system_printers_changed'] = False
    (art / 'format-results.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
