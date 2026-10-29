#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent odfpy table grid, repeated content and nested source identities."""
import argparse,json,os,subprocess
from pathlib import Path
from odf.opendocument import OpenDocumentText,load
from odf.style import Style,TableCellProperties,TableColumnProperties
from odf.table import Table,TableRow,TableCell,CoveredTableCell,TableHeaderRows,TableColumn
from odf.text import P
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('--test-bin-dir',type=Path,default=ROOT/'build-native/fltk');a=p.parse_args();a.directory.mkdir(parents=True,exist_ok=True)
doc=OpenDocumentText();doc.text.addElement(P(text='Before'))
blue=Style(name='Blue',family='table-cell');blue.addElement(TableCellProperties(backgroundcolor='#2244aa',padding='2pt',border='1pt solid #001122'));doc.automaticstyles.addElement(blue)
col=Style(name='Column',family='table-column');col.addElement(TableColumnProperties(columnwidth='90pt'));doc.automaticstyles.addElement(col)
table=Table(name='External');doc.text.addElement(table)
for _ in range(3):table.addElement(TableColumn(stylename=col))
headers=TableHeaderRows();table.addElement(headers);row=TableRow();headers.addElement(row)
cell=TableCell(numbercolumnsspanned=2,stylename=blue);cell.addElement(P(text='Heading'));row.addElement(cell);row.addElement(CoveredTableCell());cell=TableCell();cell.addElement(P(text='Third'));row.addElement(cell)
row=TableRow();table.addElement(row);cell=TableCell(numberrowsspanned=2,numbercolumnsspanned=2);cell.addElement(P(text='Span'));row.addElement(cell);row.addElement(CoveredTableCell());cell=TableCell();cell.addElement(P(text='Right'));row.addElement(cell)
row=TableRow();table.addElement(row);row.addElement(CoveredTableCell(numbercolumnsrepeated=2));cell=TableCell();cell.addElement(P(text='Bottom'));row.addElement(cell)
row=TableRow(numberrowsrepeated=2);table.addElement(row);cell=TableCell(numbercolumnsrepeated=3);row.addElement(cell);cell.addElement(P(text='Repeated'))
nested=Table(name='Nested');cell.addElement(nested);nr=TableRow();nested.addElement(nr);nc=TableCell();nr.addElement(nc);nc.addElement(P(text='Nested body'));cell.addElement(P(text='Last'))
doc.text.addElement(P(text='After'));path=a.directory/'external-tables.odt';doc.save(str(path));load(str(path));before=path.read_bytes()
report=json.loads(subprocess.check_output([str(a.test_bin_dir.resolve()/'calligra-native-table-model-test'),'--inspect',str(path)],text=True,env={**os.environ,'LD_LIBRARY_PATH':'/usr/local/lib/domed/bitmap'}))
assert report['paragraphs']==['Before','Heading','Third','Span','Right','Bottom','Repeated','Nested body','Last','After'],report
assert report['blocks']==[{'kind':'paragraph','index':0},{'kind':'table','index':1},{'kind':'paragraph','index':9}]
nested,table=report['tables'];assert nested['name']=='Nested' and nested['rows']==nested['columns']==1
assert table['rows']==5 and table['columns']==3 and len(table['cells'])==11 and table['headers']==[True,False,False,False,False]
assert table['cells'][0]['background']==[34,68,170] and table['cells'][2]['rows']==table['cells'][2]['columns']==2
for cell in table['cells'][5:]:assert cell['blocks']==[{'kind':'paragraph','index':6},{'kind':'table','index':0},{'kind':'paragraph','index':8}]
assert path.read_bytes()==before
(a.directory/'external-tables-model.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS independently produced ODT grid, headers, merged/repeated/nested cells, source identities and unchanged package',flush=True)
