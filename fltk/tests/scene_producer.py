#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODP scene producer; run using uv run --with odfpy python ..."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from odf.opendocument import OpenDocumentPresentation
from odf.style import Style, GraphicProperties, PageLayout, PageLayoutProperties, MasterPage
from odf.draw import Page, Rect, G, Frame, TextBox
from odf.text import P

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--test-bin-dir', type=Path, default=root/'build-native/fltk')
args = parser.parse_args()
with tempfile.TemporaryDirectory() as directory:
    document = OpenDocumentPresentation()
    layout = PageLayout(name='Wide')
    layout.addElement(PageLayoutProperties(pagewidth='480pt', pageheight='270pt'))
    document.automaticstyles.addElement(layout)
    paint = Style(name='Solid', family='graphic')
    paint.addElement(GraphicProperties(fill='solid', fillcolor='#225588'))
    document.styles.addElement(paint)
    master = MasterPage(name='Standard', pagelayoutname=layout)
    master.addElement(Rect(stylename=paint, x='0pt', y='0pt', width='480pt', height='25pt'))
    document.masterstyles.addElement(master)
    page = Page(name='Independent', masterpagename=master)
    page.addElement(Rect(stylename=paint, x='20pt', y='35pt', width='50pt', height='20pt'))
    group = G()
    frame = Frame(x='5pt', y='10pt', width='200pt', height='35pt', transform='translate(90pt 60pt)')
    text = TextBox(); text.addElement(P(text='Independent Český Stage'))
    frame.addElement(text); group.addElement(frame); page.addElement(group)
    document.presentation.addElement(page)
    path = Path(directory)/'independent.odp'
    document.save(str(path)); original = path.read_bytes()
    subprocess.run([str(args.test_bin_dir/'calligra-native-scene-test'), '--external', str(path)], check=True)
    assert path.read_bytes() == original
