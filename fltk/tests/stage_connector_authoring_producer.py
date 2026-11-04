#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent authoring targets and scoped connector/style/ID source oracle."""

import math
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

from stage_connector_edit_producer import D, O, S, T, V, X, entries, normalized
from stage_connector_edit_producer import produce as edit_document

COLOR = (32, 48, 64)


def produce(directory):
    original = edit_document(directory)
    parts = entries(original)
    root = ET.fromstring(parts["content.xml"])
    page = root.find(".//" + D + "page")
    for name, x, y in [("AuthorA", 30, 110), ("AuthorB", 300, 230)]:
        ET.SubElement(
            page,
            D + "rect",
            {
                D + "name": name,
                D + "style-name": "EditTargetInk",
                V + "x": f"{x}pt",
                V + "y": f"{y}pt",
                V + "width": "40pt",
                V + "height": "40pt",
            },
        )
    parts["content.xml"] = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    output = Path(directory) / "connector-authoring.odp"
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
    expected, actual = (ET.fromstring(p["content.xml"]) for p in [before, after])
    if case is None:
        assert normalized(expected) == normalized(actual)
    else:
        assert case in {"attached", "free", "skew", "label"}, case
        old_page, page = (r.find(".//" + D + "page") for r in [expected, actual])
        old_names = {n.get(D + "name") for n in old_page}
        added = [n for n in page if n.get(D + "name") not in old_names]
        assert len(added) == 1 and added[0].tag == D + "connector"
        connector = added[0]
        assert not any(
            V + key in connector.attrib for key in ["x", "y", "width", "height"]
        )
        identities = [n.get(X + "id") for n in actual.iter() if n.get(X + "id")]
        assert len(identities) == len(set(identities))
        assert connector.get(X + "id")
        for name in ["AuthorA", "AuthorB"]:
            target = next(n for n in page if n.get(D + "name") == name)
            old = next(n for n in old_page if n.get(D + "name") == name)
            identity = target.attrib[X + "id"]
            assert identity not in {n.get(X + "id") for n in expected.iter()}
            old.set(X + "id", identity)
        if case == "attached":
            for side, name, glue in [
                ("start", "AuthorA", "1"),
                ("end", "AuthorB", "3"),
            ]:
                target = next(n for n in page if n.get(D + "name") == name)
                assert connector.get(D + side + "-shape") == target.get(X + "id")
                assert connector.get(D + side + "-glue-point") == glue
            assert connector.get(D + "type") == "curve"
            length = math.hypot(230, 120) / 3
            geometry = [70, 130, 70 + length, 130, 300 - length, 250, 300, 250]
            tokens = connector.attrib[V + "d"].split()
            assert tokens[0] == "M" and tokens[3] == "C"
            data = [float(v) * 72 / 2540 for v in tokens if v not in {"M", "C"}]
            assert len(data) == len(geometry) and all(
                abs(a - b) < 1e-7 for a, b in zip(data, geometry)
            )
            coordinates = [70, 130, 300, 250]
        else:
            assert not any(
                connector.get(D + side + suffix)
                for side in ["start", "end"]
                for suffix in ["-shape", "-glue-point"]
            )
            coordinates = [80, 90, 350, 90]
            assert connector.get(D + "type") == (
                "standard" if case == "skew" else "line"
            )
            if case == "skew":
                raw = connector.get(D + "line-skew", "").split()
                assert len(raw) == 1 and abs(float(raw[0][:-2]) - 5) < 1e-7
            else:
                tokens = connector.attrib[V + "d"].split()
                assert len(tokens) == 6 and tokens[0] == "M" and tokens[3] == "L"
                data = [float(tokens[i]) * 72 / 2540 for i in [1, 2, 4, 5]]
                assert all(abs(a - b) < 1e-7 for a, b in zip(data, coordinates))
        for key, value in zip(["x1", "y1", "x2", "y2"], coordinates):
            assert abs(float(connector.attrib[V + key][:-2]) - value) < 1e-7
        paragraph = connector.find(T + "p")
        assert paragraph is not None and "".join(paragraph.itertext()) == (
            "abc" if case == "label" else ""
        )
        automatic = actual.find(O + "automatic-styles")
        original_auto = expected.find(O + "automatic-styles")
        old_styles = {n.get(S + "name") for n in original_auto}
        new_styles = [n for n in automatic if n.get(S + "name") not in old_styles]
        assert len(new_styles) == 2
        style_name = connector.attrib[D + "style-name"]
        graphic = next(n for n in new_styles if n.get(S + "name") == style_name)
        assert graphic.get(S + "family") == "graphic"
        props = graphic.find(S + "graphic-properties")
        assert props.get(D + "fill") == "none" and props.get(D + "stroke") == "solid"
        assert (
            props.get(V + "stroke-color") == "#203040"
            and props.get(V + "stroke-width") == "1pt"
        )
        assert props.get(D + "textarea-vertical-align") == "middle"
        label = next(
            n
            for n in new_styles
            if n.get(S + "name") == paragraph.get(T + "style-name")
        )
        assert label.get(S + "family") == "paragraph"
        assert (
            label.find(S + "paragraph-properties").get(
                "{urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0}text-align"
            )
            == "center"
        )
        for style in new_styles:
            automatic.remove(style)
        page.remove(connector)
        assert normalized(expected) == normalized(actual)
    for part in before.keys() - {"content.xml"}:
        assert before[part] == after[part], part
