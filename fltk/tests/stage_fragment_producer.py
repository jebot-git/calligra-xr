#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent XML/ZIP closure oracle and actual fragment PDF/CUPS regression."""

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile
from lxml import etree as ET
from PIL import Image
from pypdf import PdfReader
from cups_runtime import PrivateCups
import print_producer

N = {
    "office": "urn:oasis:names:tc:opendocument:xmlns:office:1.0",
    "draw": "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0",
    "text": "urn:oasis:names:tc:opendocument:xmlns:text:1.0",
    "style": "urn:oasis:names:tc:opendocument:xmlns:style:1.0",
    "svg": "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0",
    "xlink": "http://www.w3.org/1999/xlink",
}


def package(path):
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None
        return {name: z.read(name) for name in z.namelist()}


def xml(data):
    return ET.fromstring(data, ET.XMLParser(resolve_entities=False, no_network=True))


def freeze(node):
    return (
        node.tag,
        sorted(node.attrib.items()),
        node.text,
        [(freeze(child), child.tail) for child in node],
    )


def oracle(directory):
    original = package(directory / "source.odp")
    source_content, source_styles = (
        xml(original["content.xml"]),
        xml(original["styles.xml"]),
    )
    source_x = source_content.xpath("//draw:rect[@draw:name='X']", namespaces=N)[0]
    source_group = source_x.getparent()
    records = []
    for name, expected_names in [
        ("selected-x", ["X"]),
        ("selected-pair", ["X", "Y"]),
        ("selected-group", ["X", "Y", "N1", "N2"]),
    ]:
        data = package(directory / (name + ".odp"))
        content, styles = xml(data["content.xml"]), xml(data["styles.xml"])
        assert len(content.xpath("//draw:page", namespaces=N)) == 1
        objects = content.xpath("//draw:rect|//draw:frame|//draw:line", namespaces=N)
        assert [
            node.get("{" + N["draw"] + "}name") for node in objects
        ] == expected_names
        x = content.xpath("//draw:rect[@draw:name='X']", namespaces=N)[0]
        assert freeze(x) == freeze(source_x)
        assert dict(x.getparent().attrib) == dict(source_group.attrib)
        assert x.getparent().get("{" + N["draw"] + "}transform") is None
        assert (
            x.get("{" + N["draw"] + "}transform")
            == "matrix(.8 .2 -.1 1.1 0 0) translate(30pt 15pt)"
        )
        # Every included style definition is the exact expanded-name subtree
        # from its proper source scope, including parent/next and inline styles.
        for target, source in [(content, source_content), (styles, source_styles)]:
            for node in target.xpath(
                "//style:style|//style:default-style|//style:font-face", namespaces=N
            ):
                style_name = node.get("{" + N["style"] + "}name")
                family = node.get("{" + N["style"] + "}family")
                matches = [
                    candidate
                    for candidate in source.iter(node.tag)
                    if candidate.get("{" + N["style"] + "}name") == style_name
                    and candidate.get("{" + N["style"] + "}family") == family
                ]
                assert len(matches) == 1 and freeze(node) == freeze(matches[0])
        assert b"UNSELECTED" not in b"".join(data.values())
        assert "opaque/source" not in data and "meta.xml" not in data
        automatic = content.xpath("//office:automatic-styles/style:style", namespaces=N)
        assert {node.get("{" + N["style"] + "}name") for node in automatic} == {
            "paint",
            "P",
        }
        common = styles.xpath("//office:styles/style:style", namespaces=N)
        assert {node.get("{" + N["style"] + "}name") for node in common} == {
            "CommonGraphic",
            "CommonParagraph",
            "AfterParagraph",
            "Emphasis",
        }
        faces = styles.xpath("//style:font-face", namespaces=N)
        assert [node.get("{" + N["style"] + "}name") for node in faces] == ["DocBitmap"]
        layers = content.xpath("//draw:layer", namespaces=N)
        layer_names = {node.get("{" + N["draw"] + "}name") for node in layers}
        assert layer_names == (
            {"Art", "Hidden", "Printer"} if name == "selected-group" else {"Art"}
        )
        expected_parts = {
            "mimetype",
            "content.xml",
            "styles.xml",
            "META-INF/manifest.xml",
        }
        if name != "selected-x":
            expected_parts.add("Pictures/image.svg")
            assert data["Pictures/image.svg"] == original["Pictures/image.svg"]
        assert set(data) == expected_parts
        records.append(
            dict(
                file=str(directory / (name + ".odp")),
                selected_names=expected_names,
                exact_style_subtrees=True,
                exact_object_subtree=True,
                affine_transform_and_ancestor_context_retained=True,
                no_unselected_parts=True,
            )
        )
    return records


def pixels(path, group):
    image = Image.open(path).convert("RGB")

    def sample(x, y):
        return image.getpixel((int(x * 100 / 72), int(y * 100 / 72)))

    # Independently derived from matrix(.8 .2 -.1 1.1 30 15):
    # center(165,80)->(154,136), red #e04020 at50% over opaque white.
    assert max(abs(a - b) for a, b in zip(sample(154, 136), (240, 160, 144))) <= 2
    assert sample(125, 230) == (255, 255, 255), "Hidden member appeared in print"
    if group:
        # Printer-only N1 midpoint(55,170)->(57,213), opaque #102030 stroke.
        assert max(abs(a - b) for a, b in zip(sample(57, 213), (16, 32, 48))) <= 12


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
    records = oracle(docs)
    environment = {**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"}
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET"):
        environment.pop(key, None)
    binary = args.bin_dir.resolve() / "calligra-stage-fltk"
    pages = []
    with PrivateCups(art / "cups") as cups:
        print_producer.verify(args.bin_dir, art / "suite", cups=cups, bitmap_fonts=True)
        environment.update(CUPS_SERVER=cups.server, LC_ALL="C")
        for name, count in [
            ("source", 2),
            ("selected-x", 1),
            ("selected-pair", 1),
            ("selected-group", 1),
        ]:
            source = docs / (name + ".odp")
            original = hashlib.sha256(source.read_bytes()).hexdigest()
            output, saved = art / (name + ".pdf"), art / (name + "-saved.odp")
            run = subprocess.run(
                [
                    str(binary),
                    str(source),
                    "--export-odf",
                    str(saved),
                    "--print-pdf",
                    str(output),
                    "--print-to",
                    cups.name,
                ],
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            (art / (name + "-print.log")).write_bytes(run.stdout + run.stderr)
            assert run.returncode == 0, run.stderr.decode(errors="replace")
            assert len(PdfReader(output).pages) == count
            reopened = art / (name + "-reopened.pdf")
            subprocess.run(
                [str(binary), str(saved), "--print-pdf", str(reopened)],
                env=environment,
                check=True,
                capture_output=True,
            )
            for file, prefix in [(output, name), (reopened, name + "-reopened")]:
                subprocess.run(
                    ["pdftoppm", "-r", "100", "-png", str(file), str(art / prefix)],
                    check=True,
                    capture_output=True,
                )
            for i in range(1, count + 1):
                image = art / f"{name}-{i}.png"
                assert (
                    image.read_bytes()
                    == (art / f"{name}-reopened-{i}.png").read_bytes()
                )
                if i == 1:
                    pixels(image, name in ("source", "selected-group"))
                pages.append(str(image))
            assert hashlib.sha256(source.read_bytes()).hexdigest() == original
        captured = cups.completed(7)
        expected = [
            art / (name + ".pdf")
            for name in ("source", "selected-x", "selected-pair", "selected-group")
        ]
        expected += [
            art / "suite" / (name + ".pdf") for name in ("words", "sheets", "stage")
        ]
        assert Counter(
            hashlib.sha256(p.read_bytes()).hexdigest() for p in captured
        ) == Counter(hashlib.sha256(p.read_bytes()).hexdigest() for p in expected)
        assert sum(len(PdfReader(p).pages) for p in captured) == 14
    result = dict(
        xml_oracle=records,
        cups_jobs=7,
        reviewed_page_candidates=14,
        fragment_pages=pages,
        all_reopened_pages_identical=True,
        independent_affine_opacity_layer_pixels=True,
        system_queues_changed=False,
        scope="selected-object extraction and printing; paste and UI remain unfinished",
    )
    (art / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(
        "PASS independent fragment XML/assets/style oracle and seven CUPS jobs/14 pages"
    )


if __name__ == "__main__":
    main()
