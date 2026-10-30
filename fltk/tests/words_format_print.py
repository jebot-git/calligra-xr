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
from odf.style import Style, TextProperties, ParagraphProperties
from odf.text import P, Span, LineBreak, Tab
from odf.table import Table, TableRow, TableCell, TableColumn
from PIL import Image
from pypdf import PdfReader

from cups_runtime import PrivateCups
from print_producer import ROOT, verify


def color_count(picture, color):
    pixels = picture.load()
    return sum(pixels[x,y] == color for y in range(picture.height)
               for x in range(picture.width))


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


def highlight_source(path):
    # Independent ODF producer: common/inherited/automatic styles, explicit
    # transparency, bidi, ligatures, tabs, soft breaks and justified wrapping.
    doc = OpenDocumentText()
    body = Style(name='HighlightBody', family='paragraph')
    body.addElement(TextProperties(fontfamily='DejaVu Sans', fontsize='16pt',
                                   backgroundcolor='#fff176'))
    doc.styles.addElement(body)
    clear = Style(name='Clear', family='text')
    clear.addElement(TextProperties(backgroundcolor='transparent'))
    doc.styles.addElement(clear)
    green = Style(name='Green', family='text', parentstylename=clear)
    green.addElement(TextProperties(backgroundcolor='#80ffb0'))
    doc.automaticstyles.addElement(green)
    p = P(stylename=body, text='Highlighted office affine ')
    p.addElement(Span(stylename=clear, text='clear gap '))
    p.addElement(Span(stylename=green, text='green'))
    doc.text.addElement(p)
    p = P(stylename=body, text='left'); p.addElement(Tab()); p.addText('right')
    p.addElement(LineBreak()); p.addText('After soft break')
    doc.text.addElement(p)
    doc.text.addElement(P(stylename=body, text='אבג 123 日本語'))
    justified = Style(name='HighlightJustify', family='paragraph', parentstylename=body)
    justified.addElement(ParagraphProperties(textalign='justify'))
    doc.styles.addElement(justified)
    doc.text.addElement(P(stylename=justified, text='Wrapped highlighted words '*12))
    doc.save(str(path))
    with zipfile.ZipFile(path, 'a') as archive:
        archive.writestr('Extras/opaque.bin', b'\0keep\xff')


def decoration_source(path, enabled=True):
    doc = OpenDocumentText()
    body = Style(name='DecorationBody', family='paragraph')
    body.addElement(TextProperties(fontfamily='Noto Sans', fontsize='16pt'))
    doc.styles.addElement(body)
    clear = Style(name='DecorationClear', family='text')
    clear.addElement(TextProperties(textunderlinestyle='none', textlinethroughstyle='none'))
    doc.styles.addElement(clear)
    for i, pattern in enumerate(('solid', 'dotted', 'dash', 'long-dash', 'dot-dash', 'dot-dot-dash', 'wave')):
        style = Style(name='Decoration'+str(i), family='paragraph', parentstylename=body)
        props = TextProperties(textunderlinestyle=pattern if enabled else 'none',
                               textlinethroughstyle=pattern if enabled else 'none',
                               textunderlinecolor='#0066cc', textlinethroughcolor='#c04020',
                               textunderlinemode='skip-white-space' if i%2 else 'continuous',
                               textlinethroughmode='skip-white-space' if i%2 else 'continuous',
                               textunderlinewidth=('auto', 'thin', 'bold', '2pt', '5%', 'medium', 'thick')[i])
        if enabled:
            props.setAttribute('textunderlinetype', 'double' if i%2 else 'single')
            props.setAttribute('textlinethroughtype', 'double' if i%2 else 'single')
        style.addElement(props); doc.styles.addElement(style)
        paragraph = P(stylename=style, text=pattern+' office affine ')
        paragraph.addElement(Tab()); paragraph.addText('after tab ')
        paragraph.addElement(Span(stylename=clear, text='clear gap'))
        doc.text.addElement(paragraph)
    partial = Style(name='PartialLigature', family='text')
    partial.addElement(TextProperties(textunderlinestyle='solid' if enabled else 'none',
                                     textunderlinecolor='#0066cc'))
    doc.styles.addElement(partial)
    paragraph = P(stylename=body, text='Partial of')
    paragraph.addElement(Span(stylename=partial, text='f')); paragraph.addText('ice affine')
    doc.text.addElement(paragraph)
    justified = Style(name='DecorationJustify', family='paragraph', parentstylename='Decoration6')
    justified.addElement(ParagraphProperties(textalign='justify'))
    doc.styles.addElement(justified)
    doc.text.addElement(P(stylename=justified, text='Justified decorated words '*9))
    rtl = Style(name='DecorationRTL', family='paragraph', parentstylename='Decoration0')
    rtl.addElement(ParagraphProperties(writingmode='rl-tb'))
    doc.styles.addElement(rtl)
    doc.text.addElement(P(stylename=rtl, text='אבג 123 office 日本語'))
    cell_style = Style(name='DecorationCell', family='table-cell')
    cell_style.addElement(TextProperties(textunderlinestyle='dotted' if enabled else 'none',
                                        textunderlinecolor='#0066cc'))
    doc.styles.addElement(cell_style)
    table = Table(name='DecoratedCells'); table.addElement(TableColumn(numbercolumnsrepeated=2))
    row = TableRow()
    for label in ('Inherited cell', 'Other cell'):
        cell = TableCell(stylename=cell_style, valuetype='string')
        cell.addElement(P(stylename=body, text=label)); row.addElement(cell)
    table.addElement(row); doc.text.addElement(table)
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
    parser.add_argument('--style-artifacts', type=Path,
                        help='Optional named paragraph style UI snapshots (defaults to --ui-artifacts)')
    parser.add_argument('--character-style-artifacts', type=Path,
                        help='Optional named character style UI snapshots (defaults to --ui-artifacts)')
    parser.add_argument('--test-bin-dir', type=Path,
                        help='Native geometry test binary directory (defaults to --bin-dir)')
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
        cases.append(('highlight-source', art / 'highlight-source.odt'))
        cases.append(('decoration-source', art / 'decoration-source.odt'))
        style_artifacts = (args.style_artifacts or args.ui_artifacts).resolve()
        for name, file in [('named-created', 'style-created-applied.odt'), ('named-edited', 'style-edited.odt')]:
            if (style_artifacts/file).is_file(): cases.append((name,style_artifacts/file))
        character_artifacts = (args.character_style_artifacts or args.ui_artifacts).resolve()
        for name, file in [('character-created','character-style-created.odt'),
                           ('character-edited','character-style-edited.odt'),
                           ('character-typed','character-style-typed.odt')]:
            if (character_artifacts/file).is_file(): cases.append((name,character_artifacts/file))
        for name, source in cases:
            if name == 'unicode-source':
                unicode_source(source)
            if name == 'highlight-source':
                highlight_source(source)
            if name == 'decoration-source':
                decoration_source(source)
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
            elif name == 'decoration-source':
                extracted = subprocess.check_output(['pdftotext', str(output), '-'], text=True)
                assert 'office affine' in extracted and 'Partial office affine' in extracted
                assert 'Inherited cell' in extracted and 'clear gap' in extracted
            elif name == 'highlight-source':
                # pypdf ignores Cairo ActualText for the ffi ligatures; Poppler
                # must extract the original source rather than glyph aliases.
                extracted = subprocess.check_output(['pdftotext', str(output), '-'], text=True)
                assert 'Highlighted office affine clear gap green' in extracted, repr(extracted)
                (art / 'highlight-extracted.txt').write_text(extracted)
                (art / 'highlight-pypdf-extracted.txt').write_text(text)
                assert 'left' in text and 'right' in text and 'After soft break' in text
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
                assert color_count(picture, (255, 241, 118)) > 250
            if name == 'highlight-source':
                picture = Image.open(pages[0]).convert('RGB')
                assert color_count(picture, (255, 241, 118)) > 15000
                assert color_count(picture, (128, 255, 176)) > 300
                bbox = art / 'highlight-bounds.html'
                subprocess.run(['pdftotext', '-bbox', str(output), str(bbox)], check=True)
                xml = ET.fromstring(bbox.read_text())
                ns = '{http://www.w3.org/1999/xhtml}'
                gap = next(w for w in xml.iter(ns+'word') if w.text == 'clear')
                scale = 100/72
                crop = picture.crop((round(float(gap.get('xMin'))*scale),
                                     round(float(gap.get('yMin'))*scale),
                                     round(float(gap.get('xMax'))*scale),
                                     round(float(gap.get('yMax'))*scale)))
                assert color_count(crop, (255,255,255)) > 50
                assert color_count(crop, (255,241,118)) == 0
            if name == 'decoration-source':
                picture = Image.open(pages[0]).convert('RGB')
                assert color_count(picture, (0,102,204)) > 100
                assert color_count(picture, (192,64,32)) > 100
                control_source = art / 'decoration-control.odt'
                control_pdf = art / 'decoration-control.pdf'
                decoration_source(control_source, enabled=False)
                subprocess.run([str(binary), str(control_source), '--print-pdf', str(control_pdf)],
                               env=env, capture_output=True, check=True)
                control_pages = render(control_pdf, art / 'decoration-control-page')
                assert len(control_pages) == len(pages)
                control_picture = Image.open(control_pages[0]).convert('RGB')
                assert color_count(control_picture, (0,102,204)) == 0
                assert color_count(control_picture, (192,64,32)) == 0
                # Line effects, including a boundary inside ffi, never change
                # source words, font layout, pagination, glyph positions or tabs.
                def words(pdf, name):
                    bbox = art / (name+'-bounds.html')
                    subprocess.run(['pdftotext', '-bbox', str(pdf), str(bbox)], check=True)
                    root = ET.fromstring(bbox.read_text())
                    return [(w.text, dict(w.attrib)) for w in root.iter('{http://www.w3.org/1999/xhtml}word')]
                helper = (args.test_bin_dir or args.bin_dir).resolve() / 'calligra-native-text-shaper-test'
                assert helper.is_file(), 'Use --test-bin-dir for the mandatory native geometry gate'
                geometry = []
                for label, package in [('styled', source), ('plain', control_source)]:
                    result = subprocess.check_output([str(helper), '--layout', str(package)], env=env, text=True)
                    (art / ('decoration-'+label+'-layout.json')).write_text(result)
                    geometry.append(json.loads(result))
                assert geometry[0] == geometry[1], 'Line effects changed native glyph/caret geometry'
                styled_words, plain_words = words(output, 'decoration'), words(control_pdf, 'decoration-control')
                assert len(styled_words) == len(plain_words)
                max_delta = 0
                for (a, bounds_a), (b, bounds_b) in zip(styled_words, plain_words):
                    assert a == b
                    for axis in ('xMin', 'yMin', 'xMax', 'yMax'):
                        delta = abs(float(bounds_a[axis]) - float(bounds_b[axis]))
                        max_delta = max(max_delta, delta)
                        # Cairo emits relative or absolute PDF text matrices
                        # depending on intervening paths. Allow decimal stream
                        # rounding only; native geometry above remains exact.
                        assert delta < .001, (a, axis, delta)
                (art / 'decoration-layout-comparison.json').write_text(json.dumps({
                    'native_glyph_caret_layout_exact': True, 'words': len(styled_words),
                    'pdf_serialization_tolerance_points': .001,
                    'max_pdf_word_box_delta_points': max_delta}, indent=2)+'\n')
            if name.startswith('named-'):
                picture = Image.open(pages[0]).convert('RGB')
                assert color_count(picture, (22,90,167)) > 100
                faces = subprocess.check_output(['pdffonts', str(output)], text=True)
                assert 'Hack-Bold' in faces, faces
                bbox = art / (name+'-bounds.html')
                subprocess.run(['pdftotext','-bbox',str(output),str(bbox)],check=True)
                root = ET.fromstring(bbox.read_text()); ns='{http://www.w3.org/1999/xhtml}'
                page = root.find('.//'+ns+'page')
                words = list(page.iter(ns+'word'))
                first = next(w for w in words if w.text == 'Start')
                tail = next(w for w in words if w.text == 'tail')
                midpoint = (float(first.get('xMin'))+float(tail.get('xMax')))/2
                assert abs(midpoint-(float(page.get('width'))/2+3)) < 3, midpoint
                results[name+'-title'] = {'font_height':float(first.get('yMax'))-float(first.get('yMin')),
                                          'midpoint':midpoint,'centered':True}
            if name.startswith('character-'):
                picture = Image.open(pages[0]).convert('RGB')
                assert color_count(picture,(22,90,167)) > 50
                assert color_count(picture,(255,241,118)) > 100
                faces = subprocess.check_output(['pdffonts',str(output)],text=True)
                assert 'Hack-Italic' in faces, faces
                if name in ('character-created','character-edited'):
                    bbox=art/(name+'-bounds.html')
                    subprocess.run(['pdftotext','-bbox',str(output),str(bbox)],check=True)
                    root=ET.fromstring(bbox.read_text()); ns='{http://www.w3.org/1999/xhtml}'
                    word=next(w for w in root.iter(ns+'word') if w.text=='Český')
                    results[name+'-word']={'height':float(word.get('yMax'))-float(word.get('yMin'))}
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
        if 'named-created-title' in results and 'named-edited-title' in results:
            a=results['named-created-title']['font_height']; b=results['named-edited-title']['font_height']
            assert abs(b/a-1.2) < .03, (a,b)
            with zipfile.ZipFile(style_artifacts/'style-created-applied.odt') as a, zipfile.ZipFile(style_artifacts/'style-edited.odt') as b:
                assert a.read('content.xml') == b.read('content.xml')
                assert a.read('styles.xml') != b.read('styles.xml')
        if 'character-created-word' in results and 'character-edited-word' in results:
            small=results['character-created-word']['height']; large=results['character-edited-word']['height']
            assert abs(large/small-24/18)<.03,(small,large)
            with zipfile.ZipFile(character_artifacts/'character-style-created.odt') as a, zipfile.ZipFile(character_artifacts/'character-style-edited.odt') as b:
                assert a.read('content.xml')==b.read('content.xml')
                assert a.read('styles.xml')!=b.read('styles.xml')
        assert len(cups.completed(completed_jobs)) == completed_jobs
    results['completed_cups_jobs'] = completed_jobs
    results['system_printers_changed'] = False
    (art / 'format-results.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
