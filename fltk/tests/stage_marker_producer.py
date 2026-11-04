#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODF1.2 marker resources and source-preserving edit oracle."""

import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

from stage_dash_producer import produce as base_document
from stage_layers_producer import D, O, S, V, entries
from stage_objects_producer import normalized
from stage_stroke_producer import INK

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
    for name, data, view in [
        ("Arrow", "M15 7L5 27H25Z", "100 -50 1000 500"),
        ("Hollow", "M0 0H20V20H0Z M5 5V15H15V5Z", "0 0 20 20"),
        ("Disabled", "M0 0H20V20H0Z", "0 0 0 20"),
    ]:
        ET.SubElement(
            common, D + "marker", {D + "name": name, V + "d": data, V + "viewBox": view}
        )
    ET.SubElement(
        common,
        D + "stroke-dash",
        {
            D + "name": "MarkerDash",
            D + "style": "rect",
            D + "dots1": "1",
            D + "dots1-length": "13pt",
            D + "dots2": "0",
            D + "dots2-length": "0pt",
            D + "distance": "8pt",
        },
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
                V + "stroke-width": "4pt",
                V + "stroke-linecap": "round",
                V + "stroke-color": "#203060",
                V + "stroke-opacity": "50%",
                D + "marker-start": "Arrow",
                D + "marker-end": "Arrow",
                D + "marker-start-width": "20pt",
                D + "marker-end-width": "20pt",
                D + "marker-start-center": "false",
                D + "marker-end-center": "false",
                **properties,
            },
        )

    style("MarkerShared")
    style(
        "MarkerCentered",
        **{D + "marker-start-center": "true", D + "marker-end-center": "true"},
    )
    style(
        "MarkerHollow",
        **{
            D + "marker-start": "",
            D + "marker-end": "Hollow",
            V + "stroke-width": "20pt",
        },
    )
    style(
        "MarkerDashed",
        **{
            D + "stroke": "dash",
            D + "stroke-dash": "MarkerDash",
            D + "marker-start-width": "10pt",
            D + "marker-end-width": "10pt",
        },
    )
    style(
        "MarkerOverlay",
        **{
            D + "stroke": "dash",
            D + "stroke-dash": "MarkerDash",
            D + "stroke-dash-names": "MarkerDash",
            D + "marker-start-width": "10pt",
            D + "marker-end-width": "10pt",
        },
    )
    style(
        "MarkerZeroShaft",
        **{
            V + "stroke-width": "0pt",
            D + "marker-start-width": "10pt",
            D + "marker-end-width": "10pt",
        },
    )
    style(
        "MarkerSmall",
        **{D + "marker-start-width": "8pt", D + "marker-end-width": "8pt"},
    )
    style(
        "MarkerArc",
        **{
            D + "marker-start-width": "14pt",
            D + "marker-end-width": "14pt",
            D + "marker-start-center": "true",
            D + "marker-end-center": "true",
        },
    )
    pages = root.findall(".//" + D + "page")
    for page in pages[:2]:
        for node in list(page):
            page.remove(node)
        page.attrib.pop(D + "nav-order", None)

    def line(name, x, y, end, sty):
        ET.SubElement(
            pages[0],
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

    line("MarkerA", 30, 60, 180, "MarkerShared")
    line("MarkerPeer", 30, 110, 180, "MarkerShared")
    line("Centered", 230, 60, 380, "MarkerCentered")
    line("Hollow", 30, 160, 180, "MarkerHollow")
    line("Dashed", 230, 110, 380, "MarkerDashed")
    line("Overlay", 230, 160, 380, "MarkerOverlay")
    line("ZeroShaft", 30, 210, 180, "MarkerZeroShaft")
    for name, x, y, w, h, data, sty, transform in [
        ("Cubic", 30, 30, 120, 100, "M0 80C0 0 120 0 120 80", "MarkerShared", None),
        ("Arc", 220, 30, 80, 60, "M0 0A80 60 0 0 1 80 60", "MarkerArc", None),
        (
            "Affine",
            0,
            0,
            80,
            60,
            "M0 0C0 0 20 40 80 60",
            "MarkerCentered",
            "matrix(-1 .15 .3 1.2 345 140)",
        ),
        ("Subpaths", 30, 150, 80, 60, "M0 0H80M0 30H80M0 60H80", "MarkerSmall", None),
        ("Closed", 150, 150, 50, 50, "M0 0H50V50H0Z", "MarkerSmall", None),
    ]:
        attrs = {
            D + "name": name,
            XML + "id": "Marker" + name,
            D + "style-name": sty,
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
    ids = [n.get(XML + "id") for n in root.iter() if n.get(XML + "id")]
    assert len(ids) == len(set(ids)), "Duplicate fixture XML identity"
    parts["content.xml"] = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    parts["styles.xml"] = ET.tostring(styles, encoding="utf-8", xml_declaration=True)
    output = Path(directory) / "markers.odp"
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
    (directory / "markers.odp").write_bytes(Path(source).read_bytes())


def verify_saved(source, saved, case, native=True):
    before, after = entries(source), entries(saved)
    assert before.keys() == after.keys()
    for name in before.keys() - (
        {"content.xml", "styles.xml"} if case == "markers" else {"content.xml"}
    ):
        assert before[name] == after[name], name
    old, new = ET.fromstring(before["content.xml"]), ET.fromstring(after["content.xml"])
    expected = next(n for n in old.iter() if n.get(D + "name") == "MarkerA")
    actual = next(n for n in new.iter() if n.get(D + "name") == "MarkerA")
    if case == "move":
        transform = actual.get(D + "transform")
        assert transform == "matrix(1 0 0 1 0 0) translate(10pt 0pt)", transform
        expected.set(D + "transform", transform)
    elif case == "markers":
        styles_old, styles_new = (
            ET.fromstring(before["styles.xml"]),
            ET.fromstring(after["styles.xml"]),
        )
        common_old, common_new = (
            styles_old.find(O + "styles"),
            styles_new.find(O + "styles"),
        )
        assert len(common_new) == len(common_old) + 1
        for i, n in enumerate(common_old):
            assert normalized(n) == normalized(common_new[i])
        resource = common_new[-1]
        identifier = resource.get(D + "name")
        assert identifier not in {n.get(D + "name") for n in common_old}
        assert resource.tag == D + "marker" and resource.attrib == {
            D + "name": identifier,
            V + "d": "M10 0L0 10L10 20L20 10Z",
            V + "viewBox": "0 0 20 20",
        }
        common_new.remove(resource)
        assert normalized(styles_old) == normalized(styles_new)
        name = actual.get(D + "style-name")
        assert name != expected.get(D + "style-name")
        automatic = new.find(O + "automatic-styles")
        node = next(n for n in automatic if n.get(S + "name") == name)
        assert node.attrib == {
            S + "name": name,
            S + "family": "graphic",
            S + "parent-style-name": "MarkerShared",
        }
        assert len(node) == 1 and node[0].attrib == {
            D + "marker-start": identifier,
            D + "marker-start-width": "30pt",
            D + "marker-start-center": "true",
            D + "marker-end": "",
        }, node[0].attrib
        automatic.remove(node)
        expected.set(D + "style-name", name)
    elif case == "width":
        name = actual.get(D + "style-name")
        assert name != expected.get(D + "style-name")
        automatic = new.find(O + "automatic-styles")
        node = next(n for n in automatic if n.get(S + "name") == name)
        assert node.attrib == {
            S + "name": name,
            S + "family": "graphic",
            S + "parent-style-name": "MarkerShared",
        }
        assert len(node) == 1 and node[0].attrib == {V + "stroke-width": "10pt"}
        automatic.remove(node)
        expected.set(D + "style-name", name)
    else:
        raise AssertionError(case)
    assert normalized(old) == normalized(new)
