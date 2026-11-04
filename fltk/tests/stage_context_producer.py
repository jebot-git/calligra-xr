#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent XML/default/target-retention oracle and context CUPS comparisons.

Fixtures are emitted by the native context test. This script checks their XML,
retained target state and rendered output independently. These are scoped style
materialization specimens, not an implemented Stage paste workflow.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import zipfile

from lxml import etree as ET
from PIL import Image
from pypdf import PdfReader

from cups_runtime import PrivateCups
import print_producer

OFFICE = "urn:oasis:names:tc:opendocument:xmlns:office:1.0"
STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0"
DRAW = "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
TEXT = "urn:oasis:names:tc:opendocument:xmlns:text:1.0"
SVG = "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"


def freeze(node):
    return (
        node.tag,
        sorted(node.attrib.items()),
        node.text,
        [(freeze(child), child.tail) for child in node],
    )


def read(path):
    with zipfile.ZipFile(path) as package:
        assert package.testzip() is None
        return {name: package.read(name) for name in package.namelist()}


def definitions(root):
    result = {}
    for group in root:
        if group.tag not in [
            f"{{{OFFICE}}}{n}"
            for n in ("styles", "automatic-styles", "font-face-decls")
        ]:
            continue
        for node in group:
            name = node.get(f"{{{STYLE}}}name")
            if name:
                key = (group.tag, node.tag, node.get(f"{{{STYLE}}}family"), name)
                assert key not in result
                result[key] = freeze(node)
    return result


def retain(before, after):
    a, b = read(before), read(after)
    assert a.keys() == b.keys()
    for name in a.keys() - {"content.xml", "styles.xml", "META-INF/manifest.xml"}:
        assert a[name] == b[name], name
    for name in ("content.xml", "styles.xml"):
        x, y = ET.fromstring(a[name]), ET.fromstring(b[name])
        defaults_x = [
            freeze(n) for g in x for n in g if n.tag == f"{{{STYLE}}}default-style"
        ]
        defaults_y = [
            freeze(n) for g in y for n in g if n.tag == f"{{{STYLE}}}default-style"
        ]
        assert defaults_x == defaults_y
        old, new = definitions(x), definitions(y)
        assert all(new.get(k) == v for k, v in old.items())
    x, y = ET.fromstring(a["content.xml"]), ET.fromstring(b["content.xml"])
    for root in (x, y):
        chosen = [n for n in root.iter() if n.get(f"{{{DRAW}}}name") == "X"]
        assert len(chosen) == 1
        chosen[0].getparent().remove(chosen[0])
    assert freeze(x.find(f"{{{OFFICE}}}body")) == freeze(y.find(f"{{{OFFICE}}}body"))


def xml_oracle(docs):
    retain(
        docs / "optional14-destination-before.odp", docs / "optional14-materialized.odp"
    )
    retain(docs / "context-destination-before.odp", docs / "context-materialized.odp")
    retain(docs / "context-fallback-before.odp", docs / "context-fallback.odp")
    for name in ("context-materialized.odp", "context-fallback.odp"):
        package = read(docs / name)
        styles = ET.fromstring(package["styles.xml"])
        default = styles.find(
            f"{{{OFFICE}}}styles/{{{STYLE}}}default-style[@{{{STYLE}}}family='paragraph']"
        )
        assert default is not None
        assert (
            default.find(f"{{{STYLE}}}paragraph-properties").get(
                f"{{{STYLE}}}tab-stop-distance"
            )
            == "17pt"
        )
        for part in ("content.xml", "styles.xml"):
            root = ET.fromstring(package[part])
            assert not root.xpath(
                "//@text:relative-tab-stop-position", namespaces={"text": TEXT}
            )
            for p in root.iter(f"{{{STYLE}}}paragraph-properties"):
                if p.getparent().tag == f"{{{STYLE}}}style":
                    assert p.get(f"{{{STYLE}}}tab-stop-distance") is None
            for p in root.iter(f"{{{STYLE}}}text-properties"):
                value = p.get(f"{{{FO}}}language")
                if value:
                    assert re.fullmatch("[A-Za-z]{1,8}", value)
    fallback = read(docs / "context-fallback.odp")
    root = ET.fromstring(fallback["styles.xml"])
    unconstrained = [
        n
        for n in root.find(f"{{{OFFICE}}}font-face-decls")
        if n.get(f"{{{STYLE}}}name", "").startswith("Context")
    ]
    assert (
        len(unconstrained) == 1
        and unconstrained[0].get(f"{{{SVG}}}font-family") is None
    )
    for path in docs.glob("selected-*.odp"):
        package = read(path)
        assert "meta.xml" not in package and "opaque/source" not in package
        assert not any(b"UNSELECTED_" in data for data in package.values())
    return dict(
        receiving_defaults_and_definitions_unchanged=True,
        unrelated_receiving_xml_and_parts_unchanged=True,
        default_tab_interval17_retained=True,
        no_invalid_named_default_or_origin_properties=True,
        language_and_country_separate=True,
        native_unconstrained_font_policy_retained=True,
        selected_outputs_exclude_unselected_data=True,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    args = parser.parse_args()
    bins, art = args.bin_dir.resolve(), args.artifacts.resolve()
    art.mkdir(parents=True, exist_ok=True)
    docs = art / "documents"
    subprocess.run(
        [str(bins / "calligra-native-stage-context-test"), "--documents", str(docs)],
        check=True,
    )
    oracle = xml_oracle(docs)
    env = {
        **os.environ,
        "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap",
        "LC_ALL": "C",
    }
    names = (
        "selected-source",
        "selected-materialized",
        "selected-fallback-source",
        "selected-fallback",
        "selected-presentation-source",
        "selected-presentation-materialized",
        "selected-nested-contexts",
        "selected-optional14-materialized",
    )
    with PrivateCups(art / "cups") as cups:
        print_producer.verify(bins, art / "suite", cups=cups, bitmap_fonts=True)
        env["CUPS_SERVER"] = cups.server
        for name in names:
            original, saved, output = (
                docs / (name + ".odp"),
                art / (name + "-saved.odp"),
                art / (name + ".pdf"),
            )
            digest = hashlib.sha256(original.read_bytes()).hexdigest()
            run = subprocess.run(
                [
                    str(bins / "calligra-stage-fltk"),
                    str(original),
                    "--export-odf",
                    str(saved),
                    "--print-pdf",
                    str(output),
                    "--print-to",
                    cups.name,
                ],
                env=env,
                capture_output=True,
                timeout=90,
            )
            (art / (name + "-print.log")).write_bytes(run.stdout + run.stderr)
            assert run.returncode == 0, run.stderr
            assert all(
                int(n) == 0 for n in re.findall(rb"(\d+) missing glyphs", run.stderr)
            ), run.stderr
            assert len(PdfReader(output).pages) == 1
            assert digest == hashlib.sha256(original.read_bytes()).hexdigest()
            reopened = art / (name + "-reopened.pdf")
            subprocess.run(
                [
                    str(bins / "calligra-stage-fltk"),
                    str(saved),
                    "--print-pdf",
                    str(reopened),
                ],
                env=env,
                check=True,
                capture_output=True,
                timeout=90,
            )
            for pdf in (output, reopened):
                subprocess.run(
                    [
                        "pdftoppm",
                        "-r",
                        "100",
                        "-png",
                        str(pdf),
                        str(pdf.with_suffix("")),
                    ],
                    check=True,
                    capture_output=True,
                )
            a, b = (
                Image.open(art / (name + "-1.png")).convert("RGBA"),
                Image.open(art / (name + "-reopened-1.png")).convert("RGBA"),
            )
            assert a.size == b.size and a.tobytes() == b.tobytes()
        for source, target in (
            ("selected-source", "selected-materialized"),
            ("selected-fallback-source", "selected-fallback"),
            ("selected-presentation-source", "selected-presentation-materialized"),
            ("selected-source", "selected-optional14-materialized"),
        ):
            a, b = (
                Image.open(art / (source + "-1.png")).convert("RGBA"),
                Image.open(art / (target + "-1.png")).convert("RGBA"),
            )
            assert a.size == b.size and a.tobytes() == b.tobytes(), (source, target)
        captured = cups.completed(11)
        assert (
            len(captured) == 11 and sum(len(PdfReader(p).pages) for p in captured) == 17
        )
    (art / "results.json").write_text(
        json.dumps(
            dict(
                xml_oracle=oracle,
                cups_jobs=11,
                page_candidates=17,
                four_source_materialized_pairs_exact=True,
                all_reopened_pages_exact=True,
                system_queues_changed=False,
                scope="Native known-property/context materialization and finite default tabs; full context/style graph/target merge/paste UI remain unfinished",
            ),
            indent=2,
        )
        + "\n"
    )
    print(
        "PASS receiving default/source retention oracle, four exact source/materialized raster pairs and eleven CUPS jobs/17 pages"
    )


if __name__ == "__main__":
    main()
