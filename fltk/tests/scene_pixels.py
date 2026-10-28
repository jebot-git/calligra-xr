#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check captured scene fixture pixels independently of renderer diagnostics."""
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
    pixels = stream.read()
assert len(pixels) == width * height * 3
scene = json.loads(args.report.read_text())['scene']
x, y, w, h = scene['viewport']
scale = min((w - 32) / scene['width'], (h - 32) / scene['height'])
left = x + (w - scene['width'] * scale) / 2
top = y + (h - scene['height'] * scale) / 2
for name, point, expected in [
    ('master paint', (5, 15), (36, 70, 110)),
    ('master background', (10, 50), (232, 242, 255)),
    ('rectangle', (30, 70), (255, 144, 0)),
    ('alpha and stacking', (95, 95), (162, 140, 119)),
    ('alpha over background', (125, 100), (150, 189, 247)),
    ('concave polygon', (150, 145), (68, 170, 102)),
    ('concave notch', (185, 180), (232, 242, 255)),
    ('transformed image', (251, 147), (32, 128, 224)),
]:
    px, py = int(left + point[0] * scale), int(top + point[1] * scale)
    offset = (py * width + px) * 3
    actual = tuple(pixels[offset:offset + 3])
    assert all(abs(a-b) <= 2 for a, b in zip(actual, expected)), (name, actual, expected)
    print('PASS', name, actual)
