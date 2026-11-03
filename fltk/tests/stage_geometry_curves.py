#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare native non-viewport resizing against independent SVG curve samples.

These deliberately non-conforming legacy ODP inputs lack the required viewBox;
they test accepted extension handling, not strict ODF schema compliance.
"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from svgpathtools import parse_path
from stage_geometry_producer import produce, entries, close
from stage_objects_producer import D, V, normalized

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("directory", type=Path)
p.add_argument("--bin-dir", required=True, type=Path)
a = p.parse_args()
art = a.directory.resolve()
art.mkdir(parents=True, exist_ok=True)
base = produce(art / "fixture")
original = entries(base)
results = []
for large, sweep, rx, ry, angle in [
    (l, s, 30, 15, 27) for l in [0, 1] for s in [0, 1]
] + [(1, 0, 30, 30, -37), (0, 1, 40, 8, 83)]:
    root = ET.fromstring(original["content.xml"])
    page = root.find(".//" + D + "page")
    d = f"M0 0 L30000000 0 A{rx} {ry} {angle} {large} {sweep} 30000040 20 C30000050 35 30000060 -10 30000070 5 Q30000080 40 30000090 0 Z"
    node = ET.SubElement(page, D + "path", {D + "name": "LargeArc", V + "d": d})
    case = f"arc-{large}-{sweep}-{rx}-{ry}-{angle}"
    source = art / (case + ".odp")
    saved = art / (case + "-saved.odp")
    with zipfile.ZipFile(source, "w") as z:
        for name, value in original.items():
            z.writestr(
                name,
                ET.tostring(root, encoding="utf-8", xml_declaration=True)
                if name == "content.xml"
                else value,
            )
    run = subprocess.run(
        [
            str(a.bin_dir.resolve() / "calligra-native-stage-geometry-test"),
            "--curve-document",
            str(source),
            str(saved),
        ],
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
        capture_output=True,
        text=True,
    )
    (art / (case + ".log")).write_text(run.stdout + run.stderr)
    run.check_returncode()
    archive = entries(saved)
    out = ET.fromstring(archive["content.xml"])
    changed = out.find(".//" + D + "path[@" + D + 'name="LargeArc"]')
    assert V + "viewBox" not in changed.attrib
    first, second = parse_path(d), parse_path(changed.get(V + "d"))
    xmin, xmax, ymin, ymax = first.bbox()
    sx, sy = 3000000 / (xmax - xmin), 5000 / (ymax - ymin)
    assert len(first) == len(second)
    errors = []
    for old, new in zip(first, second):
        for i in range(101):
            z = old.point(i / 100)
            got = new.point(i / 100)
            want = complex(z.real * sx, z.imag * sy)
            errors.append(abs(got - want))
    assert max(errors) < 0.00002, (case, max(errors))
    close(float(changed.get(V + "width")[:-2]), 3000000)
    close(float(changed.get(V + "height")[:-2]), 5000)
    changed.attrib.pop(V + "width")
    changed.attrib.pop(V + "height")
    changed.set(V + "d", d)
    assert normalized(root) == normalized(out)
    for name in original:
        if name != "content.xml":
            assert archive[name] == original[name]
    results.append(dict(case=case, samples=len(errors), max_absolute_error=max(errors)))
(art / "results.json").write_text(json.dumps(results, indent=2) + "\n")
print(
    "PASS six independent SVG arc/Bezier affine-resize oracles and source preservation",
    flush=True,
)
