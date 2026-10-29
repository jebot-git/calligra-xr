#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODF merged/nested cell flow with changing master page width."""
from pathlib import Path
from odf.opendocument import OpenDocumentText,load
from odf.style import (Style,TextProperties,ParagraphProperties,TableProperties,TableCellProperties,
                       TableColumnProperties,TableRowProperties,PageLayout,PageLayoutProperties,MasterPage)
from odf.table import Table,TableColumn,TableRow,TableCell,CoveredTableCell,TableHeaderRows
from odf.text import P,LineBreak

def produce(directory):
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    doc=OpenDocumentText()
    for name,width,nextname in [('Narrow',300,'Wide'),('Wide',380,'Wide')]:
        page=PageLayout(name=name+'Page');page.addElement(PageLayoutProperties(pagewidth=f'{width}pt',pageheight='210pt',margin='15pt'))
        doc.automaticstyles.addElement(page)
        doc.masterstyles.addElement(MasterPage(name=name,pagelayoutname=page,nextstylename=nextname))
    def style(name,family,properties):
        s=Style(name=name,family=family);s.addElement(properties);doc.automaticstyles.addElement(s);return s
    para=style('P','paragraph',TextProperties(fontfamily='DejaVu Sans',fontsize='11pt'))
    para.addElement(ParagraphProperties(lineheight='13pt',margintop='0pt',marginbottom='0pt'))
    grid=style('Grid','table',TableProperties(align='margins',bordermodel='collapsing'))
    column=style('Column','table-column',TableColumnProperties(relcolumnwidth='1*'))
    hdrrow=style('HeaderRow','table-row',TableRowProperties(rowheight='24pt'))
    def cellstyle(name,background,color):
        s=style(name,'table-cell',TableCellProperties(backgroundcolor=background,padding='3pt',border='1pt solid #2040aa'))
        s.addElement(TextProperties(color=color));return s
    header=cellstyle('Header','#c8d8ff','#2040aa')
    red=cellstyle('Red','#fff0cc','#b03020');green=cellstyle('Green','#e0f0dc','#206030')
    cyan=cellstyle('Cyan','#d0f0f0','#205080')
    expected=[]
    def paragraph(parent,text):
        p=P(stylename=para)
        for i,line in enumerate(text.split('\n')):
            if i:p.addElement(LineBreak())
            p.addText(line)
        parent.addElement(p);expected.append(text);return p
    paragraph(doc.text,'Before')
    table=Table(name='Flow',stylename=grid);doc.text.addElement(table)
    for _ in range(2):table.addElement(TableColumn(stylename=column))
    headers=TableHeaderRows();table.addElement(headers);row=TableRow(stylename=hdrrow);headers.addElement(row)
    for title in ['Left header','Right header']:
        c=TableCell(stylename=header);row.addElement(c);paragraph(c,title)
    row=TableRow();table.addElement(row);merged=TableCell(stylename=red,numberrowsspanned=2);row.addElement(merged)
    paragraph(merged,'\n'.join(f'LEFT_{i:02} alpha beta gamma' for i in range(32))+'\nالعربية עברית é')
    nested=Table(name='Nested',stylename=grid);merged.addElement(nested);nested.addElement(TableColumn(stylename=column))
    nr=TableRow();nested.addElement(nr);nc=TableCell(stylename=cyan);nr.addElement(nc);paragraph(nc,'Nested tail')
    paragraph(merged,'Left finished')
    right=TableCell(stylename=green);row.addElement(right);paragraph(right,'RIGHT first');paragraph(right,'RIGHT next')
    row=TableRow();table.addElement(row);row.addElement(CoveredTableCell())
    right=TableCell(stylename=green);row.addElement(right);paragraph(right,'\n'.join(f'RIGHT_{i:02} delta epsilon' for i in range(10)))
    paragraph(doc.text,'After')
    path=directory/'cell-flow.odt';doc.save(str(path));load(str(path))
    return path,expected

def verify(path,expected,test_bin_dir,directory):
    import json,os,subprocess
    before=path.read_bytes()
    data=json.loads(subprocess.check_output([str(Path(test_bin_dir).resolve()/'calligra-native-cell-flow-test'),'--inspect',str(path)],
                    env={**os.environ,'LD_LIBRARY_PATH':'/usr/local/lib/domed/bitmap'},text=True))
    assert data['paragraphs']==expected,data['paragraphs']
    assert not data['unsupported'],data['unsupported']
    pages=data['pages'];assert len(pages)>=3 and pages[0]['width']==300 and all(p['width']==380 for p in pages[1:]),pages
    for source,text in enumerate(expected):
        if source in (1,2):continue
        end=0;count=0
        for page in pages:
            for line in page['lines']:
                if line['source']!=source:continue
                assert line['begin']==end,(source,end,line)
                end=line['end'];count+=1
        assert count and end==len(text),(source,count,end,len(text))
    for page in pages:
        assert len([c for c in page['cells'] if c['header']])==2,page['cells']
        for line in page['lines']:
            assert not line['missing'],line
            assert line['y']>=page['top']-1e-6 and line['y']+line['height']<=page['height']-page['bottom']+1e-6,line
            if 'clip' in line:
                x,y,w,h=line['clip'];assert line['y']>=y-1e-6 and line['y']+line['height']<=y+h+1e-6,line
    assert any(c['continued'] and c['rows']==2 for p in pages[1:] for c in p['cells'])
    assert path.read_bytes()==before
    Path(directory,'external-cell-flow.json').write_text(json.dumps(data,indent=2)+'\n')
    print('PASS independent ODF merged/nested cell continuations, changing page width, whole glyph lines and exact Unicode source coverage',flush=True)
    return data
if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path)
    parser.add_argument('--test-bin-dir',type=Path,default=Path(__file__).resolve().parents[2]/'build-native/fltk')
    args=parser.parse_args();path,expected=produce(args.directory);verify(path,expected,args.test_bin_dir,args.directory)
