#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent image assets, ODP producer and source/package oracle."""

from pathlib import Path
import json
import math
import os
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from PIL import Image, ImageDraw
from stage_objects_producer import produce as objects_source, normalized, D, O, S, V, X
from stage_move_producer import entries

M = "{urn:oasis:names:tc:opendocument:xmlns:manifest:1.0}"
L = "{http://www.w3.org/1999/xlink}"
P = "{urn:oasis:names:tc:opendocument:xmlns:presentation:1.0}"
UI = (176, 105, 48, 30, 20, 10)
MODEL = (40, 80, 160, 100, 0, 0)


def produce(directory):
    directory = Path(directory)
    path = objects_source(directory)
    im = Image.new("RGBA", (64, 40), (20, 190, 210, 255))
    draw = ImageDraw.Draw(im)
    draw.rectangle((0, 0, 31, 19), fill=(240, 190, 20, 255))
    draw.rectangle((32, 20, 63, 39), fill=(190, 20, 160, 170))
    im.save(directory / "image.png")
    im.convert("RGB").save(directory / "image.jpg", quality=92)
    im.convert("RGB").save(directory / "image.bmp")
    im.save(directory / "image.gif")
    (directory / "image.svg").write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" width="64" height="40"><rect width="64" height="40" fill="#14bed2"/><path d="M0 0H32V20H0Z" fill="#f0be14"/><circle cx="48" cy="30" r="8" fill="#be14a0"/></svg>'
    )
    archive = entries(path)
    manifest = ET.fromstring(archive["META-INF/manifest.xml"])
    ET.SubElement(
        manifest,
        M + "file-entry",
        {M + "full-path": "Pictures/native-image-1.png", M + "media-type": "image/png"},
    )
    archive["META-INF/manifest.xml"] = ET.tostring(
        manifest, encoding="utf-8", xml_declaration=True
    )
    archive["Pictures/native-image-1.png"] = (
        directory / "image.png"
    ).read_bytes() + b"original unrelated asset"
    output = path.with_name("images.odp")
    with zipfile.ZipFile(output, "w") as z:
        for name, value in archive.items():
            z.writestr(
                name,
                value,
                compress_type=zipfile.ZIP_STORED
                if name == "mimetype"
                else zipfile.ZIP_DEFLATED,
            )
    path.unlink()
    return output


def verify_saved(original, saved, image, rect=UI):
    a, b = entries(original), entries(saved)
    image = Path(image)
    extension = image.suffix[1:]
    media = {
        "png": "image/png",
        "jpg": "image/jpeg",
        "gif": "image/gif",
        "bmp": "image/bmp",
        "svg": "image/svg+xml",
    }[extension]
    asset = f"Pictures/native-image-{2 if extension == 'png' else 1}.{extension}"
    assert b.keys() == a.keys() | {asset}
    assert b[asset] == image.read_bytes()
    for name in a:
        if name not in ["content.xml", "META-INF/manifest.xml"]:
            assert a[name] == b[name], name
    before, after = ET.fromstring(a["content.xml"]), ET.fromstring(b["content.xml"])
    oldpage = before.find(".//" + D + "page")
    page = after.find(".//" + D + "page")
    node = page.find(D + "frame[@" + D + 'name="NativeObject1"]')
    assert node is not None
    assert (
        node.get(X + "id") == "native-object-1"
        and node.get(D + "style-name") == "NativeGraphic1"
    )
    assert node.get(D + "z-index") == "5" and node.get(P + "user-transformed") == "true"
    x, y, w, h, dx, dy = rect
    for key, value in [("x", x), ("y", y), ("width", w), ("height", h)]:
        assert math.isclose(
            float(node.get(V + key).removesuffix("pt")), value, abs_tol=1e-8
        )
    if dx or dy:
        assert (
            node.get(D + "transform") == f"matrix(1 0 0 1 0 0) translate({dx}pt {dy}pt)"
        )
    else:
        assert D + "transform" not in node.attrib
    child = node.find(D + "image")
    assert child is not None and len(node) == 1
    assert child.attrib == {
        L + "href": asset,
        L + "type": "simple",
        L + "show": "embed",
        L + "actuate": "onLoad",
    }
    page.remove(node)
    assert (
        page.get(D + "nav-order") == oldpage.get(D + "nav-order") + " native-object-1"
    )
    page.set(D + "nav-order", oldpage.get(D + "nav-order"))
    auto = after.find(O + "automatic-styles")
    style = auto.find(S + "style[@" + S + 'name="NativeGraphic1"]')
    assert style is not None and style.get(S + "family") == "graphic"
    assert (
        len(style) == 1
        and style[0].tag == S + "graphic-properties"
        and style[0].attrib == {D + "fill": "none", D + "stroke": "none"}
    )
    auto.remove(style)
    assert normalized(before) == normalized(after), (
        "Unrelated drawing, rich text, style or source XML changed"
    )
    old, new = (
        ET.fromstring(a["META-INF/manifest.xml"]),
        ET.fromstring(b["META-INF/manifest.xml"]),
    )
    added = new.find(M + "file-entry[@" + M + 'full-path="' + asset + '"]')
    assert added is not None and added.attrib == {
        M + "full-path": asset,
        M + "media-type": media,
    }
    new.remove(added)
    assert normalized(old) == normalized(new), "Unrelated manifest data changed"
    print(
        "PASS independent embedded image geometry, IDs/navigation, exact asset and unrelated source/manifest preservation",
        flush=True,
    )


def verify(source, bin_dir, art):
    art = Path(art)
    art.mkdir(parents=True, exist_ok=True)
    results = []
    for extension in ["png", "jpg", "gif", "bmp", "svg"]:
        image = Path(source).parent / ("image." + extension)
        out = art / ("model-" + extension + ".odp")
        run = subprocess.run(
            [
                str(Path(bin_dir).resolve() / "calligra-native-stage-images-test"),
                "--document",
                str(source),
                str(image),
                str(out),
            ],
            env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
            capture_output=True,
            text=True,
        )
        (art / (extension + ".log")).write_text(run.stdout + run.stderr)
        run.check_returncode()
        verify_saved(source, out, image, MODEL)
        results.append(extension)
    (art / "results.json").write_text(json.dumps(results, indent=2) + "\n")
