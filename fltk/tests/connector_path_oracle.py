#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare affine SVG serialization with independent CairoSVG filled geometry."""

import argparse
import io
import itertools
import json
import os
import subprocess
from pathlib import Path
from xml.sax.saxutils import escape

import cairosvg
from PIL import Image, ImageChops

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--test-bin", required=True, type=Path)
p.add_argument("--artifacts", required=True, type=Path)
a = p.parse_args()
art = a.artifacts.resolve()
art.mkdir(parents=True, exist_ok=True)
matrices = [
    [1, 0, 0, 1, 0, 0],
    [2, 0, 0, 3, 7, 11],
    [-2, 0, 0, 3, 7, 11],
    [1.2, 0.7, -0.3, 2.1, -10, 20],
    [0, 1, -1, 0, 0, 0],
    [1, 2, 3, 6, 4, -5],
    [0, 0, 0, 0, 10, -20],
]
cases = [
    {
        "matrix": m,
        "data": f"M13 7 A40 20 {rotation} {large} {sweep} 60 30 "
        "C70 0 80 20 90 40 Q110 0 120 30 L150 40 Z "
        "M-80 -80 L-40 -80 L-60 -20 Z",
    }
    for m, large, sweep, rotation in itertools.product(
        matrices, [0, 1], [0, 1], [0, 30, 90]
    )
]
run = subprocess.run(
    [str(a.test_bin.resolve()), "--format-path"],
    input="".join(json.dumps(c) + "\n" for c in cases),
    text=True,
    capture_output=True,
    check=True,
    env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
)
outputs = [json.loads(line)["data"] for line in run.stdout.splitlines()]
assert len(outputs) == len(cases)
results = []


def raster(data, transform):
    svg = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="-350 -350 1000 1000">'
        '<rect x="-350" y="-350" width="1000" height="1000" fill="white"/>'
        f'<path d="{escape(data)}" transform="{escape(transform)}" '
        'fill="#203060" fill-opacity="0.5" fill-rule="evenodd"/></svg>'
    )
    return Image.open(
        io.BytesIO(
            cairosvg.svg2png(
                bytestring=svg.encode(), output_width=1024, output_height=1024
            )
        )
    ).convert("RGB")


for i, (case, output) in enumerate(zip(cases, outputs)):
    transform = "matrix(" + " ".join(str(x) for x in case["matrix"]) + ")"
    original, serialized = raster(case["data"], transform), raster(output, "")
    difference = ImageChops.difference(original, serialized).convert("L")
    histogram = difference.histogram()
    mean = sum(i * count for i, count in enumerate(histogram)) / (1024 * 1024)
    differing = sum(histogram[5:])
    # Cairo's cubic approximation of an SVG arc can change decomposition after
    # affine axis normalization. Allow subpixel edge differences, never a
    # changed filled region. Keep every raster/result available for inspection.
    original.save(art / f"case-{i:03d}-original.png")
    serialized.save(art / f"case-{i:03d}-serialized.png")
    difference.save(art / f"case-{i:03d}-difference.png")
    results.append(
        {
            **case,
            "serialized": output,
            "mean_pixel_difference": mean,
            "pixels_differing_over_four": differing,
        }
    )
    assert mean < 0.12 and differing < 1200, (i, case, mean, differing)
(art / "results.json").write_text(
    json.dumps(
        {
            "independent_renderer": "CairoSVG/cairo",
            "cases": len(results),
            "scope": "Filled geometry under affine/reflected/singular transforms; stroke/dash/marker semantics need separate evidence",
            "maximum_mean_pixel_difference": max(
                r["mean_pixel_difference"] for r in results
            ),
            "maximum_pixels_differing_over_four": max(
                r["pixels_differing_over_four"] for r in results
            ),
            "results": results,
        },
        indent=2,
    )
    + "\n"
)
print(f"PASS {len(results)} independent CairoSVG affine serialization comparisons")
