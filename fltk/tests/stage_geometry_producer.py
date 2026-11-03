#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODP geometry fixture and source-preservation oracle."""

from pathlib import Path
import math
import re
import subprocess
import os
import xml.etree.ElementTree as ET
import zipfile
from stage_move_producer import produce as movement_source, entries
from stage_objects_producer import normalized, D, V, T, P

MM = 72 / 25.4
UI = {"GroupedText": (80 * MM, 25 * MM, 34 * MM, 45 * MM, 15)}
MODEL = {
    "Rectangle": (50, 50, 100, 60, 30),
    "GroupedText": (230, 70, 95, 115, 15),
    "Image": (140, 110, 70, 40, 15),
    "Triangle": (240, 130, 90, 50, 0),
    "Line": (30, 205, 310, None, -8),
}


def produce(directory):
    path = movement_source(directory)
    original = entries(path)
    root = ET.fromstring(original["content.xml"])
    frame = root.find(".//" + D + "frame[@" + D + 'name="GroupedText"]')
    frame.set(D + "transform", "matrix(0 1 -1 0 0 0) translate(340pt 30pt)")
    frame.set(V + "width", "180pt")
    frame.set(V + "height", "60pt")
    paragraph = frame.find(".//" + T + "p")
    paragraph[
        -1
    ].tail = " layout wraps inside the resized frame while preserving the original font size and rich source runs."
    original["content.xml"] = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    target = path.with_name("geometry.odp")
    with zipfile.ZipFile(target, "w") as z:
        for name, value in original.items():
            z.writestr(
                name,
                value,
                compress_type=zipfile.ZIP_STORED
                if name == "mimetype"
                else zipfile.ZIP_DEFLATED,
            )
    path.unlink()
    return target


def length(value):
    match = re.fullmatch(r"([-+\d.e]+)(pt|cm|mm|in)?", value)
    assert match, value
    return (
        float(match[1])
        * {"pt": 1, "cm": 72 / 2.54, "mm": MM, "in": 72, None: 1}[match[2]]
    )


def close(a, b):
    assert math.isclose(a, b, rel_tol=1e-10, abs_tol=1e-7), (a, b)


def verify_saved(source, saved, edits=UI):
    a, b = entries(source), entries(saved)
    assert a.keys() == b.keys()
    for name in a:
        if name != "content.xml":
            assert a[name] == b[name], name
    before, after = ET.fromstring(a["content.xml"]), ET.fromstring(b["content.xml"])
    for name, (x, y, w, h, angle) in edits.items():
        old = next(n for n in before.iter() if n.get(D + "name") == name)
        new = next(n for n in after.iter() if n.get(D + "name") == name)
        radians = math.radians(angle)
        c, s = math.cos(radians), math.sin(radians)
        if name == "Line":
            for attr, want in [
                ("x1", x),
                ("y1", y),
                ("x2", x + w * c),
                ("y2", y + w * s),
            ]:
                close(length(new.get(V + attr)), want)
                new.set(V + attr, old.get(V + attr))
        else:
            close(length(new.get(V + "width")), w)
            close(length(new.get(V + "height")), h)
            new.set(V + "width", old.get(V + "width"))
            new.set(V + "height", old.get(V + "height"))
            matrix = re.fullmatch(
                r"matrix\(([-+\d.e ]+)\) translate\(([-+\d.e]+)pt ([-+\d.e]+)pt\)",
                new.get(D + "transform"),
            )
            assert matrix, new.get(D + "transform")
            m = list(map(float, matrix[1].split()))
            assert len(m) == 6
            for got, want in zip(m, [c, s, -s, c, 0, 0]):
                close(got, want)
            ox, oy = length(old.get(V + "x", "0")), length(old.get(V + "y", "0"))
            close(float(matrix[2]) + c * ox - s * oy, x)
            close(float(matrix[3]) + s * ox + c * oy, y)
            if D + "transform" in old.attrib:
                new.set(D + "transform", old.get(D + "transform"))
            else:
                new.attrib.pop(D + "transform")
            if old.tag == D + "frame":
                assert new.get(P + "user-transformed") == "true"
                if P + "user-transformed" in old.attrib:
                    new.set(P + "user-transformed", old.get(P + "user-transformed"))
                else:
                    new.attrib.pop(P + "user-transformed")
    assert normalized(before) == normalized(after), (
        "Unrelated XML, text runs, fonts or attributes changed"
    )
    print(
        "PASS independent ODP origin, size, rotation, exact rich source and package preservation",
        flush=True,
    )


def verify(source, bin_dir, art):
    art = Path(art)
    art.mkdir(parents=True, exist_ok=True)
    output = art / "model-saved.odp"
    result = subprocess.run(
        [
            str(Path(bin_dir).resolve() / "calligra-native-stage-geometry-test"),
            "--document",
            str(source),
            str(output),
        ],
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
        capture_output=True,
        text=True,
    )
    (art / "model.log").write_text(result.stdout + result.stderr)
    result.check_returncode()
    verify_saved(source, output, MODEL)
