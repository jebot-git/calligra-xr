#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODP curved path producer; run using uv run --with odfpy python ..."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from odf.opendocument import OpenDocumentPresentation
from odf.style import Style, GraphicProperties, PageLayout, PageLayoutProperties, MasterPage
from odf.draw import Page, Path as DrawPath, Rect

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--test-bin-dir', type=Path, default=root/'build-native/fltk')
args = parser.parse_args()
with tempfile.TemporaryDirectory() as directory:
    document=OpenDocumentPresentation()
    layout=PageLayout(name='Wide')
    layout.addElement(PageLayoutProperties(pagewidth='400pt',pageheight='300pt'))
    document.automaticstyles.addElement(layout)
    master=MasterPage(name='Default',pagelayoutname=layout)
    document.masterstyles.addElement(master)
    paint=Style(name='Blue',family='graphic')
    paint.addElement(GraphicProperties(fill='solid',fillcolor='#4488ee'))
    document.styles.addElement(paint)
    page=Page(name='Independent paths',masterpagename=master)
    page.addElement(DrawPath(stylename=paint,x='20pt',y='45pt',width='100pt',height='100pt',viewbox='-10 20 200 100',d='M-10 120 C-10 20 190 20 190 120 Z'))
    page.addElement(Rect(stylename=paint,x='150pt',y='45pt',width='100pt',height='100pt',cornerradius='25pt'))
    document.presentation.addElement(page)
    path=Path(directory)/'independent-paths.odp'
    document.save(str(path));original=path.read_bytes()
    subprocess.run([str(args.test_bin_dir/'calligra-native-path-test'),'--external',str(path)],check=True)
    assert path.read_bytes()==original
