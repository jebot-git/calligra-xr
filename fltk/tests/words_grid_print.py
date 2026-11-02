#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify native Words grid snapshots through ODF export and real private CUPS.

Uses native geometry and rendered pixels with the current system font. Bitmap
mask output deliberately does not require searchable PDF text.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import zipfile

from PIL import Image
from pypdf import PdfReader
from cups_runtime import PrivateCups


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def render(pdf, prefix):
    subprocess.run(['pdftoppm', '-r', '100', '-png', str(pdf), str(prefix)],
                   check=True, capture_output=True)
    return sorted(prefix.parent.glob(prefix.name+'-*.png'))


def color_count(image, color):
    counts = image.convert('RGB').getcolors(image.width*image.height)
    return sum(count for count, pixel in counts if pixel == color)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin-dir', type=Path, required=True)
    parser.add_argument('--test-bin-dir', type=Path)
    parser.add_argument('--grid-artifacts', type=Path)
    parser.add_argument('--repeat-artifacts', type=Path)
    parser.add_argument('--core-artifacts', type=Path)
    parser.add_argument('--edit-artifacts', type=Path, help='Preexisting repeated-cell editing UI snapshots')
    parser.add_argument('--edit-only', action='store_true', help='Run only repeated text-edit snapshots and optional nested core fixtures')
    parser.add_argument('--format-artifacts', type=Path, help='Repeated-cell formatting UI snapshots')
    parser.add_argument('--format-only', action='store_true', help='Run repeated formatting UI/core snapshots')
    parser.add_argument('--list-artifacts', type=Path, help='Repeated-cell list menu and keyboard UI snapshots')
    parser.add_argument('--list-only', action='store_true', help='Run repeated-cell list UI/core snapshots')
    parser.add_argument('--merge-artifacts', type=Path, help='Words merge/split UI snapshots')
    parser.add_argument('--merge-only', action='store_true', help='Run merge/split UI and optional core snapshots')
    parser.add_argument('--subdivide-artifacts', type=Path, help='Words cell subdivision UI snapshots')
    parser.add_argument('--subdivide-only', action='store_true', help='Run subdivision UI and optional core snapshots')
    parser.add_argument('--geometry-artifacts', type=Path, help='Equal measured subdivision UI snapshots')
    parser.add_argument('--geometry-only', action='store_true', help='Run measured geometry UI and optional core snapshots')
    parser.add_argument('--artifacts', type=Path, required=True)
    args = parser.parse_args()
    if args.geometry_only and (args.edit_only or args.format_only or args.list_only or args.merge_only or args.subdivide_only or not args.geometry_artifacts):
        parser.error('--geometry-only requires --geometry-artifacts and excludes other focused modes')
    if args.subdivide_only and (args.edit_only or args.format_only or args.list_only or args.merge_only or not args.subdivide_artifacts):
        parser.error('--subdivide-only requires --subdivide-artifacts and excludes other focused modes')
    if args.merge_only and (args.edit_only or args.format_only or args.list_only or not args.merge_artifacts):
        parser.error('--merge-only requires --merge-artifacts and excludes other focused modes')
    if args.list_only and (args.edit_only or args.format_only or not args.list_artifacts):
        parser.error('--list-only requires --list-artifacts and excludes edit/format modes')
    if args.format_only and (args.edit_only or not args.format_artifacts):
        parser.error('--format-only requires --format-artifacts and excludes --edit-only')
    if args.edit_only and not args.edit_artifacts:
        parser.error('--edit-only requires --edit-artifacts')
    if not (args.edit_only or args.format_only or args.list_only or args.merge_only or args.subdivide_only or args.geometry_only) and not all((args.grid_artifacts,args.repeat_artifacts,args.core_artifacts)):
        parser.error('Grid mode requires --grid-artifacts, --repeat-artifacts and --core-artifacts')
    art = args.artifacts.resolve()
    art.mkdir(parents=True, exist_ok=False)
    cases = []
    if args.geometry_only:
        cases=[('geometry-'+name,args.geometry_artifacts/('geometry-'+name+'.odt')) for name in ('equal','typed')]
        if args.core_artifacts:
            cases += [('core-'+name,args.core_artifacts/(name+'.odt')) for name in (
                'subdivided','equal-columns','equal-auto-rows','nested-equal','equal-rows','rtl-subdivided','redone','zero-equal','covered-zero-equal','micro-equal')]
    elif args.subdivide_only:
        cases=[('subdivide-'+name,args.subdivide_artifacts/('subdivide-'+name+'.odt')) for name in ('parts','typed')]
        if args.core_artifacts:
            cases += [('core-'+name,args.core_artifacts/(name+'.odt')) for name in (
                'subdivided','relative-subdivided','merged-subdivided','repeated-subdivided',
                'nested-subdivided','numeric-subdivided','typed-subdivided')]
    elif args.merge_only:
        cases=[('merge-'+name,args.merge_artifacts/('merge-'+name+'.odt')) for name in ('merged','split','typed')]
        if args.core_artifacts:
            cases += [('core-'+name,args.core_artifacts/(name+'.odt')) for name in (
                'merged','split','hidden-split','styled-merged','styled-split',
                'repeated-merged','nested-merged','nested-split','typed-merged',
                'common-styled-merged','common-styled-split')]
    elif args.list_only:
        cases=[('list-'+name,args.list_artifacts/('list-'+name+'.odt')) for name in (
            'menu-indent','menu-outdent','key-indent','key-outdent','empty-exit')]
        if args.core_artifacts:
            cases += [('core-'+name,args.core_artifacts/(name+'.odt')) for name in (
                'flat-indent','flat-outdent','flat-exit','flat-typed-blue',
                'nested-indent','nested-outdent','nested-exit','nested-typed-blue')]
    elif args.format_only:
        cases=[('format-'+name,args.format_artifacts/('repeat-format-'+name+'.odt')) for name in (
            'color','centered','tabs','defaults','definition','named-character','named-paragraph')]
        if args.core_artifacts:
            cases += [('core-'+name,args.core_artifacts/(name+'.odt')) for name in (
                'range-blue','paragraph-centered','empty-blue','tabs-local','tabs-global-default',
                'named-character','named-paragraph','named-global-definition','nested-blue')]
    elif not args.edit_only:
        cases = [(name, args.grid_artifacts/('grid-'+name+'.odt'))
                 for name in ('expanded', 'row-deleted', 'column-deleted')]
        cases += [('repeat-'+name, args.repeat_artifacts/('grid-repeat-'+name+'.odt'))
              for name in ('row-added', 'column-typed')]
        cases += [(name, args.core_artifacts/(file+'.odt')) for name, file in (
        ('style-row', 'promoted-row-style-retained'),
        ('style-neutral', 'promoted-neutral-style-retained'),
        ('style-column', 'deleted-anchor-column-style-retained'),
        ('hidden-horizontal', 'horizontal-covered-retained'))]
    elif args.core_artifacts:
        cases += [('nested-'+name,args.core_artifacts/(file+'.odt')) for name,file in (
            ('typed','typed'),('range','range-replaced'),('blue','typed-blue'))]
    if args.edit_artifacts:
        cases += [('edit-'+name,args.edit_artifacts/('repeated-'+name+'.odt')) for name in (
            'typed','direct-backspace','direct-delete','cut','menu-cut','pasted','paragraph','soft-break')]
        nested=args.edit_artifacts/'repeated-nested-typed.odt'
        if nested.is_file():cases.append(('edit-nested-typed',nested))
    binary = args.bin_dir.resolve()/'calligra-words-fltk'
    helper = (args.test_bin_dir or args.bin_dir).resolve()/'calligra-native-text-shaper-test'
    env = dict(os.environ, LD_LIBRARY_PATH='/usr/local/lib/domed/bitmap',
               FLTK_BACKEND='wayland', LC_ALL='C')
    env.pop('DISPLAY', None)
    env.pop('WAYLAND_DISPLAY', None)
    results = {}
    with PrivateCups(art/'cups') as cups:
        env['CUPS_SERVER'] = cups.server
        known_jobs = set()
        for number, (name, source) in enumerate(cases, 1):
            source = source.resolve()
            before = digest(source)
            output, exported = art/(name+'.pdf'), art/(name+'.odt')
            run = subprocess.run([str(binary), str(source), '--export-odf', str(exported),
                                  '--print-pdf', str(output), '--print-to', cups.name],
                                 env=env, capture_output=True, text=True)
            (art/(name+'.log')).write_text(run.stdout+run.stderr)
            run.check_returncode()
            assert all(int(n) == 0 for n in re.findall(r'(\d+) missing glyphs', run.stderr)), run.stderr
            assert digest(source) == before, (name, 'source changed')
            with zipfile.ZipFile(source) as old, zipfile.ZipFile(exported) as new:
                for entry in old.namelist():
                    if entry != 'META-INF/manifest.xml':
                        assert old.read(entry) == new.read(entry), (name, entry)
                assert ('styles.xml' in old.namelist()) == ('styles.xml' in new.namelist())
            layout = json.loads(subprocess.check_output(
                [str(helper), '--layout', str(source)], env=env, text=True))
            (art/(name+'-layout.json')).write_text(json.dumps(layout)+'\n')
            assert len(PdfReader(output).pages) == len(layout) > 0
            pages = render(output, art/(name+'-page'))
            for page in pages:
                picture = Image.open(page).convert('L')
                assert sum(picture.histogram()[:80]) > 50, (name, 'blank page')
            if name in ('nested-blue','core-nested-blue','core-range-blue','core-named-character',
                         'format-color','format-named-character','format-named-paragraph',
                         'core-flat-typed-blue','core-nested-typed-blue','core-styled-merged','core-styled-split','core-common-styled-merged','core-common-styled-split'):
                assert sum(color_count(Image.open(page), (22, 90, 167)) for page in pages) > 5
            if name in ('style-row', 'style-column'):
                picture = Image.open(pages[0]).convert('RGB')
                assert color_count(picture, (255, 0, 0)) > 20, name
                if name == 'style-column':
                    assert color_count(picture, (0, 255, 0)) == 0, name
            if name == 'style-neutral':
                # Paragraph 0 is Before; paragraph 1 is the promoted anchor.
                line = next(line for page in layout for line in page[2] if line[0] == 1)
                x, y, width, height = line[3:7]
                scale = 100/72
                box = (int(x*scale), int(y*scale), int((x+width)*scale)+1,
                       int((y+height)*scale)+1)
                crop = Image.open(pages[0]).crop(box)
                assert color_count(crop, (0, 0, 0)) > 10
                assert color_count(crop, (0, 255, 0)) == 0
            reopened = art/(name+'-reopened.pdf')
            subprocess.run([str(binary), str(exported), '--print-pdf', str(reopened)],
                           env=env, check=True, capture_output=True)
            again = render(reopened, art/(name+'-reopened-page'))
            assert [digest(p) for p in pages] == [digest(p) for p in again], name
            jobs = cups.completed(number)
            assert len(jobs) == number
            new_jobs = set(jobs)-known_jobs
            assert len(new_jobs) == 1
            captured = next(iter(new_jobs))
            known_jobs.update(jobs)
            assert digest(captured) == digest(output), name
            printed = render(captured, art/(name+'-cups-page'))
            assert [digest(p) for p in pages] == [digest(p) for p in printed], name
            results[name] = dict(source_unchanged=True, package_entries_preserved=True,
                                 pages=len(pages), native_geometry_pages=True,
                                 reopen_pixels_identical=True, cups_pixels_identical=True)
            print('PASS', name, 'ODF retention, native print, reopen and CUPS pixels', flush=True)
    results['completed_cups_jobs'] = len(cases)
    results['system_printers_changed'] = False
    (art/'results.json').write_text(json.dumps(results, indent=2)+'\n')


if __name__ == '__main__':
    main()
