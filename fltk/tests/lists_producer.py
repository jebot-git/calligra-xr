#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent odfpy list producer and XML-preserving edit verification."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import zipfile
import xml.etree.ElementTree as ET
from odf.opendocument import OpenDocumentText
from odf.style import Style, TextProperties, ListLevelProperties, ListLevelLabelAlignment
from odf.text import ListStyle, ListLevelStyleNumber, ListLevelStyleBullet, List, ListItem, P
from odf.namespaces import XMLNS

root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--test-bin-dir',type=Path,default=root/'build-native/fltk');a=p.parse_args()
with tempfile.TemporaryDirectory() as directory:
    doc=OpenDocumentText()
    ink=Style(name='Label',family='text');ink.addElement(TextProperties(color='#bb2244',fontsize='11pt'));doc.styles.addElement(ink)
    numbers=ListStyle(name='Numbers')
    for level,fmt,prefix,suffix,display in [(1,'1','[',']',1),(2,'i','',')',2)]:
        item=ListLevelStyleNumber(level=level,numformat=fmt,numprefix=prefix,numsuffix=suffix,displaylevels=display,stylename=ink)
        props=ListLevelProperties(listlevelpositionandspacemode='label-alignment',textalign='end')
        props.addElement(ListLevelLabelAlignment(marginleft=str(level*36)+'pt',textindent='-18pt',labelfollowedby='listtab',listtabstopposition=str(level*36)+'pt'))
        item.addElement(props);numbers.addElement(item)
    doc.styles.addElement(numbers)
    bullets=ListStyle(name='Bullets');bullets.addElement(ListLevelStyleBullet(level=1,bulletchar='✓',stylename=ink));doc.styles.addElement(bullets)
    first=List(stylename=numbers);first.setAttrNS(XMLNS,'id','list-one')
    item=ListItem();item.addElement(P(text='Independent first'));nested=List()
    sub=ListItem();sub.addElement(P(text='Independent nested'));nested.addElement(sub);item.addElement(nested);first.addElement(item);doc.text.addElement(first)
    continued=List(stylename=numbers,continuelist='list-one');item=ListItem();item.addElement(P(text='Independent continuation'));continued.addElement(item);doc.text.addElement(continued)
    bullet=List(stylename=bullets);item=ListItem();item.addElement(P(text='Independent bullet'));bullet.addElement(item);doc.text.addElement(bullet)
    path=Path(directory)/'independent-lists.odt';doc.save(str(path));original=path.read_bytes()
    subprocess.run([str(a.test_bin_dir/'calligra-native-list-test'),'--external',str(path)],check=True)
    assert path.read_bytes()==original
    whitespace={'{urn:oasis:names:tc:opendocument:xmlns:text:1.0}'+name for name in ('s','tab','line-break')}
    def signature(e):return e.tag,tuple(sorted(e.attrib.items())),tuple(signature(c) for c in e if c.tag not in whitespace)
    with zipfile.ZipFile(path) as source,zipfile.ZipFile(str(path)+'.native.odt') as saved:
        assert source.namelist()==saved.namelist() or set(source.namelist())==set(saved.namelist())
        assert signature(ET.fromstring(source.read('content.xml')))==signature(ET.fromstring(saved.read('content.xml')))
        for name in source.namelist():
            if name!='content.xml':assert source.read(name)==saved.read(name)
        assert b'Edited' in saved.read('content.xml')
