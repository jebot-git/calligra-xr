#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Produce image-list ODT/ODP with independent odfpy and Pillow encoders."""
import argparse
import base64
from io import BytesIO
from pathlib import Path
from PIL import Image
from odf.opendocument import OpenDocumentText, OpenDocumentPresentation
from odf.style import PageLayout, PageLayoutProperties, MasterPage, ListLevelProperties
from odf.text import ListStyle, ListLevelStyleImage, List, ListItem, P
from odf.draw import Page, Frame, TextBox, Image as OdfImage
from odf.office import BinaryData

def fixtures(directory):
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    assets={}
    for name,color,mode,fmt in [('png',(220,20,40,128),'RGBA','PNG'),('jpeg',(20,180,40),'RGB','JPEG'),('gif',(240,190,20,255),'RGBA','GIF'),('bmp',(180,30,210),'RGB','BMP'),('inline',(20,170,210,255),'RGBA','PNG')]:
        image=Image.new(mode,(16,16),color)
        if name in ('png','gif'):
            for y in range(16):
                for x in range(8,16):image.putpixel((x,y),(0,0,0,0))
        out=BytesIO();image.save(out,format=fmt);assets[name]=out.getvalue()
    assets['svg']=b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><rect width="16" height="16" fill="#2030dd"/></svg>'
    assets['corrupt']=b'not a supported image'
    names=['png','jpeg','svg','gif','bmp','inline','missing','corrupt','png']
    for app,create,suffix in [('words',OpenDocumentText,'odt'),('stage',OpenDocumentPresentation,'odp')]:
        doc=create();layout=PageLayout(name='Paper');layout.addElement(PageLayoutProperties(pagewidth='600pt',pageheight='650pt',margin='30pt'));doc.automaticstyles.addElement(layout)
        master=MasterPage(name='Default',pagelayoutname=layout);doc.masterstyles.addElement(master)
        target=doc.text if app=='words' else None
        if app=='stage':
            page=Page(name='Image lists',masterpagename=master);doc.presentation.addElement(page)
            frame=Frame(x='30pt',y='30pt',width='540pt',height='580pt');page.addElement(frame)
            target=TextBox();frame.addElement(target)
        for name,data in assets.items():
            if name!='inline':doc.addPicture('Pictures/'+name+' image.'+('jpg' if name=='jpeg' else name),mediatype='image/'+name,content=data)
        for i,name in enumerate(names):
            style=ListStyle(name='Image'+str(i));level=ListLevelStyleImage(level=1)
            if name=='inline':
                value=base64.b64encode(assets[name]).decode();level.addElement(BinaryData(text='\n'.join(value[j:j+32] for j in range(0,len(value),32))))
            else:level.setAttribute('href','file:///must-not-read.png' if name=='missing' else './Pictures/'+name+'%20image.'+('jpg' if name=='jpeg' else name))
            level.addElement(ListLevelProperties(width='32pt',height='24pt',spacebefore='4pt',minlabelwidth='40pt',minlabeldistance='4pt',verticalpos='middle',verticalrel='line'))
            style.addElement(level);doc.styles.addElement(style)
            listing=List(stylename=style);item=ListItem();item.addElement(P(text=name.title()+' image bullet'));listing.addElement(item);target.addElement(listing)
        if app=='stage':
            frame=Frame(x='200pt',y='510pt',width='64pt',height='48pt');frame.addElement(OdfImage(href='./Pictures/png%20image.png'));page.addElement(frame)
        doc.save(str(directory/f'image-lists.{suffix}'))
    return assets

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path)
    fixtures(parser.parse_args().directory)
