#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare native curves/bounds against independent svgpathtools geometry."""
import argparse
import json
import math
from pathlib import Path
import random
import subprocess
from svgpathtools import parse_path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--test-bin-dir', type=Path, default=root/'build-native/fltk')
args = parser.parse_args()
cases = [
    'M0 0C0 100 100 100 100 0S200-100 200 0',
    'M10 20q40 100 80 0t80 0',
    'M0 0L100 0 100 100zM30 30v20h20z',
    'M0 0C100 0-100 0 0 0',
    'M0 0A10 10 0 0 1 100 0',
    'M0 0a0 30 0 0 1 100 50',
    'M0 0A-80-50 35 0 1 60 40',
    'M.5.6L1e2-2e1',
]
cases += [f'M0 0A80 50 35 {large} {sweep} 60 40' for large in (0,1) for sweep in (0,1)]
rng = random.Random(20261028)
for i in range(32):
    numbers = [rng.uniform(-90,140) for _ in range(6)]
    cases.append('M0 0C'+' '.join(f'{n:.9f}' for n in numbers))
matrix = (1.2,.35,-.2,.8,12,-17)
def transformed(z):
    a,b,c,d,e,f = matrix
    return complex(a*z.real+c*z.imag+e,b*z.real+d*z.imag+f)
def distance(z, a, b):
    v=b-a
    t=max(0,min(1,((z-a).real*v.real+(z-a).imag*v.imag)/abs(v)**2)) if v else 0
    return abs(z-a-t*v)
for index, data in enumerate(cases):
    native = json.loads(subprocess.check_output([str(args.test_bin_dir/'calligra-native-path-test'), '--dump', data,
        'matrix(1.2 .35 -.2 .8 12 -17)']))
    reference = parse_path(data)
    edges=[]
    for contour in native['contours']:
        points=[complex(*p) for p in contour['points']]
        edges += list(zip(points,points[1:]))
        if contour['closed'] and len(points)>1:edges.append((points[-1],points[0]))
    assert edges, data
    sampled=[transformed(s.point(i/2000)) for s in reference for i in range(2001)]
    expected=(min(z.real for z in sampled),min(z.imag for z in sampled),max(z.real for z in sampled),max(z.imag for z in sampled))
    x,y,w,h=native['bounds'];actual=(x,y,x+w,y+h)
    assert max(abs(a-b) for a,b in zip(actual,expected))<.002,(data,actual,expected)
    # Dense samples independently check the quarter-pixel adaptive geometry at
    # a much stricter .02-unit tolerance, including rotated/sheared arcs.
    worst=max(min(distance(z,a,b) for a,b in edges) for z in sampled[::4])
    assert worst<.0201,(data,worst)
    print('PASS reference path',index,'bounds and curve error',round(worst,8))
print('PASS',len(cases),'independent SVG path comparisons')
