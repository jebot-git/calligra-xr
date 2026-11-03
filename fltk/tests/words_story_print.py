#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Print scoped Words stories, verify ODF reopen pixels and private CUPS jobs.

Run with uv --with Pillow --with pypdf. Inputs are produced by the native
page-stories test and the independent --stories-only GUI producer.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile

from PIL import Image, ImageDraw
from pypdf import PdfReader
from cups_runtime import PrivateCups

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bin-dir', type=Path, required=True)
parser.add_argument('--core-artifacts', type=Path, required=True)
parser.add_argument('--ui-artifacts', type=Path, required=True)
parser.add_argument('--artifacts', type=Path, required=True)
args = parser.parse_args()
out = args.artifacts.resolve()
out.mkdir(parents=True, exist_ok=True)
binary = args.bin_dir.resolve() / 'calligra-words-fltk'
inputs = sorted(args.core_artifacts.resolve().glob('*.odt'))
for name in ('story-original', 'story-footer-saved', 'story-region-redone'):
    inputs.append(args.ui_artifacts.resolve() / (name + '.odt'))
assert inputs and all(p.is_file() for p in inputs)
env = {**os.environ, 'LD_LIBRARY_PATH': '/usr/local/lib/domed/bitmap',
       'FLTK_BACKEND': 'wayland', 'LC_ALL': 'C'}
for name in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET'):
    env.pop(name, None)
records, pages, printed_hashes = [], [], set()
with PrivateCups(out / 'cups') as cups:
    env['CUPS_SERVER'] = cups.server
    for source in inputs:
        stem = source.stem
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        pdf = out / (stem + '.pdf')
        exported = out / ('exported-' + stem + '.odt')
        run = subprocess.run([str(binary), str(source), '--export-odf', str(exported),
                              '--print-pdf', str(pdf), '--print-to', cups.name],
                             env=env, capture_output=True, text=True, timeout=60)
        (out / (stem + '.log')).write_text(run.stdout + run.stderr)
        run.check_returncode()
        assert 'missing glyphs' not in run.stderr, (stem, run.stderr)
        assert 'unsupported' not in run.stderr.lower(), (stem, run.stderr)
        assert hashlib.sha256(source.read_bytes()).hexdigest() == before
        with zipfile.ZipFile(source) as original, zipfile.ZipFile(exported) as saved:
            assert original.read('styles.xml') == saved.read('styles.xml'), stem
            if 'Extras/opaque.bin' in original.namelist():
                assert original.read('Extras/opaque.bin') == saved.read('Extras/opaque.bin'), stem
        reopened = out / (stem + '-reopened.pdf')
        again = subprocess.run([str(binary), str(exported), '--print-pdf', str(reopened)],
                               env=env, capture_output=True, text=True, timeout=60)
        again.check_returncode()
        count = len(PdfReader(pdf).pages)
        assert count and len(PdfReader(reopened).pages) == count
        for path, prefix in ((pdf, stem), (reopened, stem + '-reopened')):
            subprocess.run(['pdftoppm', '-r', '120', '-png', str(path), str(out / prefix)],
                           capture_output=True, check=True, timeout=60)
        for page in range(1, count + 1):
            png = out / (stem + f'-{page}.png')
            repeat = out / (stem + f'-reopened-{page}.png')
            first, second = Image.open(png).convert('RGB'), Image.open(repeat).convert('RGB')
            assert first.size == second.size and first.tobytes() == second.tobytes(), (stem, page)
            assert sum(max(pixel) < 100 for pixel in first.get_flattened_data()) > 15, (stem, page)
            pages.append(png)
        printed_hashes.add(hashlib.sha256(pdf.read_bytes()).hexdigest())
        records.append({'source': str(source), 'pages': count, 'source_unchanged': True,
                        'styles_and_archive_retained': True, 'odf_reopen_pixels_identical': True})
    jobs = cups.completed(len(inputs))
    assert len(jobs) == len(inputs)
    for job in jobs:
        assert hashlib.sha256(job.read_bytes()).hexdigest() in printed_hashes, job
        assert len(PdfReader(job).pages)

# Contact sheets make each new print page reviewable without changing outputs.
for batch in range(0, len(pages), 6):
    sheet = Image.new('RGB', (1000, 900), '#d8d8d8')
    draw = ImageDraw.Draw(sheet)
    for i, page in enumerate(pages[batch:batch + 6]):
        picture = Image.open(page).convert('RGB')
        picture.thumbnail((480, 266))
        x, y = (i % 2) * 500 + 10, (i // 2) * 300 + 25
        sheet.paste(picture, (x, y))
        draw.text((x, y - 20), page.name, fill='black')
    sheet.save(out / f'review-{batch // 6 + 1:02}.png')
(out / 'results.json').write_text(json.dumps({'documents': records,
    'pages': len(pages), 'cups_jobs': len(inputs), 'cups_matches_pdf_bytes': True,
    'review_sheets': (len(pages) + 5) // 6}, indent=2) + '\n')
print('PASS', len(inputs), 'Words story exports/CUPS jobs and', len(pages),
      'print pages with identical ODF reopen pixels', flush=True)
