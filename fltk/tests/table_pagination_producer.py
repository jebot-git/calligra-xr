#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independently produced multi-page Words table for rendering/editing checks."""
from pathlib import Path
from odf.opendocument import OpenDocumentText
from odf.style import (Style, TextProperties, ParagraphProperties, TableProperties,
                       TableColumnProperties, TableRowProperties, TableCellProperties,
                       PageLayout, PageLayoutProperties, MasterPage)
from odf.table import Table, TableColumn, TableRow, TableCell, TableHeaderRows
from odf.text import P

def produce(directory):
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    doc=OpenDocumentText()
    page=PageLayout(name='Page')
    page.addElement(PageLayoutProperties(pagewidth='300pt',pageheight='260pt',margin='15pt'))
    doc.automaticstyles.addElement(page)
    doc.masterstyles.addElement(MasterPage(name='Standard',pagelayoutname=page))
    def style(name,family,properties):
        s=Style(name=name,family=family);s.addElement(properties);doc.automaticstyles.addElement(s);return s
    para=style('P','paragraph',TextProperties(fontsize='10pt',fontfamily='DejaVu Sans',color='#203040'))
    para.addElement(ParagraphProperties(lineheight='100%',margintop='0pt',marginbottom='0pt'))
    table_style=style('Grid','table',TableProperties(width='270pt',align='margins'))
    col=style('Column','table-column',TableColumnProperties(columnwidth='135pt'))
    row=style('Row','table-row',TableRowProperties(rowheight='28pt'))
    header_row=style('HeaderRow','table-row',TableRowProperties(rowheight='24pt'))
    body=style('Body','table-cell',TableCellProperties(padding='3pt',border='1pt solid #203040',backgroundcolor='#fff0cc'))
    header=style('Header','table-cell',TableCellProperties(padding='3pt',border='1pt solid #203040',backgroundcolor='#c8d8ff'))
    doc.text.addElement(P(stylename=para,text='Before'))
    table=Table(name='Paging',stylename=table_style)
    for _ in range(2):table.addElement(TableColumn(stylename=col))
    headers=TableHeaderRows();r=TableRow(stylename=header_row)
    for title in ('Left header','Right header'):
        cell=TableCell(stylename=header);cell.addElement(P(stylename=para,text=title));r.addElement(cell)
    headers.addElement(r);table.addElement(headers)
    for i in range(1,19):
        r=TableRow(stylename=row)
        for prefix in ('L','R'):
            cell=TableCell(stylename=body);cell.addElement(P(stylename=para,text=f'{prefix}{i:02}'));r.addElement(cell)
        table.addElement(r)
    doc.text.addElement(table);doc.text.addElement(P(stylename=para,text='After'))
    path=directory/'table-pages.odt';doc.save(str(path));return path
if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path)
    print(produce(parser.parse_args().directory))
