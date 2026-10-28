#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent styled ODP producer; run using uv run --with odfpy python ..."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from odf.opendocument import OpenDocumentPresentation
from odf.style import Style, TextProperties, ParagraphProperties, GraphicProperties, PageLayout, PageLayoutProperties, MasterPage
from odf.draw import Page, Frame, TextBox
from odf.text import P, Span, A

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--test-bin-dir', type=Path, default=root/'build-native/fltk')
args = parser.parse_args()
with tempfile.TemporaryDirectory() as directory:
    document = OpenDocumentPresentation()
    page = PageLayout(name='Wide')
    page.addElement(PageLayoutProperties(pagewidth='400pt', pageheight='300pt'))
    document.automaticstyles.addElement(page)
    master = MasterPage(name='Default', pagelayoutname=page)
    document.masterstyles.addElement(master)
    body = Style(name='Body', family='paragraph')
    body.addElement(TextProperties(fontfamily='Noto Sans', fontsize='12pt'))
    document.styles.addElement(body)
    heading = Style(name='Heading', family='paragraph', parentstylename=body)
    heading.addElement(TextProperties(fontsize='22pt', fontweight='bold'))
    heading.addElement(ParagraphProperties(textalign='center'))
    document.styles.addElement(heading)
    red = Style(name='Red', family='text')
    red.addElement(TextProperties(color='#dd2233', fontsize='16pt'))
    document.styles.addElement(red)
    blue = Style(name='Blue', family='text')
    blue.addElement(TextProperties(color='#2255dd'))
    document.styles.addElement(blue)
    arabic = Style(name='Arabic', family='paragraph', parentstylename=body)
    arabic.addElement(TextProperties(fontfamily='Noto Sans Arabic', fontsize='16pt', language='ar'))
    arabic.addElement(ParagraphProperties(writingmode='rl-tb', textalign='start'))
    document.styles.addElement(arabic)
    card = Style(name='Card', family='graphic')
    card.addElement(GraphicProperties(padding='10pt', textareaverticalalign='middle'))
    document.styles.addElement(card)
    slide = Page(name='Independent', masterpagename=master)
    frame = Frame(stylename=card, x='30pt', y='40pt', width='300pt', height='200pt')
    text = TextBox()
    text.addElement(P(stylename=heading, text='Independent heading'))
    paragraph = P(stylename=body, text='Plain ')
    paragraph.addElement(Span(stylename=red, text='colored'))
    paragraph.addElement(A(stylename=blue, href='https://example.invalid/', text=' linked'))
    text.addElement(paragraph)
    text.addElement(P(stylename=arabic, text='سلام عليكم'))
    frame.addElement(text); slide.addElement(frame); document.presentation.addElement(slide)
    path = Path(directory)/'styled.odp'
    document.save(str(path)); original = path.read_bytes()
    subprocess.run([str(args.test_bin_dir/'calligra-native-rich-text-test'), '--external', str(path)], check=True)
    assert path.read_bytes() == original
