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

from odf.opendocument import OpenDocumentText
from odf.style import Style, TextProperties
from odf.text import P, Span, LineBreak
from PIL import Image
from pypdf import PdfReader

from cups_runtime import PrivateCups
from print_producer import ROOT, verify


def render(pdf, prefix):
    subprocess.run(['pdftoppm', '-r', '100', '-png', str(pdf), str(prefix)],
                   check=True, capture_output=True)
    return sorted(prefix.parent.glob(prefix.name + '-*.png'))


def soft_lines(path, paragraph_size):
    doc = OpenDocumentText()
    body = Style(name='Body', family='paragraph')
    body.addElement(TextProperties(fontfamily='Hack', fontsize=str(paragraph_size)+'pt'))
    doc.styles.addElement(body)
    small = Style(name='Small', family='text')
    small.addElement(TextProperties(fontsize='6pt'))
    doc.styles.addElement(small)
    paragraph = P(stylename=body)
    span = Span(stylename=small)
    span.addElement(LineBreak()); span.addElement(LineBreak()); span.addText('A')
    paragraph.addElement(span); doc.text.addElement(paragraph)
    doc.save(str(path))
    with zipfile.ZipFile(path, 'a') as archive:
        archive.writestr('Extras/opaque.bin', b'\0keep\xff')


def unicode_source(path):
    # Several of these scalars share a font glyph. PDF text must retain source
    # codepoints rather than reverse-map the glyph to its first cmap alias.
    doc = OpenDocumentText()
    body = Style(name='UnicodeBody', family='paragraph')
    body.addElement(TextProperties(fontfamily='DejaVu Sans', fontsize='18pt'))
    doc.styles.addElement(body)
    for line in ('Latin office affine', 'NFD a\u0301', 'NFC á',
                 'CJK 日本語', 'Radical ⽇本語'):
        doc.text.addElement(P(stylename=body, text=line))
    doc.save(str(path))
    with zipfile.ZipFile(path, 'a') as archive:
        archive.writestr('Extras/opaque.bin', b'\0keep\xff')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ui-artifacts', type=Path, required=True)
    parser.add_argument('--page-artifacts', type=Path,
                        help='Also print native page editing and optional IME snapshots')
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
        cases = [(name, args.ui_artifacts.resolve() / ('format-' + name + '.odt'))
                 for name in ('font', 'paragraph', 'typed', 'quoted-family', 'soft-lines')]
        if args.page_artifacts:
            page_source = args.page_artifacts.resolve() / 'page-edited-final.odt'
            assert page_source.is_file(), page_source
            cases.append(('page-edit', page_source))
            ime_source = args.page_artifacts.resolve() / 'page-ime-committed.odt'
            if ime_source.is_file():
                cases.append(('page-ime', ime_source))
            navigation = args.page_artifacts.resolve() / 'table-navigation-edited-final.odt'
            if navigation.is_file():
                cases.append(('table-navigation', navigation))
        cases.append(('unicode-source', art / 'unicode-source.odt'))
        for name, source in cases:
            if name == 'unicode-source':
                unicode_source(source)
            if name == 'soft-lines':
                source = art / 'soft-lines-source.odt'
                soft_lines(source, 24)
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
            native_page = name in ('page-edit', 'page-ime')
            assert len(reader.pages) == (5 if native_page else 3 if name == 'table-navigation' else 1)
            text = '\n'.join(page.extract_text() or '' for page in reader.pages)
            if name == 'table-navigation':
                for expected in ('BEFORE TABLE', 'xNEXT LEFT', 'RIGHT2', 'NESTED LEFT',
                                 'NESTED RIGHT2', 'MERGED1', 'AFTER MERGE', 'AFTER TABLE'):
                    assert expected in text, (expected, text)
            elif name == 'unicode-source':
                extracted = subprocess.check_output(['pdftotext', str(output), '-'], text=True)
                for expected in ('office affine', 'NFD a\u0301', 'NFC á', 'CJK 日本語', 'Radical ⽇本語'):
                    assert expected in extracted, (expected, repr(extracted))
                # Cairo emits ActualText when a glyph has a different/multiple
                # Unicode source mapping. pypdf's current extractor ignores it;
                # inspect the encoded values as well as Poppler's extraction.
                actual = [str(operands[1]['/ActualText'])
                          for operands, op in reader.pages[0].get_contents().operations
                          if op == b'BDC' and '/ActualText' in operands[1]]
                assert actual.count('ffi') == 2 and 'a\u0301' in actual and '⽇' in actual, actual
                (art / 'unicode-extracted.txt').write_text(extracted)
                (art / 'unicode-pypdf-extracted.txt').write_text(text)
                (art / 'unicode-actual-text.json').write_text(json.dumps(actual, ensure_ascii=False) + '\n')
            else:
                assert ('Česxký' if native_page else 'A' if name == 'soft-lines' else 'Český') in text
            if name == 'page-ime':
                assert '日本語' in text
            pages = render(output, art / ('formatted-' + name + '-page'))
            assert len(pages) == len(reader.pages)
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
            if name == 'quoted-family':
                faces = subprocess.check_output(['pdffonts', str(output)], text=True)
                assert 'Hack-Bold' in faces, faces
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
            if name == 'soft-lines':
                reference = art / 'soft-lines-reference.odt'
                soft_lines(reference, 6)
                reference_pdf = art / 'soft-lines-reference.pdf'
                subprocess.run([str(binary), str(reference), '--print-pdf', str(reference_pdf)],
                               env=env, capture_output=True, check=True)
                expected = render(reference_pdf, art / 'soft-lines-reference-page')
                assert [p.read_bytes() for p in pages] == [p.read_bytes() for p in expected]
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
            results[name] = {'source_unchanged': True, 'pages': len(reader.pages),
                             'exported_odf': str(exported), 'pdf': str(output),
                             'reopened_pixels_identical': True,
                             'cups_pixels_identical': True, 'captured': str(captured)}
            print('PASS formatted', name, 'ODF, native PDF, reopen and CUPS raster', flush=True)
        assert len(cups.completed(completed_jobs)) == completed_jobs
    results['completed_cups_jobs'] = completed_jobs
    results['system_printers_changed'] = False
    (art / 'format-results.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
