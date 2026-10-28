#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Produce an ODF adjustment handle independently with odfpy."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import zipfile
import xml.etree.ElementTree as ET
from odf.opendocument import OpenDocumentPresentation
from odf.style import Style, GraphicProperties, PageLayout, PageLayoutProperties, MasterPage
from odf.draw import Page, CustomShape, EnhancedGeometry, Handle
from odf.text import P
from odf.namespaces import DRAWNS, OFFICENS

root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--test-bin-dir',type=Path,default=root/'build-native/fltk')
a=p.parse_args()
with tempfile.TemporaryDirectory() as directory:
    doc=OpenDocumentPresentation()
    layout=PageLayout(name='Wide');layout.addElement(PageLayoutProperties(pagewidth='400pt',pageheight='300pt'));doc.automaticstyles.addElement(layout)
    master=MasterPage(name='Default',pagelayoutname=layout);doc.masterstyles.addElement(master)
    paint=Style(name='Blue',family='graphic');paint.addElement(GraphicProperties(fill='solid',fillcolor='#4488ee'));doc.styles.addElement(paint)
    page=Page(name='Independent handles',masterpagename=master)
    shape=CustomShape(stylename=paint,x='20pt',y='30pt',width='180pt',height='90pt')
    shape.addElement(P(text='Independent handle text'))
    g=EnhancedGeometry(viewbox='0 0 100 100',modifiers='60 20',enhancedpath='M0 $1 L$0 $1 $0 0 100 50 $0 100 0 100 Z N')
    h=Handle(check_grammar=False)
    # odfpy's schema predates ODF 1.4's split handle attributes.
    for name,value in [('handle-position-x','$0'),('handle-position-y','$1'),('handle-range-x-minimum','10'),('handle-range-x-maximum','80')]:h.setAttrNS(DRAWNS,name,value)
    g.addElement(h);shape.addElement(g);page.addElement(shape);doc.presentation.addElement(page)
    path=Path(directory)/'independent-handles.odp';doc.save(str(path))
    with zipfile.ZipFile(path) as z:entries={name:z.read(name) for name in z.namelist()}
    content=ET.fromstring(entries['content.xml']);content.set('{'+OFFICENS+'}version','1.4')
    entries['content.xml']=ET.tostring(content,encoding='utf-8',xml_declaration=True)
    with zipfile.ZipFile(path,'w') as z:
        for name,data in entries.items():z.writestr(name,data)
    original=path.read_bytes()
    subprocess.run([str(a.test_bin_dir/'calligra-native-handle-test'),'--external',str(path)],check=True)
    assert path.read_bytes()==original
