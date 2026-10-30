#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODF tab/leader producer and native round-trip validation."""
import argparse
from pathlib import Path
import subprocess
import zipfile
import xml.etree.ElementTree as ET
from odf.opendocument import OpenDocumentText, OpenDocumentPresentation
from odf.style import Style, DefaultStyle, ParagraphProperties, TextProperties, TabStops, TabStop, PageLayout, PageLayoutProperties, MasterPage
from odf.text import P, Tab, Span
from odf.draw import Page, Frame, TextBox
from odf.namespaces import TEXTNS

def produce(directory, test_bin=None):
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    definitions=[('Solid','left','solid','single'),('Dotted','right','dotted','single'),('Dash','center','dash','single'),('LongDash','char','long-dash','single'),('DotDash','left','dot-dash','single'),('DotDotDash','right','dot-dot-dash','single'),('Wave','center','wave','single'),('Double','left','solid','double'),('Text','right','solid','single'),('RTL','char','solid','single'),('Clear','left','none','none'),('Centered','center','solid','single'),('Justified','left','dotted','single'),('Automatic','left','solid','single')]
    for app,create,ext in [('words',OpenDocumentText,'odt'),('stage',OpenDocumentPresentation,'odp')]:
        doc=create();default=DefaultStyle(family='paragraph');props=ParagraphProperties(tabstopdistance='64pt',lineheight='24pt',marginbottom='2pt');default.addElement(props);default.addElement(TextProperties(fontfamily='DejaVu Sans Mono',fontsize='14pt'));doc.styles.addElement(default)
        layout=PageLayout(name='Paper');layout.addElement(PageLayoutProperties(pagewidth='620pt',pageheight='500pt',margin='24pt'));doc.automaticstyles.addElement(layout);master=MasterPage(name='Default',pagelayoutname=layout);doc.masterstyles.addElement(master)
        ink=Style(name='LeaderInk',family='text');ink.addElement(TextProperties(color='#7820cc',fontsize='150%',fontweight='bold'));doc.styles.addElement(ink)
        auto_ink=Style(name='LeaderInk',family='text');auto_ink.addElement(TextProperties(color='#cc2040',fontsize='120%'));doc.automaticstyles.addElement(auto_ink)
        tab_ink=Style(name='TabInk',family='text');tab_ink.addElement(TextProperties(fontsize='18pt'));doc.styles.addElement(tab_ink)
        parent=Style(name='Parent',family='paragraph');props=ParagraphProperties();stops=TabStops()
        for pos in ['40pt','80pt','120pt']:stops.addElement(TabStop(position=pos))
        props.addElement(stops);parent.addElement(props);doc.styles.addElement(parent)
        if app=='words':target=doc.text
        else:
            page=Page(name='Native tabs',masterpagename=master);doc.presentation.addElement(page);frame=Frame(x='24pt',y='24pt',width='572pt',height='452pt');page.addElement(frame);target=TextBox();frame.addElement(target)
        for i,(name,align,line,kind) in enumerate(definitions):
            style=Style(name=name,family='paragraph',parentstylename=parent);props=ParagraphProperties()
            stops=TabStops()
            if name!='Clear':
                stop=TabStop(position='280pt',type=align,leaderstyle=line,leadertype=kind,leaderwidth='1.25pt',leadercolor='#cc2040')
                if align=='char':stop.setAttribute('char','.')
                if name in ('Text','Automatic'):
                    stop.setAttribute('leadertext','·');stop.setAttribute('leadertextstyle','LeaderInk')
                stops.addElement(stop)
            props.addElement(stops)
            if name=='Solid':props.setAttribute('tabstopdistance','12pt') # ignored outside defaults
            if name=='RTL':props.setAttribute('writingmode','rl-tb')
            if name=='Centered':props.setAttribute('textalign','center')
            if name=='Justified':props.setAttribute('textalign','justify')
            style.addElement(props);(doc.automaticstyles if name=='Automatic' else doc.styles).addElement(style)
            paragraph=P(stylename=style)
            if name=='Automatic':paragraph.addElement(Span(stylename=auto_ink,text=name))
            else:paragraph.addText('שלום' if name=='RTL' else name)
            if name=='Text':span=Span(stylename=tab_ink);span.addElement(Tab());paragraph.addElement(span)
            else:paragraph.addElement(Tab())
            paragraph.addText('12.50' if name!='Justified' else 'One two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen')
            target.addElement(paragraph)
        path=directory/f'tabs.{ext}';doc.save(str(path))
        # odfpy prefixes colliding automatic names. Normalize this deliberate
        # scope-collision fixture so native style lookup has to distinguish them.
        automatic_name=auto_ink.getAttribute('name')
        if automatic_name!='LeaderInk':
            temporary=path.with_suffix('.temporary')
            with zipfile.ZipFile(path) as source,zipfile.ZipFile(temporary,'w') as saved:
                for info in source.infolist():
                    data=source.read(info.filename)
                    if info.filename=='content.xml':
                        for attribute in ('style:name','text:style-name'):
                            data=data.replace((attribute+'="'+automatic_name+'"').encode(),(attribute+'="LeaderInk"').encode())
                    saved.writestr(info,data)
            temporary.replace(path)
        if test_bin:
            subprocess.run([str(Path(test_bin)/'calligra-native-tab-test'),'--external' if app=='words' else '--stage-external',str(path)],check=True)
            whitespace={'{'+TEXTNS+'}'+name for name in ('s','line-break')}
            def signature(e):return e.tag,tuple(sorted(e.attrib.items())),tuple(signature(c) for c in e if c.tag not in whitespace)
            with zipfile.ZipFile(path) as source,zipfile.ZipFile(str(path)+'.native.'+ext) as saved:
                assert set(source.namelist())==set(saved.namelist())
                assert signature(ET.fromstring(source.read('content.xml')))==signature(ET.fromstring(saved.read('content.xml')))
                for name in source.namelist():
                    if name!='content.xml':assert source.read(name)==saved.read(name),name
    return directory

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path);parser.add_argument('--test-bin-dir',type=Path)
    args=parser.parse_args();produce(args.directory,args.test_bin_dir)
