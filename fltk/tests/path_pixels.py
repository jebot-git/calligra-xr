#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check rendered native path fills, holes and curved primitive geometry."""
import argparse
import json
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('capture',type=Path);p.add_argument('report',type=Path);a=p.parse_args()
with a.capture.open('rb') as f:
    assert f.readline().strip()==b'P6'
    width,height=map(int,f.readline().split());assert f.readline().strip()==b'255'
    pixels=f.read()
assert len(pixels)==width*height*3
scene=json.loads(a.report.read_text())['scene'];x,y,w,h=scene['viewport']
scale=min((w-32)/scene['width'],(h-32)/scene['height'])
left=x+(w-scene['width']*scale)/2;top=y+(h-scene['height']*scale)/2
for name,point,color in [
    ('cubic fill',(70,100),(68,136,238)),
    ('cubic exterior',(25,50),(255,255,255)),
    ('compound exterior contour',(150,50),(68,136,238)),
    ('even-odd hole',(195,95),(255,255,255)),
    ('rounded corner exterior',(276,46),(255,255,255)),
    ('rounded rectangle fill',(325,95),(68,170,102)),
    ('pie fill',(50,210),(255,144,0)),
    ('pie removed quadrant',(95,245),(255,255,255)),
    ('ellipse chord fill',(195,195),(68,170,102)),
    ('ellipse chord exterior',(195,245),(255,255,255)),
    ('implicit closing fill',(325,240),(187,51,68)),
    ('nonzero winding overlap',(427,220),(68,136,238)),
]:
    px,py=int(left+point[0]*scale),int(top+point[1]*scale);at=(py*width+px)*3
    actual=tuple(pixels[at:at+3]);assert all(abs(c-d)<=2 for c,d in zip(color,actual)),(name,color,actual)
    print('PASS',name,actual)
