#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check colored native text and frame clipping in real PPM captures."""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('capture', type=Path)
parser.add_argument('report', type=Path)
args = parser.parse_args()
with args.capture.open('rb') as stream:
    assert stream.readline().strip() == b'P6'
    width, height = map(int, stream.readline().split())
    assert stream.readline().strip() == b'255'
    data = stream.read()
assert len(data) == width * height * 3
report = json.loads(args.report.read_text())
def pixel(x, y):
    at = (y * width + x) * 3
    return tuple(data[at:at+3])
if report['application'] == 'stage':
    scene = report['scene']; x, y, w, h = scene['viewport']
    scale = min((w-32)/scene['width'], (h-32)/scene['height'])
    left = x+(w-scene['width']*scale)/2; top = y+(h-scene['height']*scale)/2
    def area(x1, y1, x2, y2):
        return [pixel(x,y) for y in range(int(top+y1*scale), int(top+y2*scale))
                for x in range(int(left+x1*scale), int(left+x2*scale))]
    ink = area(32, 65, 290, 220)
    below = area(325, 131, 450, 225)
    assert below and all(p == (255,255,255) for p in below), 'Overflow escaped its frame'
    # Bottom alignment and padding put the first ink below point 255.
    assert all(p == (255,255,255) for p in area(32, 247, 285, 255)), 'Bottom text was top aligned'
    assert any(g > r+25 and g > b+15 for r,g,b in area(32, 256, 285, 275)), 'Bottom aligned text missing'
    print('PASS continuous overflow clipping and bottom alignment/padding')
else:
    # All colored runs appear in the document page, below the shell toolbar.
    ink = [pixel(x,y) for y in range(80,height-50) for x in range(60,width-60)]
    assert pixel(width//2, height//2) == (255,255,255), 'Default paper is not white'
for name, test in [
    ('red run', lambda r,g,b: r>150 and r>g+60 and r>b+50),
    ('green run', lambda r,g,b: g>r+25 and g>b+15),
    ('blue link', lambda r,g,b: b>r+50 and b>g+35),
    ('scoped purple', lambda r,g,b: b>g+30 and r>g+20 and b>r+10),
]:
    count = sum(test(*p) for p in ink)
    assert count >= 3, (name, count)
    print('PASS', name, count, 'colored pixels')
