#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODF stacking fixture, exact source oracle and overlap samples."""

from pathlib import Path
import json
import os
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from stage_move_producer import produce as movement_source, entries, DRAW as D, SVG as V
from stage_objects_producer import normalized

O = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
S = "{urn:oasis:names:tc:opendocument:xmlns:style:1.0}"
T = "{urn:oasis:names:tc:opendocument:xmlns:text:1.0}"
XML = "{http://www.w3.org/XML/1998/namespace}"
INITIAL_ROOT = ["C", "Group", "B", "Spare", "Opaque", "A", "Last"]
INITIAL_GROUP = ["Y", "Inner", "X"]
MODEL_ROOT = ["C", "Spare", "Opaque", "A", "Last", "B", "Group"]
MODEL_GROUP = ["X", "Y", "Inner"]


def produce(directory):
    source = movement_source(directory)
    parts = entries(source)
    root = ET.fromstring(parts["content.xml"])
    page = root.find(".//" + D + "page")
    for child in list(page):
        page.remove(child)
    page.set(D + "name", "Stacking")
    page.set(D + "nav-order", "a b x y n1 n2 last")
    styles = ET.fromstring(parts["styles.xml"])
    definitions = styles.find(O + "styles")
    for name, color in [("Green", "#20a060"), ("Orange", "#e0a020")]:
        style = ET.SubElement(
            definitions, S + "style", {S + "name": name, S + "family": "graphic"}
        )
        ET.SubElement(
            style,
            S + "graphic-properties",
            {D + "fill": "solid", D + "fill-color": color, D + "stroke": "none"},
        )

    def rectangle(
        parent, name, color, x, y, width, height, z=None, identifier=None, text=None
    ):
        attrs = {
            D + "name": name,
            D + "style-name": color,
            V + "x": f"{x}pt",
            V + "y": f"{y}pt",
            V + "width": f"{width}pt",
            V + "height": f"{height}pt",
        }
        if z is not None:
            attrs[D + "z-index"] = str(z)
        if identifier:
            attrs[XML + "id"] = identifier
        node = ET.SubElement(parent, D + "rect", attrs)
        if text:
            paragraph = ET.SubElement(node, T + "p", {T + "style-name": "Body"})
            paragraph.text = text + " "
            ET.SubElement(
                paragraph, T + "span", {T + "style-name": "Strong"}
            ).text = "Český"
        return node

    rectangle(page, "A", "Red", 40, 60, 180, 120, 20, "a", "A")
    group = ET.SubElement(page, D + "g", {D + "name": "Group", D + "z-index": "4"})
    rectangle(group, "X", "Blue", 80, 45, 120, 125, 7, "x", "X")
    rectangle(group, "Y", "Orange", 90, 100, 100, 80, 2, "y", "Y")
    inner = ET.SubElement(group, D + "g", {D + "name": "Inner", D + "z-index": "4"})
    rectangle(inner, "N1", "Blue", 300, 180, 50, 30, identifier="n1")
    rectangle(inner, "N2", "Red", 310, 190, 50, 30, 7, "n2")
    rectangle(page, "B", "Green", 120, 70, 140, 130, 4, "b", "B")
    rectangle(page, "C", "Blue", 20, 100, 100, 100)
    rectangle(page, "Spare", "Orange", 10, 25, 10, 10, 6)
    ET.SubElement(
        page,
        D + "connector",
        {
            D + "name": "Opaque",
            V + "viewBox": "0 0 100 100",
            D + "z-index": "8",
            D + "start-shape": "a",
            D + "end-shape": "b",
            V + "x1": "40pt",
            V + "y1": "60pt",
            V + "x2": "120pt",
            V + "y2": "70pt",
        },
    )
    rectangle(page, "Last", "Blue", 270, 50, 100, 100, 2147483647, "last")
    parts["content.xml"] = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    parts["styles.xml"] = ET.tostring(styles, encoding="utf-8", xml_declaration=True)
    target = source.with_name("stacking.odp")
    with zipfile.ZipFile(target, "w") as archive:
        for name, value in parts.items():
            archive.writestr(
                name,
                value,
                compress_type=zipfile.ZIP_STORED
                if name == "mimetype"
                else zipfile.ZIP_DEFLATED,
            )
    source.unlink()
    return target


def verify_saved(source, saved, root_order, group_order=INITIAL_GROUP):
    before, after = entries(source), entries(saved)
    assert before.keys() == after.keys()
    for name in before:
        if name != "content.xml":
            assert before[name] == after[name], name
    a, b = ET.fromstring(before["content.xml"]), ET.fromstring(after["content.xml"])
    contexts = [
        (
            a.find(".//" + D + "page"),
            b.find(".//" + D + "page"),
            root_order,
            INITIAL_ROOT,
        ),
        (
            a.find(".//" + D + "g[@" + D + 'name="Group"]'),
            b.find(".//" + D + "g[@" + D + 'name="Group"]'),
            group_order,
            INITIAL_GROUP,
        ),
    ]
    for old, new, expected, initial in contexts:
        if expected == initial:
            # An unchanged context must retain original spelling/absence, too.
            for first, second in zip(list(old), list(new)):
                assert first.get(D + "z-index") == second.get(D + "z-index")
            continue
        assert len(expected) == len(list(new))
        for node in new:
            assert node.get(D + "z-index") == str(expected.index(node.get(D + "name")))
        for first, second in zip(list(old), list(new)):
            if D + "z-index" in first.attrib:
                second.set(D + "z-index", first.get(D + "z-index"))
            else:
                second.attrib.pop(D + "z-index", None)
    assert normalized(a) == normalized(b), (
        "Changed child/navigation order, geometry, text, IDs, links, assets or unrelated source"
    )
    print(
        "PASS independent stacking indices, unchanged source/navigation/group/text and package bytes",
        flush=True,
    )


def verify(source, bin_dir, artifacts):
    art = Path(artifacts)
    art.mkdir(parents=True, exist_ok=True)
    saved = art / "model-saved.odp"
    run = subprocess.run(
        [
            str(Path(bin_dir).resolve() / "calligra-native-stage-order-test"),
            "--document",
            str(source),
            str(saved),
        ],
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
        capture_output=True,
        text=True,
    )
    (art / "model.log").write_text(run.stdout + run.stderr)
    run.check_returncode()
    verify_saved(source, saved, MODEL_ROOT, MODEL_GROUP)
    (art / "edits.json").write_text(
        json.dumps({"root_order": MODEL_ROOT, "group_order": MODEL_GROUP}, indent=2)
        + "\n"
    )
