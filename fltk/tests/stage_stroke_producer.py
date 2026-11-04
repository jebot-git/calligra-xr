#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODF1.2 stroke inputs and isolated raw-style/archive oracle."""

from pathlib import Path
import copy
import xml.etree.ElementTree as ET
import zipfile
from stage_paint_producer import produce as base_document
from stage_layers_producer import entries, O, S, D, V
from stage_objects_producer import normalized

XML = "{http://www.w3.org/XML/1998/namespace}"
INK = (143, 151, 175)  # #203060 at50% on white; Cairo byte rounding.


def produce(directory):
    source = base_document(directory)
    parts = entries(source)
    root, styles = (
        ET.fromstring(parts["content.xml"]),
        ET.fromstring(parts["styles.xml"]),
    )
    common, automatic = styles.find(O + "styles"), root.find(O + "automatic-styles")
    if automatic is None:
        automatic = ET.Element(O + "automatic-styles")
        root.insert(0, automatic)

    def style(name, parent=None, **properties):
        node = ET.SubElement(
            common, S + "style", {S + "name": name, S + "family": "graphic"}
        )
        if parent:
            node.set(S + "parent-style-name", parent)
        prop = ET.SubElement(node, S + "graphic-properties")
        for k, v in properties.items():
            prop.set(k, v)

    style(
        "StrokeBase",
        **{
            D + "fill": "none",
            D + "stroke": "solid",
            V + "stroke-width": "20pt",
            V + "stroke-color": "#203060",
            V + "stroke-opacity": "50%",
        },
    )
    for cap in ["butt", "round", "square"]:
        style("Cap" + cap.title(), "StrokeBase", **{V + "stroke-linecap": cap})
    for join in ["miter", "round", "bevel", "none"]:
        style("Join" + join.title(), "StrokeBase", **{D + "stroke-linejoin": join})
    inherited = ET.SubElement(
        automatic,
        S + "style",
        {
            S + "name": "InheritedStroke",
            S + "family": "graphic",
            S + "parent-style-name": "CapRound",
        },
    )
    ET.SubElement(inherited, S + "graphic-properties", {D + "stroke-linejoin": "bevel"})
    pages = root.findall(".//" + D + "page")
    protected = copy.deepcopy(
        next(n for n in pages[0] if n.get(D + "name") == "Locked")
    )
    for i, page in enumerate(pages):
        for node in list(page):
            page.remove(node)
        page.attrib.pop(D + "nav-order", None)
        page.set(
            D + "name", ["Caps and joins", "Inherited and protected", "Unchanged"][i]
        )

    def line(page, name, x1, y1, x2, y2, style_name):
        return ET.SubElement(
            page,
            D + "line",
            {
                D + "name": name,
                XML + "id": name,
                D + "style-name": style_name,
                V + "x1": f"{x1}pt",
                V + "y1": f"{y1}pt",
                V + "x2": f"{x2}pt",
                V + "y2": f"{y2}pt",
            },
        )

    def path(page, name, x, y, data, style_name):
        return ET.SubElement(
            page,
            D + "path",
            {
                D + "name": name,
                XML + "id": name,
                D + "style-name": style_name,
                V + "x": f"{x}pt",
                V + "y": f"{y}pt",
                V + "width": "80pt",
                V + "height": "80pt",
                V + "viewBox": "0 0 80 80",
                V + "d": data,
            },
        )

    for i, cap in enumerate(["butt", "round", "square"]):
        line(
            pages[0],
            "Line" + cap.title(),
            30 + 125 * i,
            60,
            100 + 125 * i,
            60,
            "Cap" + cap.title(),
        )
    for i, join in enumerate(["miter", "round", "bevel", "none"]):
        path(
            pages[0],
            "Corner" + join.title(),
            20 + 90 * i,
            120,
            "M5 10H50V55",
            "Join" + join.title(),
        )
    path(pages[0], "ZeroRound", 110, 200, "M10 10h0", "CapRound")
    path(pages[0], "ZeroSquare", 200, 200, "M10 10h0", "CapSquare")
    path(pages[0], "MoveOnly", 310, 200, "M10 10", "CapSquare")
    line(pages[1], "Inherited", 40, 70, 130, 70, "InheritedStroke")
    line(pages[1], "Peer", 170, 70, 260, 70, "InheritedStroke")
    protected.set(D + "name", "Protected")
    protected.set(V + "x", "300pt")
    protected.set(V + "y", "50pt")
    pages[1].append(protected)
    group = ET.SubElement(
        pages[1], D + "g", {D + "name": "Group", XML + "id": "StrokeGroup"}
    )
    line(group, "GroupedLine", 40, 150, 110, 150, "CapButt")
    path(group, "GroupedCorner", 150, 140, "M5 10H50V55", "JoinMiter")
    # A translucent self-crossing polyline and a reflected/sheared counterpart
    # exercise single coverage and affine-local stroke width in both outputs.
    path(pages[2], "Crossing", 20, 40, "M0 0L70 70H0L70 0", "CapRound")
    affine = path(pages[2], "Affine", 150, 40, "M0 0H50V50", "JoinRound")
    affine.set(D + "transform", "matrix(-1 .15 .3 1.2 0 0) translate(430pt 75pt)")
    style("ZigzagStroke", "StrokeBase", **{V + "stroke-width": "13pt"})
    zigzag = path(
        pages[2],
        "Zigzag",
        0,
        0,
        "M59.679117911660015 102.56047225130351L100.19754509135957 65.575191105774309L46.177254092804588 58.580344234204723L22.246590926162412 98.286156342534241L41.793675365775442 26.201354044098714L26.975217695373999 83.497360830057033L77.167577401999367 72.854054460710174",
        "ZigzagStroke",
    )
    zigzag.set(D + "transform", "scale(1.7 1.5) translate(15pt 15pt)")
    near_limit = path(
        pages[1],
        "NearLimit",
        0,
        0,
        "M36.2218 67.1899L63.3619 36.7287L25.4666 67.9204L102.431 97.3649L93.9649 45.8485L91.5374 100.459L71.0086 58.0295",
        "ZigzagStroke",
    )
    near_limit.set(D + "transform", "scale(.7 .5) translate(250pt 100pt)")
    parts["content.xml"] = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    parts["styles.xml"] = ET.tostring(styles, encoding="utf-8", xml_declaration=True)
    output = Path(directory) / "strokes.odp"
    with zipfile.ZipFile(output, "w") as archive:
        for name, content in parts.items():
            archive.writestr(
                name,
                content,
                compress_type=zipfile.ZIP_STORED
                if name == "mimetype"
                else zipfile.ZIP_DEFLATED,
            )
    return output


CASES = {
    "cap": {"LineRound": {V + "stroke-linecap": "square"}},
    "join": {"CornerMiter": {D + "stroke-linejoin": "round"}},
    "inherited": {
        "Inherited": {V + "stroke-linecap": "square", D + "stroke-linejoin": "round"}
    },
    "group": {
        "GroupedLine": {V + "stroke-linecap": "round", D + "stroke-linejoin": "bevel"},
        "GroupedCorner": {
            V + "stroke-linecap": "round",
            D + "stroke-linejoin": "bevel",
        },
    },
}


def verify(source, bin_dir, directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "strokes.odp").write_bytes(Path(source).read_bytes())


def verify_saved(source, saved, case, native=True):
    before, after = entries(source), entries(saved)
    assert before.keys() == after.keys()
    assert all(before[n] == after[n] for n in before if n != "content.xml"), (
        "Unrelated archive part changed"
    )
    expected, actual = (
        ET.fromstring(before["content.xml"]),
        ET.fromstring(after["content.xml"]),
    )
    automatic = expected.find(O + "automatic-styles")
    original_styles = {n.get(S + "name"): n for n in automatic.findall(S + "style")}
    actual_styles = {
        n.get(S + "name"): n
        for n in actual.find(O + "automatic-styles").findall(S + "style")
    }
    additions = []
    for name, changes in CASES[case].items():
        src = next(n for n in expected.iter() if n.get(D + "name") == name)
        dst = next(n for n in actual.iter() if n.get(D + "name") == name)
        old_name, new_name = src.get(D + "style-name"), dst.get(D + "style-name")
        assert new_name != old_name and new_name not in original_styles
        changed = actual_styles[new_name]
        if old_name in original_styles:
            new = copy.deepcopy(original_styles[old_name])
        else:
            new = ET.Element(
                S + "style",
                {S + "family": "graphic", S + "parent-style-name": old_name},
            )
        new.set(S + "name", new_name)
        properties = new.find(S + "graphic-properties")
        if properties is None:
            properties = ET.Element(S + "graphic-properties")
            new.insert(0, properties)
        properties.attrib.update(changes)
        assert normalized(new) == normalized(changed), (
            case,
            name,
            "Unexpected changed style",
        )
        src.set(D + "style-name", new_name)
        additions.append(new)
    for node in additions:
        automatic.append(node)
    assert normalized(expected) == normalized(actual), (
        case,
        "Unrelated source or style changed",
    )
