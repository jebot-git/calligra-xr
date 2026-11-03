#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independently authored strict ODF1.2 layer visibility/protection/scoping corpus."""

from pathlib import Path
import copy
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from stage_move_producer import produce as movement_source, entries, DRAW as D, SVG as V
from stage_objects_producer import normalized

O = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
S = "{urn:oasis:names:tc:opendocument:xmlns:style:1.0}"
T = "{urn:oasis:names:tc:opendocument:xmlns:text:1.0}"


def produce(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    source = movement_source(directory)
    parts = entries(source)
    root = ET.fromstring(parts["content.xml"])
    presentation = root.find(O + "body/" + O + "presentation")
    for node in list(presentation):
        presentation.remove(node)
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
    masters = styles.find(O + "master-styles")
    master = masters.find(S + "master-page")
    for node in list(master):
        master.remove(node)

    def layer_set(parent, values):
        result = ET.Element(D + "layer-set")
        parent.insert(0, result)
        for name, display, locked in values:
            ET.SubElement(
                result,
                D + "layer",
                {
                    D + "name": name,
                    D + "display": display,
                    D + "protected": "true" if locked else "false",
                },
            )
        return result

    layer_set(
        masters,
        [
            ("All", "always", False),
            ("Screen", "screen", False),
            ("Print", "printer", False),
            ("Never", "none", False),
            ("Locked", "always", True),
            ("HiddenLock", "none", True),
        ],
    )

    def rectangle(
        parent, name, color, x, y, layer=None, text=True, width=70, height=45
    ):
        attrs = {
            D + "name": name,
            D + "style-name": color,
            V + "x": f"{x}pt",
            V + "y": f"{y}pt",
            V + "width": f"{width}pt",
            V + "height": f"{height}pt",
        }
        if layer is not None:
            attrs[D + "layer"] = layer
        node = ET.SubElement(parent, D + "rect", attrs)
        if text:
            ET.SubElement(node, T + "p", {T + "style-name": "Body"}).text = (
                name + " Český"
            )
        return node

    rectangle(master, "MasterScreen", "Blue", 0, 0, "Screen", False, 400, 20)
    rectangle(master, "MasterPrint", "Orange", 0, 0, "Print", False, 400, 20)
    local_master = copy.deepcopy(master)
    local_master.set(S + "name", "LocalMaster")
    masters.append(local_master)
    layer_set(local_master, [("Screen", "always", False), ("Print", "none", False)])
    page = ET.SubElement(
        presentation,
        D + "page",
        {D + "name": "Global layers", D + "master-page-name": master.get(S + "name")},
    )
    rectangle(page, "Editable", "Red", 40, 50, "All")
    rectangle(page, "ScreenOnly", "Blue", 140, 50, "Screen")
    rectangle(page, "PrintOnly", "Green", 240, 50, "Print")
    rectangle(page, "Never", "Orange", 40, 120, "Never")
    rectangle(page, "Locked", "Orange", 140, 120, "Locked")
    group = ET.SubElement(page, D + "g", {D + "name": "Mixed protected group"})
    rectangle(group, "GroupVisible", "Blue", 240, 120, "All")
    nested = ET.SubElement(group, D + "g", {D + "name": "Hidden protected member"})
    rectangle(nested, "HiddenLocked", "Red", 240, 190, "HiddenLock")
    page = ET.SubElement(
        presentation,
        D + "page",
        {D + "name": "Local layers", D + "master-page-name": master.get(S + "name")},
    )
    layer_set(
        page,
        [
            ("Screen", "printer", False),
            ("Print", "screen", False),
            ("Locked", "always", False),
        ],
    )
    rectangle(page, "LocalScreen", "Red", 40, 60, "Print")
    rectangle(page, "LocalPrint", "Green", 140, 60, "Screen")
    rectangle(page, "LocalDefault", "Orange", 240, 60, "Locked")
    page = ET.SubElement(
        presentation,
        D + "page",
        {
            D + "name": "Empty local and master override",
            D + "master-page-name": "LocalMaster",
        },
    )
    layer_set(page, [])
    rectangle(page, "EmptyLocal", "Red", 40, 50)
    parts["content.xml"] = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    parts["styles.xml"] = ET.tostring(styles, encoding="utf-8", xml_declaration=True)
    target = source.with_name("layers.odp")
    with zipfile.ZipFile(target, "w") as z:
        for name, data in parts.items():
            z.writestr(
                name,
                data,
                compress_type=zipfile.ZIP_STORED
                if name == "mimetype"
                else zipfile.ZIP_DEFLATED,
            )
    source.unlink()
    return target


def verify_saved(source, saved, moved=False):
    before, after = entries(source), entries(saved)
    assert before.keys() == after.keys()
    for name in before:
        if name != "content.xml":
            assert before[name] == after[name], name
    a, b = ET.fromstring(before["content.xml"]), ET.fromstring(after["content.xml"])
    if moved:
        node = b.find(".//" + D + "rect[@" + D + 'name="Editable"]')
        assert node.get(D + "transform") == "matrix(1 0 0 1 0 0) translate(10pt 5pt)"
        del node.attrib[D + "transform"]
    assert normalized(a) == normalized(b), (
        "Unexpected layer/protected object/master/text/archive change"
    )


def verify(source, bin_dir, artifacts):
    artifacts = Path(artifacts)
    artifacts.mkdir(parents=True, exist_ok=True)
    target = artifacts / "model-moved.odp"
    subprocess.run(
        [
            str(Path(bin_dir).resolve() / "calligra-native-stage-layers-test"),
            "--document",
            str(source),
            str(target),
        ],
        check=True,
    )
    verify_saved(source, target, True)
    (artifacts / "layers.odp").write_bytes(source.read_bytes())
    return target
