#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODF keep chains crossing paragraphs, tables and nested cells."""
from pathlib import Path
from odf.opendocument import OpenDocumentText,load
from odf.style import (Style,TextProperties,ParagraphProperties,TableProperties,TableCellProperties,
                       TableColumnProperties,TableRowProperties,PageLayout,PageLayoutProperties,MasterPage)
from odf.table import Table,TableColumn,TableRow,TableCell,TableHeaderRows
from odf.text import P,LineBreak

def produce(directory):
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    cases=[]
    for kind in ('chain','long','nested','forced','master-table'):
        doc=OpenDocumentText();expected=[]
        for name,width,nextname in [('Narrow',300,'Wide'),('Wide',380,'Wide')]:
            page=PageLayout(name=name+'Page');page.addElement(PageLayoutProperties(pagewidth=f'{width}pt',pageheight='210pt',margin='15pt'))
            doc.automaticstyles.addElement(page);doc.masterstyles.addElement(MasterPage(name=name,pagelayoutname=page,nextstylename=nextname))
        def style(name,family,props):
            s=Style(name=name,family=family);s.addElement(props);doc.automaticstyles.addElement(s);return s
        para=style('P','paragraph',TextProperties(fontfamily='DejaVu Sans',fontsize='11pt',color='#203040'))
        para.addElement(ParagraphProperties(lineheight='15pt',margintop='0pt',marginbottom='0pt'))
        heading=Style(name='Heading',family='paragraph',parentstylename=para)
        heading.addElement(ParagraphProperties(keepwithnext='always'));doc.automaticstyles.addElement(heading)
        caption=Style(name='Caption',family='paragraph',parentstylename=para)
        caption.addElement(ParagraphProperties(keeptogether='always'));caption.addElement(TextProperties(color='#883088'));doc.automaticstyles.addElement(caption)
        short=Style(name='Short',family='paragraph',parentstylename=para)
        short.addElement(ParagraphProperties(keeptogether='always'));doc.automaticstyles.addElement(short)
        grid=style('Grid','table',TableProperties(align='margins',bordermodel='collapsing'))
        keepgrid=style('KeepGrid','table',TableProperties(align='margins',bordermodel='collapsing',keepwithnext='always',breakbefore='page' if kind=='forced' else 'auto'))
        if kind=='master-table':keepgrid.setAttribute('masterpagename','Narrow')
        column=style('Column','table-column',TableColumnProperties(relcolumnwidth='1*'))
        headerrow=style('HeaderRow','table-row',TableRowProperties(rowheight='20pt'))
        bodyrow=style('BodyRow','table-row',TableRowProperties(rowheight='40pt' if kind!='nested' else '30pt'))
        cellstyle=style('Cell','table-cell',TableCellProperties(backgroundcolor='#c8d8ff',padding='2pt',border='1pt solid #2040aa'))
        parentstyle=style('Parent','table-cell',TableCellProperties(backgroundcolor='#fff0cc',padding='3pt',border='1pt solid #2040aa'))
        def paragraph(parent,text,sty=para):
            p=P(stylename=sty)
            for i,line in enumerate(text.split('\n')):
                if i:p.addElement(LineBreak())
                p.addText(line)
            parent.addElement(p);index=len(expected);expected.append(text);return index
        def table(parent,name,cols=2,kept=True):
            t=Table(name=name,stylename=keepgrid if kept else grid);parent.addElement(t)
            for _ in range(cols):t.addElement(TableColumn(stylename=column))
            return t
        header_ids=[];heading_id=None
        if kind=='nested':
            outer=table(doc.text,'Outer',1,False);r=TableRow();outer.addElement(r);c=TableCell(stylename=parentstyle);r.addElement(c)
            filler=paragraph(c,'\n'.join(f'Filler_{i}' for i in range(7)))
            heading_id=paragraph(c,'Nested heading',heading)
            t=table(c,'Kept',1);row=TableRow(stylename=bodyrow);t.addElement(row);child=TableCell(stylename=cellstyle);row.addElement(child)
            target=paragraph(child,'Nested body')
            caption_id=paragraph(c,'Nested caption\nCaption second',caption)
        else:
            if kind in ('chain','forced','master-table'):
                paragraph(doc.text,'\n'.join(f'Filler_{i}' for i in range(8)))
                heading_id=paragraph(doc.text,'Český heading',heading)
            t=table(doc.text,'Kept');headers=TableHeaderRows();t.addElement(headers);row=TableRow(stylename=headerrow);headers.addElement(row)
            for title in ('Left header','Right header'):
                c=TableCell(stylename=cellstyle);row.addElement(c);header_ids.append(paragraph(c,title))
            row=TableRow() if kind=='long' else TableRow(stylename=bodyrow);t.addElement(row)
            c=TableCell(stylename=cellstyle);row.addElement(c)
            target=paragraph(c,'\n'.join(f'Body_{i:02}' for i in range(18)) if kind=='long' else 'Body left')
            c=TableCell(stylename=cellstyle);row.addElement(c)
            short_id=paragraph(c,'\n'.join(f'Short_{i}' for i in range(10)) if kind=='long' else 'Body right',short)
            caption_id=paragraph(doc.text,'Caption first\nCaption second'+ ('\nCaption third' if kind=='long' else ''),caption)
        path=directory/f'keep-{kind}.odt';doc.save(str(path));load(str(path))
        cases.append({'kind':kind,'path':path,'expected':expected,'heading':heading_id,'caption':caption_id,
                      'headers':header_ids,'target':target,'short':short_id if kind=='long' else None,'table':0})
    return cases

def verify(case,test_bin_dir,directory):
    import json,os,subprocess
    path=case['path'];before=path.read_bytes()
    report=json.loads(subprocess.check_output([str(Path(test_bin_dir).resolve()/'calligra-native-cell-flow-test'),'--inspect',str(path)],
                    env={**os.environ,'LD_LIBRARY_PATH':'/usr/local/lib/domed/bitmap'},text=True))
    assert report['paragraphs']==case['expected'],report['paragraphs']
    assert not report['unsupported'],(case['kind'],report['unsupported'])
    pages=report['pages'];assert len(pages)>=2,(case['kind'],len(pages))
    def locations(source):return [i for i,p in enumerate(pages) if any(l['source']==source for l in p['lines'])]
    body=locations(case['target']);caption=locations(case['caption']);assert body and caption
    assert body[-1]==caption[0] and len(caption)==1,(case['kind'],body,caption)
    if case['heading'] is not None:
        heading=locations(case['heading'])
        assert (heading[-1]<body[0] if case['kind'] in ('forced','master-table') else heading[-1]==body[0]),(case['kind'],heading,body)
    if case['short'] is not None:assert locations(case['short'])==[0],locations(case['short'])
    for source,text in enumerate(case['expected']):
        if source in case['headers']:continue
        end=0;count=0
        for page in pages:
            for line in page['lines']:
                if line['source']!=source:continue
                assert line['begin']==end,(case['kind'],source,end,line)
                end=line['end'];count+=1
        assert count and end==len(text),(case['kind'],source,count,end,len(text))
    for page in pages:
        for line in page['lines']:
            assert not line['missing'],line
            assert line['y']>=page['top']-1e-6 and line['y']+line['height']<=page['height']-page['bottom']+1e-6,line
            if 'clip' in line:
                x,y,w,h=line['clip'];assert line['y']>=y-1e-6 and line['y']+line['height']<=y+h+1e-6,line
    assert path.read_bytes()==before
    out=Path(directory);out.mkdir(parents=True,exist_ok=True);(out/f'keep-{case["kind"]}.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS independent ODF',case['kind'],'keep chain, break precedence, exact source coverage and whole line bounds',flush=True)
    return report
if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path)
    parser.add_argument('--test-bin-dir',type=Path,default=Path(__file__).resolve().parents[2]/'build-native/fltk')
    args=parser.parse_args()
    for case in produce(args.directory):verify(case,args.test_bin_dir,args.directory)
