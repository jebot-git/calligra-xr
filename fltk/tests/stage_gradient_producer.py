#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODF1.2 gradient corpus and SVG-consumer reference geometry."""

import math
import xml.etree.ElementTree as E
import zipfile
from pathlib import Path

O = "urn:oasis:names:tc:opendocument:xmlns:office:1.0"
D = "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
S = "urn:oasis:names:tc:opendocument:xmlns:style:1.0"
V = "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
F = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
P = "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0"
M = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0"
SVG = "http://www.w3.org/2000/svg"
WIDTH, HEIGHT = 600, 420
START, END = (224, 32, 64), (32, 64, 224)
SPECS = {
    "Linear": {"kind": "linear", "angle": 90, "border": 0.2},
    "Axial": {"kind": "axial", "border": 0.1},
    "Radial": {"kind": "radial", "angle": 90, "cx": 0.35, "cy": 0.65},
    "Ellipsoid": {"kind": "ellipsoid", "angle": 30},
    "Square": {"kind": "square", "angle": 45},
    "Rectangular": {"kind": "rectangular", "cx": 0.3, "cy": 0.7},
    "Stepped": {"kind": "linear", "angle": 90, "steps": 3},
    "Dimmed": {"kind": "radial", "start_intensity": 0.3, "end_intensity": 0.9},
}
AUTHORED = {
    "kind": "radial",
    "angle": 90,
    "cx": 0.25,
    "cy": 0.75,
    "border": 0.25,
    "start": (32, 64, 128),
    "end": (224, 192, 32),
    "start_intensity": 0.5,
    "end_intensity": 0.8,
    "steps": 5,
}
PAGES = [
    [
        (name, "rect", 20 + (i % 3) * 200, 20 + (i // 3) * 200, 160, 140, {})
        for i, name in enumerate(list(SPECS)[:6])
    ],
    [
        ("Linear", "path", 30, 40, 160, 120, {"opacity": 0.5, "hole": True}),
        (
            "Ellipsoid",
            "rect",
            280,
            60,
            200,
            120,
            {"transform": "matrix(0.9 0.25 -0.2 1 0 0)"},
        ),
        ("Stepped", "rect", 30, 240, 240, 90, {}),
        ("Dimmed", "ellipse", 350, 260, 180, 100, {"opacity": 0.65}),
    ],
    [],
    [],
]


def el(parent, ns, name, **attrs):
    return E.SubElement(parent, "{" + ns + "}" + name, attrs)


def attrs(ns, **values):
    return {"{" + ns + "}" + k.replace("_", "-"): str(v) for k, v in values.items()}


def rgb(value):
    return "#" + "".join(f"{max(0, min(255, round(x))):02x}" for x in value)


def endpoints(spec):
    return [
        tuple(v * spec.get(key, 1) for v in color)
        for key, color in [
            ("start_intensity", spec.get("start", START)),
            ("end_intensity", spec.get("end", END)),
        ]
    ]


def produce(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    styles = E.Element("{" + O + "}document-styles", attrs(O, version="1.2"))
    common = el(styles, O, "styles")
    for name, spec in SPECS.items():
        values = {
            "name": name,
            "style": spec["kind"],
            "angle": f"{spec.get('angle', 0)}deg",
            "border": f"{100 * spec.get('border', 0):g}%",
            "cx": f"{100 * spec.get('cx', 0.5):g}%",
            "cy": f"{100 * spec.get('cy', 0.5):g}%",
            "start_color": rgb(START),
            "end_color": rgb(END),
            "start_intensity": f"{100 * spec.get('start_intensity', 1):g}%",
            "end_intensity": f"{100 * spec.get('end_intensity', 1):g}%",
        }
        el(common, D, "gradient", **attrs(D, **values))
        style = el(common, S, "style", **attrs(S, name=name, family="graphic"))
        prop = {"fill": "gradient", "fill_gradient_name": name, "stroke": "none"}
        if "steps" in spec:
            prop["gradient_step_count"] = str(spec["steps"])
        el(style, S, "graphic-properties", **attrs(D, **prop))
    for name, paint in {
        "MasterSolid": {"fill": "solid", "fill_color": "#80a060"},
        "White": {"fill": "solid", "fill_color": "#ffffff"},
        "MasterGradient": {"fill": "gradient", "fill_gradient_name": "Axial"},
        "HalfGradient": {
            "fill": "gradient",
            "fill_gradient_name": "Linear",
            "opacity": "50%",
        },
    }.items():
        style = el(common, S, "style", **attrs(S, name=name, family="drawing-page"))
        el(style, S, "drawing-page-properties", **attrs(D, **paint))
    auto = el(styles, O, "automatic-styles")
    layout = el(auto, S, "page-layout", **attrs(S, name="Layout"))
    el(
        layout,
        S,
        "page-layout-properties",
        **attrs(F, page_width="600pt", page_height="420pt"),
    )
    masters = el(styles, O, "master-styles")
    for name, style in [
        ("Default", "MasterSolid"),
        ("GradientMaster", "MasterGradient"),
    ]:
        el(
            masters,
            S,
            "master-page",
            **attrs(S, name=name, page_layout_name="Layout"),
            **attrs(D, style_name=style),
        )
    content = E.Element("{" + O + "}document-content", attrs(O, version="1.2"))
    auto = el(content, O, "automatic-styles")
    no_master = el(
        auto,
        S,
        "style",
        **attrs(
            S,
            name="HiddenMaster",
            family="drawing-page",
            parent_style_name="HalfGradient",
        ),
    )
    el(no_master, S, "drawing-page-properties", **attrs(P, background_visible="false"))
    body = el(el(content, O, "body"), O, "presentation")
    for index, shapes in enumerate(PAGES):
        page_attrs = {
            "name": f"Gradient page {index + 1}",
            "master_page_name": "GradientMaster" if index == 2 else "Default",
        }
        if index in [0, 1, 3]:
            page_attrs["style_name"] = ["White", "HalfGradient", "", "HiddenMaster"][
                index
            ]
        page = el(body, D, "page", **attrs(D, **page_attrs))
        for i, (name, kind, x, y, w, h, options) in enumerate(shapes):
            style_name = name
            if options.get("opacity") is not None or options.get("hole"):
                style_name = f"Local{index}_{i}"
                style = el(
                    auto,
                    S,
                    "style",
                    **attrs(
                        S, name=style_name, family="graphic", parent_style_name=name
                    ),
                )
                prop = attrs(D, opacity=f"{100 * options.get('opacity', 1):g}%")
                if options.get("hole"):
                    prop.update(attrs(V, fill_rule="evenodd"))
                el(style, S, "graphic-properties", **prop)
            a = attrs(D, name=f"{name} {index}_{i}", style_name=style_name)
            a.update(attrs(V, x=f"{x}pt", y=f"{y}pt", width=f"{w}pt", height=f"{h}pt"))
            if options.get("transform"):
                a.update(attrs(D, transform=options["transform"]))
            if kind == "path":
                a.update(
                    attrs(
                        V, viewBox="0 0 100 100", d="M0 0H100V100H0Z M25 25H75V75H25Z"
                    )
                )
            el(page, D, kind, **a)
    manifest = E.Element("{" + M + "}manifest", attrs(M, version="1.2"))
    mime = "application/vnd.oasis.opendocument.presentation"
    for path, media in [
        ("/", mime),
        ("content.xml", "text/xml"),
        ("styles.xml", "text/xml"),
    ]:
        el(manifest, M, "file-entry", **attrs(M, full_path=path, media_type=media))
    path = directory / "gradients.odp"
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("mimetype", mime, compress_type=zipfile.ZIP_STORED)
        for name, root in [
            ("content.xml", content),
            ("styles.xml", styles),
            ("META-INF/manifest.xml", manifest),
        ]:
            z.writestr(name, E.tostring(root, encoding="utf-8", xml_declaration=True))
    return path


def svg_gradient(defs, key, spec, w, h):
    """Use SVG-native ramps/ellipses and nested rectangles, not native samples."""
    angle = math.radians(spec.get("angle", 0))
    c, s = math.cos(angle), math.sin(angle)
    border = spec.get("border", 0)
    start, end = endpoints(spec)
    kind = spec["kind"]
    if kind in ["linear", "axial"]:
        extent = abs(s) * w + abs(c) * h
        grad = el(
            defs,
            SVG,
            "linearGradient",
            id=key,
            gradientUnits="userSpaceOnUse",
            x1=str(w / 2 - s * extent / 2),
            y1=str(h / 2 - c * extent / 2),
            x2=str(w / 2 + s * extent / 2),
            y2=str(h / 2 + c * extent / 2),
        )
        if kind == "axial":
            stops = [
                (0, end),
                (border / 2, end),
                (0.5, start),
                (1 - border / 2, end),
                (1, end),
            ]
        else:
            stops = [(0, start), (border, start), (1, end)]
        if spec.get("steps"):
            stops = []
            n = spec["steps"]
            for i in range(n):
                col = tuple(a + (b - a) * i / (n - 1) for a, b in zip(start, end))
                stops.extend([(i / n, col), ((i + 1) / n, col)])
        for offset, col in stops:
            el(grad, SVG, "stop", offset=str(offset), **{"stop-color": rgb(col)})
    else:
        cx, cy = spec.get("cx", 0.5) * w, spec.get("cy", 0.5) * h
        if kind in ["radial", "ellipsoid"]:
            rx, ry = (
                (math.hypot(w, h) / 2,) * 2
                if kind == "radial"
                else (w / math.sqrt(2), h / math.sqrt(2))
            )
            if kind == "radial":
                c, s = 1, 0
            rx, ry = rx * (1 - border), ry * (1 - border)
            grad = el(
                defs,
                SVG,
                "radialGradient",
                id=key,
                gradientUnits="userSpaceOnUse",
                cx="0",
                cy="0",
                r="1",
                gradientTransform=f"matrix({c * rx} {-s * rx} {s * ry} {c * ry} {cx} {cy})",
            )
            stops = [(0, end), (1, start)]
            if spec.get("steps"):
                stops = []
                n = spec["steps"]
                for i in range(n):
                    col = tuple(
                        a + (b - a) * (n - i - 1) / (n - 1) for a, b in zip(start, end)
                    )
                    stops.extend([(i / n, col), ((i + 1) / n, col)])
            for offset, col in stops:
                el(grad, SVG, "stop", offset=str(offset), **{"stop-color": rgb(col)})
        else:
            grad = el(
                defs,
                SVG,
                "pattern",
                id=key,
                patternUnits="userSpaceOnUse",
                width=str(w),
                height=str(h),
            )
            el(grad, SVG, "rect", width=str(w), height=str(h), fill=rgb(start))
            if kind == "square":
                w0 = h0 = max(w, h)
            else:
                w0, h0 = w, h
            rx = (abs(c) * w0 + abs(s) * h0) / 2 * (1 - border)
            ry = (abs(s) * w0 + abs(c) * h0) / 2 * (1 - border)
            group = el(grad, SVG, "g", transform=f"matrix({c} {-s} {s} {c} {cx} {cy})")
            for i in range(256):
                inset = min(rx, ry) * i / 256
                col = tuple(a + (b - a) * i / 255 for a, b in zip(start, end))
                el(
                    group,
                    SVG,
                    "rect",
                    x=str(-rx + inset),
                    y=str(-ry + inset),
                    width=str(2 * (rx - inset)),
                    height=str(2 * (ry - inset)),
                    fill=rgb(col),
                )
    return "url(#" + key + ")"


def reference_svg(page, authored=False):
    root = E.Element(
        "{" + SVG + "}svg",
        {
            "width": str(WIDTH),
            "height": str(HEIGHT),
            "viewBox": f"0 0 {WIDTH} {HEIGHT}",
        },
    )
    defs = el(root, SVG, "defs")
    el(root, SVG, "rect", width=str(WIDTH), height=str(HEIGHT), fill="white")
    if page in [1, 2]:
        el(
            root,
            SVG,
            "rect",
            width=str(WIDTH),
            height=str(HEIGHT),
            fill="#80a060"
            if page == 1
            else svg_gradient(defs, "master", SPECS["Axial"], WIDTH, HEIGHT),
        )
    if page in [1, 3]:
        el(
            root,
            SVG,
            "rect",
            width=str(WIDTH),
            height=str(HEIGHT),
            fill=svg_gradient(defs, "foreground", SPECS["Linear"], WIDTH, HEIGHT),
            opacity="0.5",
        )
    for i, (name, kind, x, y, w, h, opt) in enumerate(PAGES[page]):
        group = el(
            root, SVG, "g", transform=opt.get("transform", "") + f" translate({x} {y})"
        )
        spec = AUTHORED if authored and page == 0 and i == 0 else SPECS[name]
        fill = svg_gradient(defs, f"object{i}", spec, w, h)
        a = {"fill": fill, "opacity": str(opt.get("opacity", 1))}
        if kind == "path":
            a.update(
                d=f"M0 0H{w}V{h}H0Z M{w / 4} {h / 4}H{3 * w / 4}V{3 * h / 4}H{w / 4}Z",
                **{"fill-rule": "evenodd"},
            )
        elif kind == "ellipse":
            a.update(cx=str(w / 2), cy=str(h / 2), rx=str(w / 2), ry=str(h / 2))
        else:
            a.update(width=str(w), height=str(h))
        el(group, SVG, kind, **a)
    return E.tostring(root, encoding="utf-8", xml_declaration=True)


def verify_saved(original, saved, authored=False):
    def entries(path):
        with zipfile.ZipFile(path) as z:
            return {n: z.read(n) for n in z.namelist()}

    def normalized(n):
        return n.tag, sorted(n.attrib.items()), n.text or "", [normalized(c) for c in n]

    original_parts, saved_parts = entries(original), entries(saved)
    assert original_parts.keys() == saved_parts.keys()
    for name, data in original_parts.items():
        if name not in ["content.xml", "styles.xml"]:
            assert saved_parts[name] == data, name
    content, styles = (
        E.fromstring(saved_parts["content.xml"]),
        E.fromstring(saved_parts["styles.xml"]),
    )
    original_content, original_styles = (
        E.fromstring(original_parts["content.xml"]),
        E.fromstring(original_parts["styles.xml"]),
    )
    if authored:
        original_object = next(
            n
            for n in original_content.iter("{" + D + "}rect")
            if n.get("{" + D + "}name") == "Linear 0_0"
        )
        obj = next(
            n
            for n in content.iter("{" + D + "}rect")
            if n.get("{" + D + "}name") == "Linear 0_0"
        )
        name = obj.get("{" + D + "}style-name")
        assert name != original_object.get("{" + D + "}style-name")
        automatic = content.find("{" + O + "}automatic-styles")
        new_style = next(n for n in automatic if n.get("{" + S + "}name") == name)
        assert new_style.get("{" + S + "}family") == "graphic"
        assert new_style.get("{" + S + "}parent-style-name") == "Linear"
        props = new_style.find("{" + S + "}graphic-properties")
        assert props.attrib.keys() == {
            "{" + D + "}" + n
            for n in ["fill", "fill-gradient-name", "gradient-step-count"]
        }
        assert props.get("{" + D + "}fill") == "gradient"
        assert props.get("{" + D + "}gradient-step-count") == "5"
        resource = props.get("{" + D + "}fill-gradient-name")
        common = styles.find("{" + O + "}styles")
        gradient = next(
            n
            for n in common
            if n.tag == "{" + D + "}gradient" and n.get("{" + D + "}name") == resource
        )
        expected = {
            "style": "radial",
            "start_color": "#204080",
            "end_color": "#e0c020",
            "start_intensity": "50%",
            "end_intensity": "80%",
            "border": "25%",
            "cx": "25%",
            "cy": "75%",
        }
        for key, value in attrs(D, **expected).items():
            assert gradient.get(key) == value, (key, gradient.attrib)
        angle = gradient.get("{" + D + "}angle")
        assert angle.endswith("rad") and abs(float(angle[:-3]) - math.pi / 2) < 1e-12
        common.remove(gradient)
        automatic.remove(new_style)
        obj.set("{" + D + "}style-name", original_object.get("{" + D + "}style-name"))
    assert normalized(content) == normalized(original_content), (
        "Unrelated source/geometry changed"
    )
    assert normalized(styles) == normalized(original_styles), (
        "Unrelated resource/style changed"
    )
