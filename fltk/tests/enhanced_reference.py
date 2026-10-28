#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent arithmetic and enhanced ellipse geometry checks."""
import argparse
import json
import math
from pathlib import Path
import random
import subprocess

root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--test-bin-dir',type=Path,default=root/'build-native/fltk');a=p.parse_args()
binary=a.test_bin_dir/'calligra-native-enhanced-test'
cases=[('abs(-3)',3),('sqrt(81)',9),('sin(pi/6)',.5),('cos(pi/3)',.5),('tan(pi/4)',1),('atan(1)',math.pi/4),('atan2(-1,-1)',-3*math.pi/4),('min(3,8)',3),('max(3,8)',8),('if(-1,1/0,7)',7),('12/3*2',8),('?name-curve+?b',190),('?循環',7),('logwidth+logheight',7620),('-(width+height)*2',-360)]
rng=random.Random(20261028)
for i in range(50):
    x,y,z=[rng.uniform(1,90) for _ in range(3)]
    formula=f'sqrt({x:.12f})+sin({y:.12f})*max({z:.12f},$0)/({x:.12f}+2)'
    expected=math.sqrt(x)+math.sin(y)*max(z,40)/(x+2)
    cases.append((formula,expected))
for formula,expected in cases:
    actual=float(subprocess.check_output([str(binary),'--evaluate',formula]))
    assert math.isclose(actual,expected,rel_tol=1e-10,abs_tol=1e-10),(formula,actual,expected)
print('PASS',len(cases),'independent formula values')
def radial(rx,ry,angle):
    t=math.radians(angle);r=1/math.hypot(math.cos(t)/rx,math.sin(t)/ry)
    return complex(r*math.cos(t),r*math.sin(t))
def distance(z,a,b):
    d=b-a;t=max(0,min(1,((z-a).real*d.real+(z-a).imag*d.imag)/abs(d)**2)) if d else 0
    return abs(z-a-t*d)
paths=[]
for legacy in (False,True):
    data='U50 50 40 20 45 235 N'
    samples=[50+50j+(complex(40*math.cos(math.radians(45+190*i/4000)),-20*math.sin(math.radians(45+190*i/4000)))) if legacy else 50+50j+radial(40,20,45+190*i/4000) for i in range(4001)]
    paths.append((data,legacy,samples))
for swing in (90,-270,450,-450,720):
    center=60+60j-radial(40,20,30)
    paths.append((f'M60 60 G40 20 30 {swing} N',False,[center+radial(40,20,30+swing*i/8000) for i in range(8001)]))
for op,clockwise in [('B',False),('V',True)]:
    start=math.atan2((90-25)/25,(90-50)/50);end=math.atan2((10-25)/25,(20-50)/50)
    delta=(end-start)%(2*math.pi) if clockwise else -((start-end)%(2*math.pi))
    points=[50+25j+complex(50*math.cos(start+delta*i/4000),25*math.sin(start+delta*i/4000)) for i in range(4001)]
    paths.append((f'{op}0 0 100 50 90 90 20 10 N',False,points))
for data,legacy,samples in paths:
    native=json.loads(subprocess.check_output([str(binary),'--path',data,*(['legacy'] if legacy else [])]))
    edges=[]
    for contour in native['contours']:
        points=[complex(*p) for p in contour['points']];edges+=list(zip(points,points[1:]))
        if contour['closed'] and len(points)>1:edges.append((points[-1],points[0]))
    expected=(min(z.real for z in samples),min(z.imag for z in samples),max(z.real for z in samples),max(z.imag for z in samples))
    x,y,w,h=native['bounds'];actual=(x,y,x+w,y+h)
    assert max(abs(a-b) for a,b in zip(actual,expected))<.002,(data,legacy,actual,expected)
    worst=max(min(distance(z,a,b) for a,b in edges) for z in samples[::8])
    assert worst<.0201,(data,legacy,worst)
    print('PASS enhanced arc reference',data,'legacy' if legacy else 'modern',round(worst,8))
print('PASS',len(paths),'independent enhanced arc/bounds comparisons')
