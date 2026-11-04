#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODF1.2 dash resources and scoped authoring source oracle."""

import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

from stage_layers_producer import D, O, S, V, entries
from stage_objects_producer import normalized
from stage_stroke_producer import INK
from stage_stroke_producer import produce as base_document

__all__ = ["INK", "entries", "normalized", "produce", "verify", "verify_saved"]

XML = "{http://www.w3.org/XML/1998/namespace}"


def produce(directory):
    source = base_document(directory)
    parts = entries(source)
    root, styles = (
        ET.fromstring(parts["content.xml"]),
        ET.fromstring(parts["styles.xml"]),
    )
    common = styles.find(O + "styles")
    for name, values in {
        "DashMixed": {
            "style": "rect",
            "dots1": "2",
            "dots1-length": "200%",
            "dots2": "1",
            "dots2-length": "3pt",
            "distance": "50%",
        },
        "DashDots": {
            "style": "round",
            "dots1": "1",
            "dots1-length": "0pt",
            "dots2": "0",
            "dots2-length": "0pt",
            "distance": "30pt",
        },
        "DashRegular": {
            "style": "rect",
            "dots1": "2",
            "dots1-length": "12pt",
            "dots2": "1",
            "dots2-length": "3pt",
            "distance": "18pt",
        },
    }.items():
        ET.SubElement(
            common,
            D + "stroke-dash",
            {D + "name": name, **{D + k: v for k, v in values.items()}},
        )

    def style(name, **properties):
        node = ET.SubElement(
            common, S + "style", {S + "name": name, S + "family": "graphic"}
        )
        ET.SubElement(
            node,
            S + "graphic-properties",
            {
                D + "fill": "none",
                D + "stroke": "solid",
                V + "stroke-width": "10pt",
                V + "stroke-color": "#203060",
                V + "stroke-opacity": "50%",
                **properties,
            },
        )

    style("DashShared")
    style(
        "DashOverlay",
        **{
            D + "stroke": "dash",
            D + "stroke-dash": "DashRegular",
            D + "stroke-dash-names": "DashDots DashRegular",
        },
    )
    style("DotPaint", **{D + "stroke": "dash", D + "stroke-dash": "DashDots"})
    style(
        "MixedPaint",
        **{
            D + "stroke": "dash",
            D + "stroke-dash": "DashMixed",
            V + "stroke-width": "4pt",
        },
    )
    style(
        "CornerPaint",
        **{
            D + "stroke": "dash",
            D + "stroke-dash": "DashRegular",
            V + "stroke-width": "8pt",
            V + "stroke-linecap": "round",
            D + "stroke-linejoin": "bevel",
        },
    )
    pages = root.findall(".//" + D + "page")
    for page in pages[:2]:
        for node in list(page):
            page.remove(node)
        page.attrib.pop(D + "nav-order", None)

    def line(page, name, x, y, end, sty):
        ET.SubElement(
            page,
            D + "line",
            {
                D + "name": name,
                XML + "id": name,
                D + "style-name": sty,
                V + "x1": f"{x}pt",
                V + "y1": f"{y}pt",
                V + "x2": f"{end}pt",
                V + "y2": f"{y}pt",
            },
        )

    line(pages[0], "DashA", 30, 60, 180, "DashShared")
    line(pages[0], "DashPeer", 30, 110, 180, "DashShared")
    line(pages[0], "Overlay", 30, 160, 350, "DashOverlay")
    line(pages[0], "Dots", 30, 210, 350, "DotPaint")
    line(pages[0], "Mixed", 230, 60, 380, "MixedPaint")
    for name, x, y, w, h, data, transform in [
        ("Closed", 30, 40, 150, 120, "M0 0H150V120H0Z", None),
        ("Cusp", 220, 70, 55, 30, "M0 0L55 30L0 0Z", None),
        (
            "AffineDashes",
            0,
            0,
            80,
            60,
            "M0 0L80 30L0 60L80 0",
            "matrix(-1 .15 .3 1.2 360 150)",
        ),
    ]:
        attrs = {
            D + "name": name,
            XML + "id": name,
            D + "style-name": "CornerPaint",
            V + "x": f"{x}pt",
            V + "y": f"{y}pt",
            V + "width": f"{w}pt",
            V + "height": f"{h}pt",
            V + "viewBox": f"0 0 {w} {h}",
            V + "d": data,
        }
        if transform:
            attrs[D + "transform"] = transform
        ET.SubElement(pages[1], D + "path", attrs)
    parts["content.xml"] = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    parts["styles.xml"] = ET.tostring(styles, encoding="utf-8", xml_declaration=True)
    output = Path(directory) / "dashes.odp"
    with zipfile.ZipFile(output, "w") as z:
        for name, data in parts.items():
            z.writestr(
                name,
                data,
                compress_type=zipfile.ZIP_STORED
                if name == "mimetype"
                else zipfile.ZIP_DEFLATED,
            )
    return output


def verify(source, bin_dir, directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "dashes.odp").write_bytes(Path(source).read_bytes())


def verify_saved(source, saved, case, native=True):
    assert case == "dash"
    before, after = entries(source), entries(saved)
    assert before.keys() == after.keys()
    for name in before.keys() - {"content.xml", "styles.xml"}:
        assert before[name] == after[name], name
    old, new = ET.fromstring(before["content.xml"]), ET.fromstring(after["content.xml"])
    styles_old, styles_new = (
        ET.fromstring(before["styles.xml"]),
        ET.fromstring(after["styles.xml"]),
    )
    a, b = styles_old.find(O + "styles"), styles_new.find(O + "styles")
    assert len(b) == len(a) + 1
    for i, node in enumerate(a):
        assert normalized(node) == normalized(b[i])
    resource = b[-1]
    assert resource.tag == D + "stroke-dash"
    identifier = resource.get(D + "name")
    assert identifier not in {n.get(D + "name") for n in a}
    assert resource.attrib == {
        D + "name": identifier,
        D + "style": "round",
        D + "dots1": "2",
        D + "dots1-length": "12pt",
        D + "dots2": "1",
        D + "dots2-length": "3pt",
        D + "distance": "18pt",
    }
    b.remove(resource)
    assert normalized(styles_old) == normalized(styles_new)
    actual = next(n for n in new.iter() if n.get(D + "name") == "DashA")
    expected = next(n for n in old.iter() if n.get(D + "name") == "DashA")
    new_name = actual.get(D + "style-name")
    assert new_name != expected.get(D + "style-name")
    automatic = new.find(O + "automatic-styles")
    added = next(n for n in automatic if n.get(S + "name") == new_name)
    assert added.attrib == {
        S + "name": new_name,
        S + "family": "graphic",
        S + "parent-style-name": "DashShared",
    }
    assert len(added) == 1 and added[0].tag == S + "graphic-properties"
    assert added[0].attrib == {D + "stroke": "dash", D + "stroke-dash": identifier}
    automatic.remove(added)
    expected.set(D + "style-name", new_name)
    assert normalized(old) == normalized(new)
