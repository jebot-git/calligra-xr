#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODF documents, native export/print PDFs and actual private CUPS jobs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile
import xml.etree.ElementTree as ET
from odf.opendocument import OpenDocumentText, OpenDocumentPresentation, OpenDocumentSpreadsheet
from odf.style import Style, TextProperties, ParagraphProperties, PageLayout, PageLayoutProperties, MasterPage, TableCellProperties, GraphicProperties
from odf.text import P
from odf.table import Table, TableRow, TableCell
from odf.draw import Page, Frame, TextBox, Rect, Image
from PIL import Image as PillowImage
from pypdf import PdfReader
from cups_runtime import PrivateCups
ROOT = Path(__file__).resolve().parents[2]


def produce(directory):
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    paths={}
    words=OpenDocumentText()
    page=PageLayout(name='PrintPage');page.addElement(PageLayoutProperties(pagewidth='14cm',pageheight='18cm',margin='1cm'));words.automaticstyles.addElement(page)
    master=MasterPage(name='Standard',pagelayoutname=page);words.masterstyles.addElement(master)
    body=Style(name='PrintBody',family='paragraph');body.addElement(TextProperties(fontfamily='DejaVu Sans',fontsize='12pt',color='#102030'));words.styles.addElement(body)
    nextpage=Style(name='PrintNext',family='paragraph',parentstylename=body);nextpage.addElement(ParagraphProperties(breakbefore='page'));words.styles.addElement(nextpage)
    words.text.addElement(P(stylename=body,text='PRINT WORDS - PAGE ONE'))
    words.text.addElement(P(stylename=body,text='Unicode: Český 日本語 العربية'))
    table=Table(name='PrintTable');row=TableRow();table.addElement(row)
    for text,background in [('LEFT CELL','#ffdddd'),('RIGHT CELL','#ddffdd')]:
        style=Style(name=text.replace(' ',''),family='table-cell');style.addElement(TableCellProperties(backgroundcolor=background,border='0.7pt solid #203040',padding='0.1cm'));words.automaticstyles.addElement(style)
        cell=TableCell(stylename=style,valuetype='string');cell.addElement(P(stylename=body,text=text));row.addElement(cell)
    words.text.addElement(table)
    words.text.addElement(P(stylename=nextpage,text='PRINT WORDS - PAGE TWO'))
    words.text.addElement(P(stylename=body,text='Last line on the second page.'))
    paths['words']=directory/'print-words.odt';words.save(str(paths['words']))
    sheets=OpenDocumentSpreadsheet()
    for name in ('First worksheet','Second worksheet'):
        table=Table(name=name);sheets.spreadsheet.addElement(table)
        for r in range(45 if name.startswith('First') else 1):
            row=TableRow();table.addElement(row)
            for c in range(7 if name.startswith('First') else 1):
                if r==0 and c==0:
                    cell=TableCell(valuetype='string');cell.addElement(P(text='PRINT SHEETS' if name.startswith('First') else 'SECOND SHEET'))
                elif r==44 and c==6:
                    cell=TableCell(valuetype='string');cell.addElement(P(text='LAST TILE'))
                elif c==1:
                    cell=TableCell(formula=f'of:={r+1}*4',valuetype='float',value=901)
                else:cell=TableCell(valuetype='float',value=(r+1)*10+c)
                row.addElement(cell)
    paths['sheets']=directory/'print-sheets.ods';sheets.save(str(paths['sheets']))
    stage=OpenDocumentPresentation()
    page=PageLayout(name='SlidePage');page.addElement(PageLayoutProperties(pagewidth='24cm',pageheight='13.5cm'));stage.automaticstyles.addElement(page)
    master=MasterPage(name='Standard',pagelayoutname=page);stage.masterstyles.addElement(master)
    fill=Style(name='RedShape',family='graphic');fill.addElement(GraphicProperties(fill='solid',fillcolor='#cc2020',stroke='solid',strokecolor='#000000',strokewidth='1pt'));stage.automaticstyles.addElement(fill)
    text=Style(name='SlideText',family='paragraph');text.addElement(TextProperties(fontfamily='DejaVu Sans',fontsize='18pt',color='#102030'));stage.styles.addElement(text)
    image=directory/'print-image.png';PillowImage.new('RGBA',(80,60),(20,80,220,160)).save(image)
    href=stage.addPicture(str(image))
    for n in (1,2):
        slide=Page(name=f'Slide {n}',masterpagename=master);stage.presentation.addElement(slide)
        slide.addElement(Rect(stylename=fill,x='2cm',y='3cm',width='6cm',height='3cm'))
        frame=Frame(x='1cm',y='1cm',width='21cm',height='2cm');box=TextBox();box.addElement(P(stylename=text,text=f'PRINT STAGE - SLIDE {n}'));frame.addElement(box);slide.addElement(frame)
        frame=Frame(x='10cm',y='4cm',width='5cm',height='3.75cm');frame.addElement(Image(href=href,type='simple',show='embed',actuate='onLoad'));slide.addElement(frame)
    paths['stage']=directory/'print-stage.odp';stage.save(str(paths['stage']))
    return paths


def verify(binary_dir, directory, cups=None):
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    sources=produce(directory/'fixtures');results={}
    env={**os.environ,'LD_LIBRARY_PATH':'/usr/local/lib/domed/bitmap'}
    env.pop('DISPLAY',None);env.pop('WAYLAND_DISPLAY',None)
    if cups:env.update(CUPS_SERVER=cups.server,LC_ALL='C')
    for app,source in sources.items():
        before=hashlib.sha256(source.read_bytes()).hexdigest()
        output=directory/(app+'.pdf');exported=directory/('exported-'+source.name)
        command=[str(Path(binary_dir).resolve()/('calligra-'+app+'-fltk')),str(source),'--export-odf',str(exported),'--print-pdf',str(output)]
        if cups:command+=['--print-to',cups.name]
        run=subprocess.run(command,env=env,text=True,capture_output=True,check=True)
        (directory/(app+'-print.log')).write_text(run.stdout+run.stderr)
        assert 'missing glyphs' not in run.stderr,(app,run.stderr)
        reader=PdfReader(output);expected={'words':2,'sheets':5,'stage':2}[app]
        assert len(reader.pages)==expected,(app,len(reader.pages),expected)
        alltext='\n'.join(page.extract_text() or '' for page in reader.pages)
        (directory/(app+'-extracted.txt')).write_text(alltext)
        assert {'words':'PAGE TWO','sheets':'LAST TILE','stage':'SLIDE 2'}[app] in alltext,(app,alltext)
        assert before==hashlib.sha256(source.read_bytes()).hexdigest()
        with zipfile.ZipFile(exported) as archive:
            assert archive.infolist()[0].filename=='mimetype' and archive.infolist()[0].compress_type==zipfile.ZIP_STORED
            assert archive.read('mimetype').decode()=='application/vnd.oasis.opendocument.'+{'words':'text','sheets':'spreadsheet','stage':'presentation'}[app]
            ET.fromstring(archive.read('content.xml'));ET.fromstring(archive.read('META-INF/manifest.xml'))
        second=directory/(app+'-reopened.pdf')
        subprocess.run([command[0],str(exported),'--print-pdf',str(second)],env=env,text=True,capture_output=True,check=True)
        assert len(PdfReader(second).pages)==expected
        # Rasterize every page; visual review and pixel checks consume these PNGs.
        subprocess.run(['pdftoppm','-r','100','-png',str(output),str(directory/(app+'-page'))],check=True,capture_output=True)
        results[app]={'pages':expected,'exported_odf':str(exported),'pdf':str(output),'headless':True,'source_unchanged':True,'reopened':True}
        print('PASS',app,'OpenDocument export, headless native print pages, all-page rasterization and reopen',flush=True)
    if cups:
        jobs=cups.completed(3)
        for captured in jobs:
            assert captured.read_bytes().startswith(b'%PDF-')
        assert sorted(len(PdfReader(path).pages) for path in jobs)==[2,2,5]
        results['cups']={'server':cups.server,'completed_jobs':3,'captured_documents':[str(path) for path in jobs],'system_printers_changed':False}
        print('PASS three actual CUPS jobs completed through the private capture backend',flush=True)
    (directory/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    return results

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path);parser.add_argument('--bin-dir',type=Path,default=ROOT/'build-native/fltk');parser.add_argument('--cups',action='store_true');args=parser.parse_args()
    if args.cups:
        with PrivateCups(args.directory/'cups') as cups:verify(args.bin_dir,args.directory,cups)
    else:verify(args.bin_dir,args.directory)
