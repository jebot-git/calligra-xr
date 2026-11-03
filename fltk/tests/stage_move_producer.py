#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independently produced ODP object-movement fixture and raw source checks."""

from pathlib import Path
import io
import math
import re
import xml.etree.ElementTree as ET
import zipfile
from odf.opendocument import OpenDocumentPresentation
from odf.style import (
    Style,
    GraphicProperties,
    TextProperties,
    PageLayout,
    PageLayoutProperties,
    MasterPage,
)
from odf.draw import (
    Page,
    Rect,
    G,
    Frame,
    TextBox,
    Image as DrawImage,
    Line,
    CustomShape,
    EnhancedGeometry,
)
from odf.text import P, Span
from PIL import Image

DRAW = "{urn:oasis:names:tc:opendocument:xmlns:drawing:1.0}"
SVG = "{urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0}"


def produce(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    doc = OpenDocumentPresentation()
    layout = PageLayout(name="Wide")
    layout.addElement(PageLayoutProperties(pagewidth="400pt", pageheight="240pt"))
    doc.automaticstyles.addElement(layout)
    blue = Style(name="Blue", family="graphic")
    blue.addElement(
        GraphicProperties(
            fill="solid",
            fillcolor="#225588",
            stroke="solid",
            strokecolor="#102030",
            strokewidth="1pt",
        )
    )
    doc.styles.addElement(blue)
    red = Style(name="Red", family="graphic")
    red.addElement(
        GraphicProperties(
            fill="solid",
            fillcolor="#cc3030",
            stroke="solid",
            strokecolor="#102030",
            strokewidth="1pt",
        )
    )
    doc.styles.addElement(red)
    text = Style(name="Body", family="paragraph")
    text.addElement(TextProperties(fontfamily="DejaVu Sans", fontsize="12pt"))
    doc.styles.addElement(text)
    strong = Style(name="Strong", family="text")
    strong.addElement(TextProperties(fontweight="bold", color="#223344"))
    doc.styles.addElement(strong)
    master = MasterPage(name="Standard", pagelayoutname=layout)
    master.addElement(
        Rect(stylename=blue, x="0pt", y="0pt", width="400pt", height="20pt")
    )
    doc.masterstyles.addElement(master)
    page = Page(name="Movement", masterpagename=master)
    doc.presentation.addElement(page)
    page.addElement(
        Rect(
            name="Rectangle",
            stylename=red,
            x="40pt",
            y="70pt",
            width="80pt",
            height="50pt",
        )
    )
    group = G()
    page.addElement(group)
    frame = Frame(
        name="GroupedText",
        transform="matrix(0 1 -1 0 0 0) translate(340pt 80pt)",
        x="10pt",
        y="20pt",
        width="110pt",
        height="45pt",
    )
    box = TextBox()
    p = P(stylename=text, text="Native ")
    p.addElement(Span(stylename=strong, text="Český Stage"))
    box.addElement(p)
    frame.addElement(box)
    group.addElement(frame)
    pixels = io.BytesIO()
    Image.new("RGBA", (16, 12), (20, 150, 90, 210)).save(pixels, format="PNG")
    href = doc.addPictureFromString(pixels.getvalue(), "image/png")
    frame = Frame(name="Image", x="180pt", y="70pt", width="45pt", height="35pt")
    frame.addElement(
        DrawImage(href=href, type="simple", show="embed", actuate="onLoad")
    )
    page.addElement(frame)
    custom = CustomShape(
        name="Triangle",
        stylename=blue,
        x="125pt",
        y="145pt",
        width="80pt",
        height="55pt",
    )
    custom.addElement(
        EnhancedGeometry(
            viewbox="0 0 100 100", enhancedpath="M 0 100 L 50 0 100 100 Z N"
        )
    )
    page.addElement(custom)
    page.addElement(
        Line(name="Line", stylename=blue, x1="20pt", y1="220pt", x2="220pt", y2="220pt")
    )
    second = Page(name="Second", masterpagename=master)
    second.addElement(
        Rect(stylename=blue, x="60pt", y="60pt", width="260pt", height="120pt")
    )
    doc.presentation.addElement(second)
    path = directory / "movement.odp"
    doc.save(str(path))
    # Include opaque data in the package manifest as well as the ZIP archive.
    with zipfile.ZipFile(path) as z:
        original = [(item, z.read(item.filename)) for item in z.infolist()]
    ns = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0"
    ET.register_namespace("manifest", ns)
    with zipfile.ZipFile(path, "w") as z:
        for item, value in original:
            if item.filename == "META-INF/manifest.xml":
                root = ET.fromstring(value)
                root.set("{" + ns + "}version", "1.2")
                ET.SubElement(
                    root,
                    "{" + ns + "}file-entry",
                    {
                        "{" + ns + "}full-path": "opaque/stage.bin",
                        "{" + ns + "}media-type": "application/octet-stream",
                    },
                )
                value = ET.tostring(root, encoding="utf-8", xml_declaration=True)
            z.writestr(item, value)
        z.writestr("opaque/stage.bin", b"stage original package payload\0\xff")
    return path


def entries(path):
    with zipfile.ZipFile(path) as z:
        return {name: z.read(name) for name in z.namelist()}


def verify_saved(source, saved, rectangle_delta):
    original, current = entries(source), entries(saved)
    assert original.keys() == current.keys()
    for name in original:
        if name != "content.xml":
            assert original[name] == current[name], name
    a, b = ET.fromstring(original["content.xml"]), ET.fromstring(current["content.xml"])
    # This fixture's one edited object is a root rectangle. All other source
    # data, including nested text spans, groups, geometry and image data, agrees.
    target = b.find(".//" + DRAW + "rect[@" + DRAW + 'name="Rectangle"]')
    value = target.attrib.pop(DRAW + "transform")
    match = re.fullmatch(
        r"matrix\(1 0 0 1 0 0\) translate\(([-+\d.e]+)pt ([-+\d.e]+)pt\)", value
    )
    assert match, value
    assert all(
        math.isclose(float(got), want, abs_tol=1e-8)
        for got, want in zip(match.groups(), rectangle_delta)
    )

    def normalized(node):
        return (
            node.tag,
            sorted(node.attrib.items()),
            node.text or "",
            node.tail or "",
            [normalized(c) for c in node],
        )

    assert normalized(a) == normalized(b), "Unrelated presentation XML changed"
    assert current["opaque/stage.bin"] == b"stage original package payload\0\xff"
    print(
        "PASS independent raw ODP movement, rich source and package preservation",
        flush=True,
    )
