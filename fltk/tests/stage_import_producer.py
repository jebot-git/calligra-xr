#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent scoped-reference/asset oracle and isolated-import CUPS checks.

This verifies an isolated import source. It does not claim destination default
isolation, an applied paste, or native clipboard controls.
"""

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import subprocess

from pypdf import PdfReader
from PIL import Image
from cups_runtime import PrivateCups
import print_producer
from stage_fragment_producer import N as BASE_NS, package, xml, pixels

N = {**BASE_NS, "fo": "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"}


def one(root, expression, **variables):
    values = root.xpath(expression, namespaces=N, **variables)
    assert len(values) == 1, (expression, variables, len(values))
    return values[0]


def attr(node, ns, name):
    return node.get("{" + N[ns] + "}" + name)


def oracle(docs):
    old, new = package(docs / "source.odp"), package(docs / "remapped.odp")
    content, styles = xml(new["content.xml"]), xml(new["styles.xml"])
    before = xml(old["content.xml"])
    source_nodes, actual_nodes = list(before.iter()), list(content.iter())
    assert len(source_nodes) == len(actual_nodes)
    identities = {}
    for a, b in zip(source_nodes, actual_nodes):
        assert a.tag == b.tag and a.text == b.text and a.tail == b.tail
        assert a.nsmap == b.nsmap
        for uri in ("http://www.w3.org/XML/1998/namespace", N["draw"]):
            if a.get("{" + uri + "}id"):
                original_id, new_id = a.get("{" + uri + "}id"), b.get("{" + uri + "}id")
                assert new_id.startswith("NativePaste")
                assert (
                    original_id not in identities or identities[original_id] == new_id
                )
                identities[original_id] = new_id
    page = one(content, "//draw:page")
    source_page = one(before, "//draw:page")
    assert attr(page, "draw", "nav-order").split() == [
        identities[x] for x in attr(source_page, "draw", "nav-order").split()
    ]
    original_objects = before.xpath(
        "//draw:rect|//draw:frame|//draw:line", namespaces=N
    )
    objects = content.xpath("//draw:rect|//draw:frame|//draw:line", namespaces=N)
    assert len(objects) == len(original_objects) == 4
    by_original = {
        attr(old_node, "draw", "name"): new_node
        for old_node, new_node in zip(original_objects, objects)
    }
    x = by_original["X"]
    # The source deliberately uses 'paint' for BOTH graphic and paragraph
    # automatic definitions. Their references must retain different families.
    graphic = one(
        content,
        "office:automatic-styles/style:style[@style:name=$name and @style:family='graphic']",
        name=attr(x, "draw", "style-name"),
    )
    paragraph = one(x, "text:p")
    paragraph_style = one(
        content,
        "office:automatic-styles/style:style[@style:name=$name and @style:family='paragraph']",
        name=attr(paragraph, "text", "style-name"),
    )
    assert attr(graphic, "style", "name") != attr(paragraph_style, "style", "name")
    base_graphic = one(
        styles,
        "office:styles/style:style[@style:name=$name and @style:family='graphic']",
        name=attr(graphic, "style", "parent-style-name"),
    )
    assert (
        attr(one(base_graphic, "style:graphic-properties"), "draw", "fill-color")
        == "#e04020"
    )
    assert attr(one(graphic, "style:graphic-properties"), "draw", "opacity") == "50%"
    base_paragraph = one(
        styles,
        "office:styles/style:style[@style:name=$name and @style:family='paragraph']",
        name=attr(paragraph_style, "style", "parent-style-name"),
    )
    one(
        styles,
        "office:styles/style:style[@style:name=$name and @style:family='paragraph']",
        name=attr(base_paragraph, "style", "next-style-name"),
    )
    # A common leader must bind common Emphasis (#abcdef), while body spans
    # bind content-automatic Emphasis (#2468ac). The styles-automatic red decoy
    # must not enter the copied dependency closure at all.
    leader = one(
        base_paragraph, "style:paragraph-properties/style:tab-stops/style:tab-stop"
    )
    leader_style = one(
        styles,
        "office:styles/style:style[@style:name=$name and @style:family='text']",
        name=attr(leader, "style", "leader-text-style"),
    )
    assert attr(one(leader_style, "style:text-properties"), "fo", "color") == "#abcdef"
    span = one(paragraph, "text:span")
    inline = one(
        content,
        "office:automatic-styles/style:style[@style:name=$name and @style:family='text']",
        name=attr(span, "text", "style-name"),
    )
    assert attr(one(inline, "style:text-properties"), "fo", "color") == "#2468ac"
    assert b"#ff0000" not in new["content.xml"] + new["styles.xml"]
    content_font = one(
        content,
        "office:font-face-decls/style:font-face[@style:name=$name]",
        name=attr(one(paragraph_style, "style:text-properties"), "style", "font-name"),
    )
    default_font = one(
        styles,
        "office:font-face-decls/style:font-face[@style:name=$name]",
        name=attr(
            one(
                styles,
                "office:styles/style:default-style[@style:family='paragraph']/style:text-properties",
            ),
            "style",
            "font-name",
        ),
    )
    assert attr(content_font, "svg", "font-family") == "monospace"
    assert attr(default_font, "svg", "font-family") == "fixed"
    assert attr(content_font, "style", "name") != attr(default_font, "style", "name")
    layers = {
        one(layer, "svg:title").text: layer
        for layer in page.xpath("draw:layer-set/draw:layer", namespaces=N)
    }
    assert attr(by_original["X"], "draw", "layer") == attr(
        layers["Art"], "draw", "name"
    )
    assert attr(by_original["N1"], "draw", "layer") == attr(
        layers["Printer"], "draw", "name"
    )
    assert attr(by_original["N2"], "draw", "layer") == attr(
        layers["Hidden"], "draw", "name"
    )
    image = one(by_original["Y"], "draw:image")
    path = attr(image, "xlink", "href")
    assert path.startswith("Objects/NativePaste5/")
    assert new[path] == old["Pictures/image.svg"]
    assert "Pictures/image.svg" not in new and "opaque/source" not in new
    assert b"UNSELECTED" not in b"".join(new.values())
    return dict(
        family_scopes=True,
        common_automatic_scopes=True,
        font_file_scopes=True,
        parent_next_and_leader_edges=True,
        layer_edges=True,
        selected_ids_and_navigation=True,
        exact_image_bytes=True,
        fresh_reserved_resource_root=True,
        unselected_data_excluded=True,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--test-bin", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    art = args.artifacts.resolve()
    art.mkdir(parents=True, exist_ok=True)
    docs = art / "documents"
    subprocess.run([str(args.test_bin.resolve()), "--documents", str(docs)], check=True)
    checks = oracle(docs)
    environment = {**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"}
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET"):
        environment.pop(key, None)
    binary = args.bin_dir.resolve() / "calligra-stage-fltk"
    with PrivateCups(art / "cups") as cups:
        print_producer.verify(args.bin_dir, art / "suite", cups=cups, bitmap_fonts=True)
        environment.update(CUPS_SERVER=cups.server, LC_ALL="C")
        for name in ("source", "remapped", "geometry-source", "geometry-remapped"):
            original, output, saved = (
                docs / (name + ".odp"),
                art / (name + ".pdf"),
                art / (name + "-saved.odp"),
            )
            digest = hashlib.sha256(original.read_bytes()).hexdigest()
            run = subprocess.run(
                [
                    str(binary),
                    str(original),
                    "--export-odf",
                    str(saved),
                    "--print-pdf",
                    str(output),
                    "--print-to",
                    cups.name,
                ],
                env=environment,
                capture_output=True,
                timeout=60,
            )
            (art / (name + "-print.log")).write_bytes(run.stdout + run.stderr)
            assert run.returncode == 0 and b"missing glyphs" not in run.stderr, (
                run.stderr
            )
            assert len(PdfReader(output).pages) == 1
            reopened = art / (name + "-reopened.pdf")
            subprocess.run(
                [str(binary), str(saved), "--print-pdf", str(reopened)],
                env=environment,
                check=True,
                capture_output=True,
                timeout=60,
            )
            for file, prefix in ((output, name), (reopened, name + "-reopened")):
                subprocess.run(
                    ["pdftoppm", "-r", "100", "-png", str(file), str(art / prefix)],
                    check=True,
                    capture_output=True,
                    timeout=60,
                )
            assert (art / (name + "-1.png")).read_bytes() == (
                art / (name + "-reopened-1.png")
            ).read_bytes()
            if name.startswith("geometry-"):
                image = Image.open(art / (name + "-1.png")).convert("RGB")

                def sample(x, y):
                    return image.getpixel((int(x * 100 / 72), int(y * 100 / 72)))

                # Equation edge65 in a100-unit view box limits a50pt shape
                # to32.5pt. Map local(15,65) and(45,20) through the declared
                # source position and affine matrix, independently of native
                # contour bounds. The second probe is outside that fill.
                assert (
                    max(
                        abs(a - b)
                        for a, b in zip(sample(143.5, 161.5), (240, 160, 144))
                    )
                    <= 2
                )
                assert sample(172, 118) == (255, 255, 255)
            else:
                pixels(art / (name + "-1.png"), True)
            assert hashlib.sha256(original.read_bytes()).hexdigest() == digest
        assert (art / "source-1.png").read_bytes() == (
            art / "remapped-1.png"
        ).read_bytes()
        assert (art / "geometry-source-1.png").read_bytes() == (
            art / "geometry-remapped-1.png"
        ).read_bytes()
        geometry = xml(package(docs / "geometry-remapped.odp")["content.xml"])
        assert attr(one(geometry, "//draw:equation"), "draw", "name") == "edge"
        assert attr(one(geometry, "//draw:glue-point"), "draw", "id") == "4"
        expected = [
            art / (name + ".pdf")
            for name in ("source", "remapped", "geometry-source", "geometry-remapped")
        ] + [art / "suite" / (name + ".pdf") for name in ("words", "sheets", "stage")]
        captured = cups.completed(7)
        assert Counter(
            hashlib.sha256(p.read_bytes()).hexdigest() for p in captured
        ) == Counter(hashlib.sha256(p.read_bytes()).hexdigest() for p in expected)
        assert sum(len(PdfReader(p).pages) for p in captured) == 13
    report = dict(
        xml_oracle=checks,
        cups_jobs=7,
        page_candidates=13,
        remapped_render_exact=True,
        all_reopened_pages_exact=True,
        independent_affine_opacity_layer_pixels=True,
        system_queues_changed=False,
        scope="Isolated source remapping only; destination defaults, merge and clipboard UI remain unfinished",
    )
    (art / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS independent import XML/asset/scope oracle and seven CUPS jobs/13 pages")


if __name__ == "__main__":
    main()
