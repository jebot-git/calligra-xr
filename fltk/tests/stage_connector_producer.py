#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODF1.2 connector scene corpus and exact source/export oracle."""

import math
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

from stage_layers_producer import D, O, S, V, entries
from stage_marker_producer import produce as marker_document
from stage_objects_producer import normalized
from stage_stroke_producer import INK

__all__ = ["INK", "entries", "normalized", "probes", "produce", "verify_saved"]
X = "{http://www.w3.org/XML/1998/namespace}"
FO = "{urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0}"


def produce(directory):
    source = marker_document(directory)
    parts = entries(source)
    content, styles = (
        ET.fromstring(parts["content.xml"]),
        ET.fromstring(parts["styles.xml"]),
    )
    common = styles.find(O + "styles")
    for name in ["ConnectorInk", "ConnectorArrow", "HiddenBigFont"]:
        style = ET.SubElement(
            common, S + "style", {S + "name": name, S + "family": "graphic"}
        )
        props = {
            D + "fill": "none",
            D + "stroke": "solid",
            V + "stroke-color": "#203060",
            V + "stroke-width": "4pt",
            V + "stroke-opacity": "50%",
            V + "stroke-linecap": "butt",
        }
        if name == "ConnectorArrow":
            props |= {
                D + "marker-end": "Arrow",
                D + "marker-end-width": "10pt",
                D + "marker-end-center": "false",
            }
        ET.SubElement(style, S + "graphic-properties", props)
        if name == "HiddenBigFont":
            ET.SubElement(style, S + "text-properties", {FO + "font-size": "1100pt"})
    pages = content.findall(".//" + D + "page")
    for page in pages[:2]:
        for child in list(page):
            page.remove(child)
        page.attrib.pop(D + "nav-order", None)

    def connector(page, name, kind, start, end, path=None, style="ConnectorInk"):
        attrs = {
            D + "name": name,
            X + "id": "Connector" + name,
            D + "type": kind,
            D + "style-name": style,
            V + "viewBox": "0 0 1000 1000",
        }
        for side, value, axis in [("start", start, "1"), ("end", end, "2")]:
            if isinstance(value[0], str):
                attrs[D + side + "-shape"] = value[0]
                attrs[D + side + "-glue-point"] = str(value[1])
            else:
                attrs[V + "x" + axis] = f"{value[0]}pt"
                attrs[V + "y" + axis] = f"{value[1]}pt"
        if path:
            attrs[V + "d"] = path
        return ET.SubElement(page, D + "connector", attrs)

    for name, kind, start, end in [
        ("FreeLine", "line", (25, 45), (175, 95)),
        ("FreeLines", "lines", (225, 45), (375, 95)),
        ("FreeCurve", "curve", (25, 145), (175, 195)),
        ("FreeStandard", "standard", (225, 145), (375, 195)),
    ]:
        connector(pages[0], name, kind, start, end)
    connector(pages[1], "Attached", "curve", ("targetA", 4), ("targetGroup", 4))
    connector(
        pages[1],
        "EnhancedLink",
        "line",
        ("targetEnhanced", 4),
        (190, 210),
        style="ConnectorArrow",
    )
    connector(
        pages[1],
        "Supplied",
        "curve",
        (230, 160),
        (360, 210),
        "M10 20C10 -20 110 80 110 20",
    )
    a = ET.SubElement(
        pages[1],
        D + "rect",
        {
            D + "name": "HiddenA",
            X + "id": "targetA",
            D + "layer": "Never",
            D + "style-name": "HiddenBigFont",
            V + "x": "30pt",
            V + "y": "40pt",
            V + "width": "60pt",
            V + "height": "40pt",
        },
    )
    ET.SubElement(
        a,
        D + "glue-point",
        {
            D + "id": "4",
            V + "x": "60pt",
            V + "y": "20pt",
            D + "align": "top-left",
            D + "escape-direction": "right",
        },
    )
    group = ET.SubElement(
        pages[1],
        D + "g",
        {
            D + "name": "HiddenGroup",
            X + "id": "targetGroup",
        },
    )
    ET.SubElement(
        group,
        D + "glue-point",
        {D + "id": "4", V + "x": "-50%", V + "y": "0%", D + "escape-direction": "left"},
    )
    for x, y in [(0, 0), (50, 30)]:
        ET.SubElement(
            group,
            D + "rect",
            {
                D + "layer": "Never",
                V + "x": f"{220 + x}pt",
                V + "y": f"{30 + y}pt",
                V + "width": "40pt",
                V + "height": "40pt",
            },
        )
    custom = ET.SubElement(
        pages[1],
        D + "custom-shape",
        {
            D + "name": "HiddenEnhanced",
            X + "id": "targetEnhanced",
            D + "layer": "Never",
            V + "x": "30pt",
            V + "y": "150pt",
            V + "width": "80pt",
            V + "height": "40pt",
        },
    )
    ET.SubElement(
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
    master = styles.find(".//" + S + "master-page")
    ET.SubElement(
        master,
        D + "rect",
        {
            X + "id": "targetA",
            D + "layer": "Never",
            V + "x": "500pt",
            V + "y": "500pt",
            V + "width": "40pt",
            V + "height": "40pt",
        },
    )
    connector(master, "StyleScope", "line", ("targetA", 0), ("targetA", 1))
    for root in [content, styles]:
        identities = [n.get(X + "id") for n in root.iter() if n.get(X + "id")]
        assert len(identities) == len(set(identities))
    parts["content.xml"], parts["styles.xml"] = (
        ET.tostring(r, encoding="utf-8", xml_declaration=True)
        for r in [content, styles]
    )
    output = Path(directory) / "connectors.odp"
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


def verify_saved(source, saved, case=None):
    before, after = entries(source), entries(saved)
    assert before.keys() == after.keys()
    for name in before:
        if name == "content.xml":
            expected = ET.fromstring(before[name])
            if case is not None:
                assert case == "move", case
                selected = next(
                    n for n in expected.iter() if n.get(D + "name") == "FreeLine"
                )
                selected.set(D + "transform", "matrix(1 0 0 1 0 0) translate(10pt 0pt)")
            assert normalized(expected) == normalized(ET.fromstring(after[name]))
        else:
            assert before[name] == after[name], name


def probes(page):
    white = (255, 255, 255)
    if page == 1:
        length = math.hypot(150, 50) / 3
        # Independent cubic Bernstein evaluation at t=1/4.
        t = 0.25
        x = (
            (1 - t) ** 3 * 25
            + 3 * (1 - t) ** 2 * t * (25 + length)
            + 3 * (1 - t) * t * t * (175 - length)
            + t**3 * 175
        )
        y = (
            (1 - t) ** 3 * 145
            + 3 * (1 - t) ** 2 * t * 145
            + 3 * (1 - t) * t * t * 195
            + t**3 * 195
        )
        return [
            (100, 70, INK),
            (100, 45, white),
            (250, 45, INK),
            (300, 70, INK),
            (250, 53, white),
            (x, y, INK),
            (50, 145, white),
            (250, 145, INK),
            (225 + length, 170, INK),
            (350, 195, INK),
            (300, 170, white),
        ]
    if page == 2:
        return [
            (155, 62.5, INK),
            (50, 60, white),
            (240, 50, white),
            (150, 190, INK),
            (254.53125, 156.84375, INK),
            (291.25, 194.75, INK),
            (295, 185, white),
        ]
    return [
        (249.5, 183, INK),
        (231.6, 144.6, white),
        (234.2, 146.7, INK),
        (55, 75, INK),
        (142.5, 135.5, INK),
        (40, 60, INK),
    ]
