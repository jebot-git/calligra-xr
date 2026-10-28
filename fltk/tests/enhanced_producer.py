#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent odfpy enhanced geometry producer and edit/save check."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from odf.opendocument import OpenDocumentPresentation
from odf.style import Style, GraphicProperties, TextProperties, PageLayout, PageLayoutProperties, MasterPage
from odf.draw import Page, CustomShape, EnhancedGeometry, Equation
from odf.text import P

root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--test-bin-dir',type=Path,default=root/'build-native/fltk');a=p.parse_args()
with tempfile.TemporaryDirectory() as directory:
    document=OpenDocumentPresentation()
    layout=PageLayout(name='Wide');layout.addElement(PageLayoutProperties(pagewidth='400pt',pageheight='250pt'));document.automaticstyles.addElement(layout)
    master=MasterPage(name='Default',pagelayoutname=layout);document.masterstyles.addElement(master)
    paint=Style(name='Blue',family='graphic');paint.addElement(GraphicProperties(fill='solid',fillcolor='#4488ee',padding='2pt'));document.styles.addElement(paint)
    label=Style(name='Label',family='paragraph');label.addElement(TextProperties(fontfamily='Noto Sans',fontsize='12pt',color='#ffffff'));document.styles.addElement(label)
    page=Page(name='Independent enhanced shape',masterpagename=master)
    shape=CustomShape(stylename=paint,x='20pt',y='40pt',width='200pt',height='100pt')
    shape.addElement(P(stylename=label,text='Independent'))
    geometry=EnhancedGeometry(viewbox='0 0 100 100',modifiers='60 20',textareas='5 ?top ?base ?bottom',enhancedpath='M0 ?top L?base ?top ?base 0 ?right ?middle ?base ?height ?base ?bottom 0 ?bottom Z N')
    for name,formula in [('base','$0'),('top','$1'),('bottom','height-$1'),('right','width'),('middle','height/2'),('height','height')]:
        geometry.addElement(Equation(name=name,formula=formula))
    shape.addElement(geometry);page.addElement(shape);document.presentation.addElement(page)
    path=Path(directory)/'independent-enhanced.odp';document.save(str(path));original=path.read_bytes()
    subprocess.run([str(a.test_bin_dir/'calligra-native-enhanced-test'),'--external',str(path)],check=True)
    subprocess.run([str(a.test_bin_dir/'calligra-native-presentation-history-test'),'--external',str(path)],check=True)
    assert path.read_bytes()==original
