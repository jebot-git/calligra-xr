#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent connector target-edit fixture and full package/source oracle."""

import math
import re
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

from stage_connector_producer import FO, D, O, S, V, X, entries, normalized
from stage_connector_producer import produce as connector_document

INK = (143, 151, 175)
TARGET = (208, 224, 240)
T = "{urn:oasis:names:tc:opendocument:xmlns:text:1.0}"


def produce(directory):
    original = connector_document(directory)
    parts = entries(original)
    content, styles = (ET.fromstring(parts[n]) for n in ["content.xml", "styles.xml"])
    common = styles.find(O + "styles")
    style = ET.SubElement(
        common, S + "style", {S + "name": "EditTargetInk", S + "family": "graphic"}
    )
    ET.SubElement(
        style,
        S + "graphic-properties",
        {
            D + "fill": "solid",
            D + "fill-color": "#d0e0f0",
            D + "stroke": "solid",
            V + "stroke-width": "1pt",
            V + "stroke-color": "#203060",
        },
    )
    automatic = styles.find(O + "automatic-styles")
    layout = ET.SubElement(
        automatic, S + "page-layout", {S + "name": "ConnectorEditLayout"}
    )
    ET.SubElement(
        layout,
        S + "page-layout-properties",
        {FO + "page-width": "420pt", FO + "page-height": "300pt"},
    )
    ET.SubElement(
        styles.find(O + "master-styles"),
        S + "master-page",
        {
            S + "name": "ConnectorEditMaster",
            S + "page-layout-name": "ConnectorEditLayout",
        },
    )
    page = ET.Element(
        D + "page",
        {
            D + "name": "Connector dependency edits",
            D + "master-page-name": "ConnectorEditMaster",
        },
    )
    content.find(O + "body/" + O + "presentation").insert(0, page)

    def rect(parent, name, identity, x, y, w, h):
        return ET.SubElement(
            parent,
            D + "rect",
            {
                D + "name": name,
                X + "id": identity,
                D + "style-name": "EditTargetInk",
                V + "x": f"{x}pt",
                V + "y": f"{y}pt",
                V + "width": f"{w}pt",
                V + "height": f"{h}pt",
            },
        )

    def connector(name, identity, kind, start, end):
        attrs = {
            D + "name": name,
            X + "id": identity,
            D + "type": kind,
            D + "style-name": "ConnectorInk",
            V + "viewBox": "0 0 1000 1000",
        }
        for side, value, axis in [("start", start, "1"), ("end", end, "2")]:
            if isinstance(value[0], str):
                attrs[D + side + "-shape"] = value[0]
                attrs[D + side + "-glue-point"] = str(value[1])
            else:
                attrs[V + "x" + axis] = f"{value[0]}pt"
                attrs[V + "y" + axis] = f"{value[1]}pt"
        return ET.SubElement(page, D + "connector", attrs)

    # Both forward XML references and a transitive connector target are deliberate.
    connector("EditChain", "editChain", "line", ("editLink", 0), (380, 40))
    connector("EditLink", "editLink", "curve", ("editA", 1), ("editB", 3))
    connector("EditGroupLink", "editGroupLink", "line", ("editGroup", 2), ("editB", 2))
    connector(
        "EditEnhancedLink",
        "editEnhancedLink",
        "line",
        ("editEnhanced", 4),
        ("editB", 0),
    )
    rect(page, "EditA", "editA", 30, 50, 40, 40)
    rect(page, "EditB", "editB", 300, 170, 40, 40)
    group = ET.SubElement(
        page, D + "g", {D + "name": "EditGroup", X + "id": "editGroup"}
    )
    rect(group, "EditG1", "editG1", 120, 210, 30, 20)
    rect(group, "EditG2", "editG2", 170, 230, 30, 20)
    custom = ET.SubElement(
        page,
        D + "custom-shape",
        {
            D + "name": "EditEnhanced",
            X + "id": "editEnhanced",
            D + "style-name": "EditTargetInk",
            V + "x": "30pt",
            V + "y": "170pt",
            V + "width": "50pt",
            V + "height": "40pt",
        },
    )
    geometry = ET.SubElement(
        custom,
        D + "enhanced-geometry",
        {
            V + "viewBox": "0 0 100 100",
            D + "enhanced-path": "M 0 0 L 100 0 100 100 0 100 Z N",
            D + "modifiers": "100",
            D + "glue-points": "$0 50",
            D + "glue-point-leaving-directions": "0deg",
        },
    )
    ET.SubElement(
        geometry,
        D + "handle",
        {
            D + "handle-position": "$0 50",
            D + "handle-range-x-minimum": "0",
            D + "handle-range-x-maximum": "100",
        },
    )
    parts["content.xml"], parts["styles.xml"] = (
        ET.tostring(r, encoding="utf-8", xml_declaration=True)
        for r in [content, styles]
    )
    output = Path(directory) / "connector-edits.odp"
    with zipfile.ZipFile(output, "w") as archive:
        for name, data in parts.items():
            archive.writestr(
                name,
                data,
                compress_type=zipfile.ZIP_STORED
                if name == "mimetype"
                else zipfile.ZIP_DEFLATED,
            )
    return output


def cached(node, start, end, curve=False):
    for key, expected in zip(["x1", "y1", "x2", "y2"], [*start, *end]):
        raw = node.get(V + key)
        assert raw and raw.endswith("pt"), (key, raw)
        assert abs(float(raw[:-2]) - expected) < 1e-8, (key, raw, expected)
    data = node.get(V + "d")
    assert data and re.fullmatch(r"[MCL0-9.eE+\-\s]+", data), data
    command = "C" if curve else "L"
    tokens = data.split()
    assert tokens[0] == "M" and tokens[3] == command
    values = [float(v) * 72 / 2540 for v in tokens if v not in {"M", "C", "L"}]
    expected = [*start]
    if curve:
        length = math.hypot(end[0] - start[0], end[1] - start[1]) / 3
        expected += [start[0] + length, start[1], end[0] - length, end[1]]
    expected += [*end]
    assert len(values) == len(expected)
    assert all(abs(a - b) < 1e-8 for a, b in zip(values, expected)), (values, expected)
    assert len(node.get(V + "viewBox", "").split()) == 4
    for key in ["d", "viewBox", "x1", "y1", "x2", "y2"]:
        node.attrib.pop(V + key, None)


def verify_saved(source, saved, case=None):
    before, after = entries(source), entries(saved)
    assert before.keys() == after.keys()
    expected, actual = (ET.fromstring(p["content.xml"]) for p in [before, after])

    def named(root, name):
        return next(n for n in root.iter() if n.get(D + "name") == name)

    updates = {}
    if case == "move":
        named(expected, "EditA").set(
            D + "transform", "matrix(1 0 0 1 0 0) translate(10pt 0pt)"
        )
        updates = {
            "EditLink": ((80, 70), (300, 190), True),
            "EditChain": ((190, 70), (380, 40), False),
        }
    elif case == "resize":
        value = named(actual, "EditA").get(V + "width")
        assert value.endswith("pt") and abs(float(value[:-2]) - 60) < 1e-8
        named(expected, "EditA").set(V + "width", value)
        updates = {
            "EditLink": ((90, 70), (300, 190), True),
            "EditChain": ((195, 70), (380, 40), False),
        }
    elif case == "group":
        for name in ["EditG1", "EditG2"]:
            named(expected, name).set(
                D + "transform", "matrix(1 0 0 1 0 0) translate(20pt 0pt)"
            )
        updates = {"EditGroupLink": ((180, 250), (320, 210), False)}
    elif case == "enhanced":
        value = (
            named(actual, "EditEnhanced")
            .find(D + "enhanced-geometry")
            .get(D + "modifiers")
        )
        modifier = float(value)
        assert abs(modifier - 50) < 3, modifier
        named(expected, "EditEnhanced").find(D + "enhanced-geometry").set(
            D + "modifiers", value
        )
        updates = {"EditEnhancedLink": ((30 + modifier * 0.5, 190), (320, 170), False)}
    else:
        assert case is None, case
    for name, (start, end, curve) in updates.items():
        cached(named(actual, name), start, end, curve)
        original = named(expected, name)
        for key in ["d", "viewBox", "x1", "y1", "x2", "y2"]:
            original.attrib.pop(V + key, None)
    assert normalized(expected) == normalized(actual), case
    for part in before.keys() - {"content.xml"}:
        assert before[part] == after[part], part
