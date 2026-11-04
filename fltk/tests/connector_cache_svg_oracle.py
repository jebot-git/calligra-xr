#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Render edited cached SVG paths without native endpoint/attachment resolution."""

import argparse
import io
import json
import xml.etree.ElementTree as ET
from pathlib import Path

import cairosvg
from PIL import Image
from stage_connector_edit_producer import INK, D, V, entries

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--ui-artifacts", type=Path, required=True)
p.add_argument("--artifacts", type=Path, required=True)
a = p.parse_args()
art = a.artifacts.resolve()
art.mkdir(parents=True, exist_ok=True)
results = []
for case in ["move", "resize", "group", "enhanced"]:
    source = a.ui_artifacts / f"native-{case}.odp"
    content = ET.fromstring(entries(source)["content.xml"])
    names = (
        {"EditLink", "EditChain"}
        if case in {"move", "resize"}
        else {"EditGroupLink" if case == "group" else "EditEnhancedLink"}
    )
    svg = ET.Element(
        "svg", {"xmlns": "http://www.w3.org/2000/svg", "viewBox": "0 0 420 300"}
    )
    ET.SubElement(svg, "rect", {"width": "420", "height": "300", "fill": "white"})
    for node in content.iter(D + "connector"):
        if node.get(D + "name") not in names:
            continue
        assert not node.get(D + "transform"), (
            "This oracle's fixture has untransformed connector frames"
        )
        ET.SubElement(
            svg,
            "path",
            {
                "d": node.attrib[V + "d"],
                "transform": "scale(0.028346456692913385)",
                "fill": "none",
                "stroke": "#203060",
                "stroke-width": str(4 * 2540 / 72),
                "stroke-opacity": "0.5",
            },
        )
    raw = ET.tostring(svg)
    (art / f"{case}.svg").write_bytes(raw)
    image = Image.open(
        io.BytesIO(
            cairosvg.svg2png(bytestring=raw, output_width=840, output_height=600)
        )
    ).convert("RGB")
    image.save(art / f"{case}.png")
    if case in {"move", "resize"}:
        probes = [
            (190 if case == "move" else 195, 130, INK),
            (185, 130, (255, 255, 255)),
        ]
    elif case == "group":
        probes = [(250, 230, INK), (240, 230, (255, 255, 255))]
    else:
        target = next(n for n in content.iter() if n.get(D + "name") == "EditEnhanced")
        modifier = float(target.find(D + "enhanced-geometry").get(D + "modifiers"))
        probes = [((30 + modifier * 0.5 + 320) / 2, 180, INK)]
    for x, y, expected in probes:
        got = image.getpixel((round(x * 2), round(y * 2)))
        assert all(abs(a - b) <= 2 for a, b in zip(got, expected)), (
            case,
            x,
            y,
            got,
            expected,
        )
    results.append(
        {
            "case": case,
            "independent_svg_pixels": True,
            "native_attachment_resolution_used": False,
        }
    )
(art / "results.json").write_text(
    json.dumps(
        {
            "cases": results,
            "renderer": "CairoSVG/cairo",
            "scope": "Selected edited fixture paths in their conventional mm/100 coordinates; not whole-consumer interoperability",
        },
        indent=2,
    )
    + "\n"
)
print("PASS independent cached SVG paths for movement/resize/group/enhanced edits")
