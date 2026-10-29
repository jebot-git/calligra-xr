#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODF table painting, clipping, inherited fonts and merged cells."""
import argparse
import io
from PIL import Image
from pathlib import Path
from odf.opendocument import OpenDocumentText,OpenDocumentPresentation
from odf.style import Style,DefaultStyle,ParagraphProperties,TextProperties,TableProperties,TableCellProperties,TableRowProperties,TableColumnProperties,ListLevelProperties,TabStops,TabStop,PageLayout,PageLayoutProperties,MasterPage
from odf.table import Table,TableRow,TableCell,CoveredTableCell,TableHeaderRows,TableColumn
from odf.text import P,Span,LineBreak,Tab,ListStyle,ListLevelStyleImage,List,ListItem
from odf.draw import Page,Frame,TextBox

def produce(directory):
 directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
 for app,create,ext in [('words',OpenDocumentText,'odt'),('stage',OpenDocumentPresentation,'odp')]:
  doc=create();default=DefaultStyle(family='paragraph');default.addElement(ParagraphProperties(lineheight='120%',marginbottom='2pt'));default.addElement(TextProperties(fontfamily='DejaVu Sans',fontsize='12pt'));doc.styles.addElement(default)
  layout=PageLayout(name='Paper');layout.addElement(PageLayoutProperties(pagewidth='620pt',pageheight='420pt',margin='20pt'));doc.automaticstyles.addElement(layout);master=MasterPage(name='Default',pagelayoutname=layout);doc.masterstyles.addElement(master)
  def style(name,family,properties):
   s=Style(name=name,family=family);s.addElement(properties);doc.automaticstyles.addElement(s);return s
  red=style('Cell','table-cell',TableCellProperties(backgroundcolor='#ffe0c0',padding='5pt',border='1pt solid #882244'));red.addElement(TextProperties(color='#cc2040',fontsize='14pt'))
  blue=style('Header','table-cell',TableCellProperties(backgroundcolor='#c8d8ff',padding='5pt',border='2pt solid #2040aa'));blue.addElement(TextProperties(color='#2040aa',fontsize='18pt',fontweight='bold'))
  col=style('Column','table-column',TableColumnProperties(columnwidth='180pt'))
  full=style('Grid','table',TableProperties(width='540pt',align='center',bordermodel='collapsing',marginbottom='8pt'))
  fixed=style('Fixed','table-row',TableRowProperties(rowheight='28pt'))
  overflow=style('OverflowTab','paragraph',ParagraphProperties(marginright='-200pt'));stops=TabStops();stops.addElement(TabStop(position='260pt',type='left',leaderstyle='solid',leadertype='single',leaderwidth='2pt',leadercolor='#10aa20'));overflow.firstChild.addElement(stops)
  image=Image.new('RGB',(40,40),(20,220,20));image.paste((220,20,20),(0,0,40,20));encoded=io.BytesIO();image.save(encoded,format='PNG');doc.addPicture('Pictures/clip.png',mediatype='image/png',content=encoded.getvalue())
  listing=ListStyle(name='ClipImage');level=ListLevelStyleImage(level=1,href='./Pictures/clip.png');level.addElement(ListLevelProperties(width='40pt',height='40pt',spacebefore='0pt',minlabelwidth='44pt',minlabeldistance='0pt',verticalpos='top',verticalrel='line'));listing.addElement(level);doc.styles.addElement(listing)
  if app=='words':target=doc.text
  else:
   page=Page(name='Native tables',masterpagename=master);doc.presentation.addElement(page);frame=Frame(x='10pt',y='10pt',width='600pt',height='390pt');page.addElement(frame);target=TextBox();frame.addElement(target)
  target.addElement(P(text='Native tables'))
  table=Table(name='Grid',stylename=full);target.addElement(table)
  for _ in range(3):table.addElement(TableColumn(stylename=col))
  header=TableHeaderRows();table.addElement(header);row=TableRow();header.addElement(row);cell=TableCell(stylename=blue,numbercolumnsspanned=2);cell.addElement(P(text='Merged header'));row.addElement(cell);row.addElement(CoveredTableCell());cell=TableCell(stylename=blue);cell.addElement(P(text='Third'));row.addElement(cell)
  row=TableRow();table.addElement(row);cell=TableCell(stylename=red,numberrowsspanned=2);cell.addElement(P(text='Vertical merge'));cell.addElement(P(text='Second paragraph'));row.addElement(cell)
  for text in ['Middle','Right']:
   cell=TableCell(stylename=red)
   if text=='Middle':paragraph=P(stylename=overflow,text=text);paragraph.addElement(Tab());paragraph.addText('end')
   else:paragraph=P(text=text)
   cell.addElement(paragraph);row.addElement(cell)
  row=TableRow(stylename=fixed);table.addElement(row);row.addElement(CoveredTableCell())
  cell=TableCell(stylename=red);paragraph=P(text='Clipped');paragraph.addElement(LineBreak());paragraph.addText('long');paragraph.addElement(LineBreak());paragraph.addText('content');cell.addElement(paragraph);row.addElement(cell)
  cell=TableCell(stylename=red);bullets=List(stylename=listing);item=ListItem();item.addElement(P(text='Image'));bullets.addElement(item);cell.addElement(bullets);row.addElement(cell)
  target.addElement(P(text='Border patterns'))
  patterns=Table(name='Borders');target.addElement(patterns)
  small=style('Small','table-column',TableColumnProperties(columnwidth='60pt'));patterns.addElement(TableColumn(stylename=small,numbercolumnsrepeated=9));row=TableRow();patterns.addElement(row)
  for i,border in enumerate(['solid','double','dashed','dotted','groove','ridge','inset','outset','none']):
   s=style('Border'+str(i),'table-cell',TableCellProperties(padding='4pt',border='3pt '+border+' #7820cc',backgroundcolor='#f0e0ff'));s.addElement(TextProperties(fontsize='9pt',color='#7820cc'));cell=TableCell(stylename=s);cell.addElement(P(text=border));row.addElement(cell)
  target.addElement(P(text='After tables'))
  path=directory/f'tables.{ext}';doc.save(str(path));print(path)
  if app=='stage':
   frame.setAttribute('transform','translate(30pt 8pt) rotate(0.09)');doc.save(str(directory/'tables-rotated.odp'))
 return directory
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);produce(p.parse_args().directory)
