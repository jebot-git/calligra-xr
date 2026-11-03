#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent strict ODF paint authoring, isolated style and archive oracle."""

from pathlib import Path
import copy
import io
from PIL import Image
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from stage_layer_edit_producer import produce as source_document
from stage_layers_producer import entries, O, S, T, D, V
from stage_objects_producer import normalized

P = "{urn:oasis:names:tc:opendocument:xmlns:presentation:1.0}"
FO = "{urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0}"
PATCH = {
    D + "fill": "solid",
    D + "fill-color": "#336699",
    D + "opacity": "50%",
    D + "stroke": "solid",
    V + "stroke-color": "#990066",
    V + "stroke-opacity": "75%",
    V + "stroke-width": "6pt",
    V + "fill-rule": "evenodd",
}


def produce(directory):
    source = source_document(directory)
    parts = entries(source)
    styles = ET.fromstring(parts["styles.xml"])
    # Keep the hidden member hidden but editable; the visible Locked shape
    # independently tests rejection. Group formatting must visit both members.
    styles.find(".//" + D + "layer[@" + D + 'name="HiddenLock"]').set(
        D + "protected", "false"
    )
    common = styles.find(O + "styles")
    for style in common.findall(S + "style"):
        properties = style.find(S + "graphic-properties")
        if style.get(S + "family") == "graphic" and properties is not None:
            properties.set(D + "stroke", "none")
    base = ET.SubElement(
        common, S + "style", {S + "name": "PaintTitle", S + "family": "presentation"}
    )
    ET.SubElement(
        base,
        S + "graphic-properties",
        {
            D + "fill": "solid",
            D + "fill-color": "#448822",
            D + "stroke": "none",
            FO + "padding-left": "5pt",
        },
    )
    ET.SubElement(base, S + "text-properties", {FO + "font-size": "15pt"})
    root = ET.fromstring(parts["content.xml"])
    image_style = ET.SubElement(
        common, S + "style", {S + "name": "ImagePaint", S + "family": "graphic"}
    )
    ET.SubElement(
        image_style,
        S + "graphic-properties",
        {D + "fill": "none", D + "stroke": "none", D + "opacity": "25%"},
    )
    image = io.BytesIO()
    Image.new("RGBA", (4, 4), (40, 80, 160, 128)).save(image, format="PNG")
    parts["Pictures/paint-alpha.png"] = image.getvalue()
    M = "{urn:oasis:names:tc:opendocument:xmlns:manifest:1.0}"
    manifest = ET.fromstring(parts["META-INF/manifest.xml"])
    ET.SubElement(
        manifest,
        M + "file-entry",
        {M + "full-path": "Pictures/paint-alpha.png", M + "media-type": "image/png"},
    )
    parts["META-INF/manifest.xml"] = ET.tostring(
        manifest, encoding="utf-8", xml_declaration=True
    )
    first = root.findall(".//" + D + "page")[0]
    frame = ET.SubElement(
        first,
        D + "frame",
        {
            D + "name": "Image",
            D + "style-name": "ImagePaint",
            V + "x": "240pt",
            V + "y": "190pt",
            V + "width": "70pt",
            V + "height": "40pt",
        },
    )
    X = "{http://www.w3.org/1999/xlink}"
    ET.SubElement(
        frame,
        D + "image",
        {
            X + "href": "Pictures/paint-alpha.png",
            X + "type": "simple",
            X + "show": "embed",
            X + "actuate": "onLoad",
        },
    )
    ET.SubElement(
        first,
        D + "path",
        {
            D + "name": "Hole",
            D + "style-name": "Red",
            V + "x": "140pt",
            V + "y": "190pt",
            V + "width": "70pt",
            V + "height": "40pt",
            V + "viewBox": "0 0 70 40",
            V + "d": "M 0 0 L 70 0 L 70 40 L 0 40 Z M 20 10 L 50 10 L 50 30 L 20 30 Z",
        },
    )
    parent = ET.SubElement(
        common, S + "style", {S + "name": "ImageParent", S + "family": "graphic"}
    )
    ET.SubElement(
        parent,
        S + "graphic-properties",
        {
            D + "fill": "none",
            D + "stroke": "none",
            D + "opacity": "25%",
            D + "image-opacity": "75%",
        },
    )
    automatic = root.find(O + "automatic-styles")
    if automatic is None:
        automatic = ET.Element(O + "automatic-styles")
        root.insert(list(root).index(root.find(O + "body")), automatic)
    inherited = ET.SubElement(
        automatic,
        S + "style",
        {
            S + "name": "ImageChild",
            S + "family": "graphic",
            S + "parent-style-name": "ImageParent",
        },
    )
    ET.SubElement(inherited, S + "graphic-properties", {D + "opacity": "50%"})
    second = root.findall(".//" + D + "page")[1]
    for name, x in [("InheritedImage", 40), ("InheritedPeer", 140)]:
        item = ET.SubElement(
            second,
            D + "frame",
            {
                D + "name": name,
                D + "style-name": "ImageChild",
                V + "x": f"{x}pt",
                V + "y": "140pt",
                V + "width": "70pt",
                V + "height": "45pt",
            },
        )
        ET.SubElement(
            item,
            D + "image",
            {
                X + "href": "Pictures/paint-alpha.png",
                X + "type": "simple",
                X + "show": "embed",
                X + "actuate": "onLoad",
            },
        )
    third = root.findall(".//" + D + "page")[2]
    third.find(D + "rect").attrib.pop(D + "style-name", None)
    third.find(D + "rect").set(P + "style-name", "PaintTitle")
    # Original style/content prefix shadow/default-namespace coverage stays in
    # the original producer; this derived fixture has independent ET prefixes.
    parts["content.xml"] = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    parts["styles.xml"] = ET.tostring(styles, encoding="utf-8", xml_declaration=True)
    target = source.with_name("paint.odp")
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


def verify_saved(source, saved, case, native=False):
    before, after = entries(source), entries(saved)
    assert before.keys() == after.keys()
    for name in before:
        if name != "content.xml":
            assert before[name] == after[name], (case, name)
    original = ET.fromstring(before["content.xml"])
    actual = ET.fromstring(after["content.xml"])
    expected = copy.deepcopy(original)
    if case == "original":
        assert normalized(expected) == normalized(actual)
        return
    selected = {
        "paint": ["Editable"],
        "group": ["GroupVisible", "HiddenLocked"],
        "none": ["Editable"],
        "presentation": ["EmptyLocal"],
        "image": ["Image"],
        "image-fill": ["Image"],
        "image-background": ["Image"],
        "image-zero": ["Image"],
        "image-inherited": ["InheritedImage"],
        "rule": ["Hole"],
    }[case]
    automatic = expected.find(O + "automatic-styles")
    actual_automatic = actual.find(O + "automatic-styles")
    if automatic is None:
        automatic = ET.Element(O + "automatic-styles")
        expected.insert(list(expected).index(expected.find(O + "body")), automatic)
    # Source definitions are resolved independently of the native engine. A
    # copied automatic style keeps every old attribute and child. A common
    # style gets a new automatic child whose parent remains the common name.
    styles = ET.fromstring(before["styles.xml"])
    common = {}
    for doc in [styles, original]:
        section = doc.find(O + "styles")
        if section is not None:
            for node in section.findall(S + "style"):
                common[(node.get(S + "family"), node.get(S + "name"))] = node
    existing = {
        (node.get(S + "family"), node.get(S + "name")): node
        for node in original.findall(O + "automatic-styles/" + S + "style")
    }
    additions = []
    for name in selected:
        src = next(n for n in expected.iter() if n.get(D + "name") == name)
        dst = next(n for n in actual.iter() if n.get(D + "name") == name)
        family = "presentation" if src.get(P + "style-name") else "graphic"
        attr = (P if family == "presentation" else D) + "style-name"
        old = src.get(attr)
        new = dst.get(attr)
        assert new and new != old
        assert (family, new) not in existing and (family, new) not in common
        key = (family, old)
        node = (
            copy.deepcopy(existing[key])
            if key in existing
            else ET.Element(
                S + "style",
                {
                    S + "family": family,
                    **({S + "parent-style-name": old} if old else {}),
                },
            )
        )
        node.set(S + "name", new)
        properties = node.find(S + "graphic-properties")
        if properties is None:
            properties = ET.Element(S + "graphic-properties")
            node.insert(0, properties)
        patch = (
            {D + "fill": "none", D + "stroke": "none"}
            if case == "none"
            else dict(PATCH)
        )
        if case == "image":
            patch = {D + "image-opacity": "50%"}
        if case == "image-fill":
            patch = {D + "opacity": "75%"}
        if case == "image-background":
            patch = {
                D + "fill": "solid",
                D + "fill-color": "#cc0000",
                D + "opacity": "25%",
                D + "image-opacity": "50%",
            }
        if case == "image-zero":
            patch = {D + "image-opacity": "0%"}
        if case == "image-inherited":
            patch = {D + "image-opacity": "50%"}
        if case == "rule":
            patch = {V + "fill-rule": "evenodd"}
        if native and case not in (
            "image",
            "rule",
            "image-fill",
            "image-background",
            "image-zero",
            "image-inherited",
        ):
            # The actual panel submits changed fields only; inheritance of an
            # unchanged property remains a source relationship, not a copied
            # explicit property.
            patch.pop(D + "stroke" if case == "none" else D + "fill")
        properties.attrib.update(patch)
        additions.append(node)
        src.set(attr, new)
    for node in additions:
        automatic.append(node)
    assert normalized(expected) == normalized(actual), (
        case,
        "unrelated source/style metadata changed",
    )


def verify(source, bin_dir, directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "paint.odp").write_bytes(Path(source).read_bytes())
    prefix = directory / "model"
    subprocess.run(
        [
            str(Path(bin_dir).resolve() / "calligra-native-stage-paint-test"),
            "--document",
            str(Path(source).resolve()),
            str(prefix.resolve()),
        ],
        check=True,
    )
    for case in [
        "paint",
        "group",
        "none",
        "presentation",
        "image",
        "rule",
        "image-fill",
        "image-background",
        "image-zero",
        "image-inherited",
    ]:
        verify_saved(source, prefix.with_name(prefix.name + "-" + case + ".odp"), case)
    print(
        "PASS independent producer scoped paint, common/automatic/presentation styles and unchanged source/archive oracle",
        flush=True,
    )
