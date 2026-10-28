#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check native enhanced paths, mirrored arrows and independent paint sets."""
import argparse
import json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('report',type=Path);a=p.parse_args()
with a.capture.open('rb') as f:
    assert f.readline().strip()==b'P6'
    width,height=map(int,f.readline().split());assert f.readline().strip()==b'255';pixels=f.read()
assert len(pixels)==width*height*3
s=json.loads(a.report.read_text())['scene'];x,y,w,h=s['viewport'];scale=min((w-32)/s['width'],(h-32)/s['height']);left=x+(w-s['width']*scale)/2;top=y+(h-s['height']*scale)/2
for name,point,expected in [
    ('modifier arrow body',(25,100),(68,136,238)),
    ('modifier arrow tip',(192,97),(68,136,238)),
    ('arrow exterior',(30,60),(255,255,255)),
    ('mirrored arrow tip',(238,97),(68,170,102)),
    ('mirrored arrow exterior',(400,60),(255,255,255)),
    ('F suppresses fill',(50,200),(255,255,255)),
    ('F retains stroke',(20,200),(187,51,68)),
    ('S retains fill',(160,200),(255,144,0)),
    ('S suppresses stroke',(202,200),(255,255,255)),
    ('separate stroke path',(100,222.5),(187,51,68)),
    ('even-odd custom hole',(260,210),(255,255,255)),
    ('single alpha path set',(235,185),(162,196,246)),
    ('independent overlapping alpha sets',(306,246),(115,166,242)),
    ('separate fill inside earlier hole',(285,235),(162,196,246)),
    ('modern clockwise ellipse',(375,245),(68,170,102)),
    ('modern ellipse removed quadrant',(425,200),(255,255,255)),
]:
    px,py=int(left+point[0]*scale),int(top+point[1]*scale);at=(py*width+px)*3;actual=tuple(pixels[at:at+3]);assert all(abs(a-b)<=2 for a,b in zip(actual,expected)),(name,actual,expected)
    print('PASS',name,actual)
