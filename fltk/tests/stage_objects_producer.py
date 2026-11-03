#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODP sources and raw XML oracle for Stage object operations."""

from pathlib import Path
import json
import math
import subprocess
import os
import xml.etree.ElementTree as ET
import zipfile
from stage_move_producer import produce as movement_source, entries

D = "{urn:oasis:names:tc:opendocument:xmlns:drawing:1.0}"
O = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
S = "{urn:oasis:names:tc:opendocument:xmlns:style:1.0}"
V = "{urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0}"
T = "{urn:oasis:names:tc:opendocument:xmlns:text:1.0}"
X = "{http://www.w3.org/XML/1998/namespace}"
P = "{urn:oasis:names:tc:opendocument:xmlns:presentation:1.0}"


def produce(directory):
    path = movement_source(directory)
    with zipfile.ZipFile(path) as z:
        original = [(item, z.read(item.filename)) for item in z.infolist()]
    with zipfile.ZipFile(path, "w") as z:
        for item, data in original:
            if item.filename == "content.xml":
                root = ET.fromstring(data)
                page = root.find(".//" + D + "page")
                ids = []
                for i, shape in enumerate(page, 1):
                    identifier = f"original-{i}"
                    shape.set(X + "id", identifier)
                    ids.append(identifier)
                page.set(D + "nav-order", " ".join(ids))
                data = ET.tostring(root, encoding="utf-8", xml_declaration=True)
            z.writestr(item, data)
    output = path.with_name("objects.odp")
    path.rename(output)
    return output


def text(node):
    value = node.text or ""
    for child in node:
        if child.tag == T + "s":
            value += " " * int(child.get(T + "c", "1"))
        elif child.tag == T + "tab":
            value += "\t"
        elif child.tag == T + "line-break":
            value += "\n"
        else:
            value += text(child)
        value += child.tail or ""
    return value


def normalized(node):
    return (
        node.tag,
        sorted(node.attrib.items()),
        node.text or "",
        node.tail or "",
        [normalized(c) for c in node],
    )


def verify_saved(original, saved, descriptors):
    a, b = entries(original), entries(saved)
    assert a.keys() == b.keys()
    for name in a:
        if name != "content.xml":
            assert a[name] == b[name], name
    before, after = ET.fromstring(a["content.xml"]), ET.fromstring(b["content.xml"])
    old_page = before.find(".//" + D + "page")
    new_page = after.find(".//" + D + "page")
    removed = old_page.find(D + "rect[@" + D + 'name="Rectangle"]')
    removed_id = removed.get(X + "id")
    old_page.remove(removed)
    expected_nav = [v for v in old_page.get(D + "nav-order").split() if v != removed_id]
    ids = []
    auto = after.find(O + "automatic-styles")
    assert auto is not None
    for i, (kind, rect, want_text) in enumerate(descriptors, 1):
        node = new_page.find(D + kind + "[@" + D + 'name="NativeObject' + str(i) + '"]')
        assert node is not None, (kind, i)
        identifier = node.get(X + "id")
        assert identifier == f"native-object-{i}" and identifier not in ids
        ids.append(identifier)
        style_name = node.get(D + "style-name")
        assert style_name == f"NativeGraphic{i}"
        style = auto.find(S + "style[@" + S + 'name="' + style_name + '"]')
        assert style is not None and style.get(S + "family") == "graphic"
        paint = style.find(S + "graphic-properties")
        assert paint.get(D + "fill") == (
            "none" if kind in ("line", "frame") else "solid"
        )
        assert paint.get(D + "stroke") == ("none" if kind == "frame" else "solid")
        if kind in ("rect", "ellipse"):
            assert paint.get(D + "fill-color") == "#4477aa"
        coordinates = (
            {
                "x1": rect[0],
                "y1": rect[1],
                "x2": rect[0] + rect[2],
                "y2": rect[1] + rect[3],
            }
            if kind == "line"
            else dict(zip(("x", "y", "width", "height"), rect))
        )
        # Arrow nudges serialize the shape transform while keeping SVG geometry.
        transform = node.get(D + "transform")
        dx = dy = 0
        if transform:
            import re

            m = re.fullmatch(
                r"matrix\(1 0 0 1 0 0\) translate\(([-+\d.e]+)pt ([-+\d.e]+)pt\)",
                transform,
            )
            assert m, transform
            dx, dy = map(float, m.groups())
        for name, want in coordinates.items():
            value = node.get(V + name)
            assert value and value.endswith("pt")
            actual = float(value[:-2]) + (
                dx
                if name in ("x", "x1", "x2")
                else dy
                if name in ("y", "y1", "y2")
                else 0
            )
            assert math.isclose(actual, want, abs_tol=1e-9), (name, actual, want)
        if kind == "frame":
            assert node.get(P + "user-transformed") == "true"
        container = node.find(D + "text-box") if kind == "frame" else node
        paragraphs = container.findall(T + "p")
        actual = "\n".join(text(p) for p in paragraphs)
        assert actual == want_text, (i, actual, want_text)
        assert (not paragraphs) == (kind == "line")
        assert int(node.get(D + "z-index")) >= 0
        auto.remove(style)
        new_page.remove(node)
    assert new_page.get(D + "nav-order").split() == expected_nav + ids
    old_page.set(D + "nav-order", new_page.get(D + "nav-order"))
    if before.find(O + "automatic-styles") is None:
        assert len(auto) == 0
        after.remove(auto)
    assert normalized(before) == normalized(after), "Unrelated presentation XML changed"
    print(
        "PASS independent object geometry/styles/text/IDs/nav-order and source-package preservation",
        flush=True,
    )


MODEL = [
    ("rect", (250, 50, 60, 45), "RECT"),
    ("ellipse", (245, 120, 60, 40), "ELLIPSE"),
    ("line", (30, 205, 190, -20), ""),
    ("frame", (30, 30, 185, 35), "Native Stage text"),
]
UI = [
    ("rect", (100, 60, 120, 60), ""),
    ("ellipse", (240, 130, 120, 60), ""),
    ("line", (80, 168, 240, 0), ""),
    ("frame", (80, 108, 240, 48), "x"),
]


def verify(source, test_bin_dir, directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    saved = directory / "native-objects.odp"
    before = Path(source).read_bytes()
    subprocess.run(
        [
            str(Path(test_bin_dir).resolve() / "calligra-native-stage-objects-test"),
            "--document",
            str(Path(source).resolve()),
            str(saved.resolve()),
        ],
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
        check=True,
    )
    verify_saved(source, saved, MODEL)
    assert Path(source).read_bytes() == before
    (directory / "results.json").write_text(
        json.dumps(
            dict(
                native_object_operations=True,
                source_preserved=True,
                raw_xml_verified=True,
                undo_redo=True,
                save_reopen=True,
            ),
            indent=2,
        )
        + "\n"
    )
