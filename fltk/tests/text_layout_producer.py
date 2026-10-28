#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent styled ODT producer; run using uv run --with odfpy python ..."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from odf.opendocument import OpenDocumentText
from odf.style import Style, TextProperties, ParagraphProperties, PageLayout, PageLayoutProperties, MasterPage
from odf.text import P, H

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--test-bin-dir', type=Path, default=root/'build-native/fltk')
args = parser.parse_args()
with tempfile.TemporaryDirectory() as directory:
    document = OpenDocumentText()
    page = PageLayout(name='Page')
    page.addElement(PageLayoutProperties(pagewidth='8.5in', pageheight='11in', margin='1in'))
    document.automaticstyles.addElement(page)
    document.masterstyles.addElement(MasterPage(name='Standard', pagelayoutname=page))
    body = Style(name='Body', family='paragraph')
    body.addElement(TextProperties(fontsize='12pt'))
    document.styles.addElement(body)
    heading = Style(name='Heading', family='paragraph', parentstylename=body, masterpagename='Standard')
    heading.addElement(TextProperties(fontsize='150%', fontweight='bold'))
    document.styles.addElement(heading)
    next_page = Style(name='NextPage', family='paragraph', parentstylename=body)
    next_page.addElement(ParagraphProperties(breakbefore='page'))
    document.automaticstyles.addElement(next_page)
    document.text.addElement(H(outlinelevel=1, stylename=heading, text='Native heading'))
    document.text.addElement(P(stylename=body, text='Český 日本語 body'))
    document.text.addElement(P(stylename=next_page, text='Next page'))
    path = Path(directory)/'independent.odt'
    document.save(str(path))
    original = path.read_bytes()
    subprocess.run([str(args.test_bin_dir/'calligra-native-text-layout-test'), '--external', str(path)], check=True)
    assert path.read_bytes() == original
