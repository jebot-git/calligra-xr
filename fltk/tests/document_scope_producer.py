#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare surrounding document output with colliding embedded ODF declarations.

Native hand-authored fixtures exercise the readers. This independent ZIP/XML,
raster and private CUPS oracle checks surrounding output and source retention;
it does not establish embedded-document rendering or clipboard completion.
"""

import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import zipfile

from lxml import etree as ET
from PIL import Image
from pypdf import PdfReader

from cups_runtime import PrivateCups
import print_producer

OFFICE = "urn:oasis:names:tc:opendocument:xmlns:office:1.0"


def freeze(node):
    return (
        node.tag,
        sorted(node.attrib.items()),
        node.text,
        [(freeze(child), child.tail) for child in node],
    )


def nested(path):
    found = []
    with zipfile.ZipFile(path) as package:
        for part in ("content.xml", "styles.xml"):
            root = ET.fromstring(package.read(part))
            found.extend(freeze(n) for n in root.iter(f"{{{OFFICE}}}document"))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    args = parser.parse_args()
    bins, art = args.bin_dir.resolve(), args.artifacts.resolve()
    art.mkdir(parents=True, exist_ok=True)
    docs = art / "documents"
    subprocess.run(
        [str(bins / "calligra-native-document-scope-test"), "--documents", str(docs)],
        check=True,
    )
    environment = {
        **os.environ,
        "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap",
        "LC_ALL": "C",
    }
    checks = []
    with PrivateCups(art / "cups") as cups:
        print_producer.verify(bins, art / "suite", cups=cups, bitmap_fonts=True)
        environment["CUPS_SERVER"] = cups.server
        for app, ext in (("words", ".odt"), ("stage", ".odp")):
            binary = bins / ("calligra-" + app + "-fltk")
            for variant in ("baseline", "content", "styles"):
                name = app + "-" + variant
                source, saved = docs / (name + ext), art / (name + "-saved" + ext)
                before = hashlib.sha256(source.read_bytes()).hexdigest()
                output, reopened = art / (name + ".pdf"), art / (name + "-reopened.pdf")
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
                    capture_output=True,
                    timeout=60,
                )
                (art / (name + "-print.log")).write_bytes(run.stdout + run.stderr)
                assert run.returncode == 0, run.stderr
                assert all(
                    int(n) == 0
                    for n in re.findall(rb"(\d+) missing glyphs", run.stderr)
                ), run.stderr
                assert len(PdfReader(output).pages) == 1
                assert nested(source) == nested(saved)
                assert len(nested(saved)) == (0 if variant == "baseline" else 1)
                assert before == hashlib.sha256(source.read_bytes()).hexdigest()
                with zipfile.ZipFile(source) as a, zipfile.ZipFile(saved) as b:
                    assert a.read("styles.xml") == b.read("styles.xml")
                subprocess.run(
                    [str(binary), str(saved), "--print-pdf", str(reopened)],
                    env=environment,
                    capture_output=True,
                    check=True,
                    timeout=60,
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
                first = Image.open(art / (name + "-1.png")).convert("RGBA")
                second = Image.open(art / (name + "-reopened-1.png")).convert("RGBA")
                assert first.size == second.size and first.tobytes() == second.tobytes()
                if variant != "baseline":
                    baseline = Image.open(art / (app + "-baseline-1.png")).convert(
                        "RGBA"
                    )
                    assert (
                        baseline.size == first.size
                        and baseline.tobytes() == first.tobytes()
                    )
                checks.append(
                    dict(
                        app=app,
                        variant=variant,
                        source_unchanged=True,
                        nested_xml_retained=True,
                        styles_part_exact=True,
                        reopened_raster_exact=True,
                        surrounding_raster_exact=True,
                    )
                )
        captured = cups.completed(9)
        assert (
            len(captured) == 9 and sum(len(PdfReader(p).pages) for p in captured) == 15
        )
    (art / "results.json").write_text(
        json.dumps(
            dict(
                checks=checks,
                cups_jobs=9,
                page_candidates=15,
                system_queues_changed=False,
                desktop_clipboard_accessed=False,
                scope="Surrounding document scopes only; embedded rendering and actual paste remain unfinished",
            ),
            indent=2,
        )
        + "\n"
    )
    print(
        "PASS document scope ZIP/XML retention, six exact reopen/baseline rasters and nine CUPS jobs/15 pages"
    )


if __name__ == "__main__":
    main()
