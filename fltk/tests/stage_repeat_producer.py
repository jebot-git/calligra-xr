#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent table-instance XML, exact render/reopen and CUPS checks.

The source selection is pruned independently in Python from the original
fixture, rather than derived from the native expansion output.
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
DRAW = "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
TABLE = "urn:oasis:names:tc:opendocument:xmlns:table:1.0"
TEXT = "urn:oasis:names:tc:opendocument:xmlns:text:1.0"
XML = "http://www.w3.org/XML/1998/namespace"
XLINK = "http://www.w3.org/1999/xlink"


def read(path):
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None
        return {name: z.read(name) for name in z.namelist()}


def selected_source(source, target):
    parts = read(source)
    root = ET.fromstring(parts["content.xml"])
    presentation = root.find(f"{{{OFFICE}}}body/{{{OFFICE}}}presentation")
    assert len(presentation) == 2
    page = presentation[0]
    for p in list(presentation)[1:]:
        presentation.remove(p)
    chosen = [n for n in page.iter() if n.get(f"{{{DRAW}}}name") == "X"]
    assert len(chosen) == 1
    chosen = chosen[0]
    for n in list(page):
        if n is not chosen.getparent():
            page.remove(n)
    for n in list(chosen.getparent()):
        if n is not chosen:
            chosen.getparent().remove(n)
    page.set(f"{{{DRAW}}}nav-order", "x")
    parts["content.xml"] = ET.tostring(root, xml_declaration=True, encoding="UTF-8")
    with zipfile.ZipFile(target, "w") as z:
        z.writestr("mimetype", parts.pop("mimetype"), compress_type=zipfile.ZIP_STORED)
        for name, value in parts.items():
            z.writestr(name, value, compress_type=zipfile.ZIP_DEFLATED)


def oracle(path, empty):
    root = ET.fromstring(read(path)["content.xml"])
    tables = list(root.iter(f"{{{TABLE}}}table"))
    assert len(tables) == (1 if empty else 5)
    outer = tables[0]
    rows = outer.findall(f"{{{TABLE}}}table-row")
    assert len(rows) == 2
    ids, marks = set(), set()
    for n in root.iter():
        identity = n.get(f"{{{XML}}}id")
        if identity:
            assert identity not in ids
            ids.add(identity)
        if n.tag in (
            f"{{{TABLE}}}table-row",
            f"{{{TABLE}}}table-cell",
            f"{{{TABLE}}}covered-table-cell",
        ):
            assert n.get(f"{{{TABLE}}}number-columns-repeated") is None
            assert n.get(f"{{{TABLE}}}number-rows-repeated") is None
    for row in rows:
        cells = row.findall(f"{{{TABLE}}}table-cell")
        assert len(cells) == 2
        for cell in cells:
            if empty:
                assert len(cell) == 0
                continue
            paragraph = cell.find(f"{{{TEXT}}}p")
            assert "".join(paragraph.itertext()) == "Repeated Ω link"
            start = paragraph.find(f"{{{TEXT}}}bookmark-start").get(f"{{{TEXT}}}name")
            end = paragraph.find(f"{{{TEXT}}}bookmark-end").get(f"{{{TEXT}}}name")
            href = paragraph.find(f"{{{TEXT}}}a").get(f"{{{XLINK}}}href")
            assert start == end and href == "#" + start and start not in marks
            marks.add(start)
            assert "".join(cell.find(f"{{{TABLE}}}table").itertext()) == "Nested"
    return {"independent_cells": 4, "unique_local_references": True, "empty": empty}


def equal(a, b):
    x, y = Image.open(a).convert("RGBA"), Image.open(b).convert("RGBA")
    assert x.size == y.size and x.tobytes() == y.tobytes(), (a, b)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--before-stage", type=Path)
    args = parser.parse_args()
    bins, art = args.bin_dir.resolve(), args.artifacts.resolve()
    art.mkdir(parents=True, exist_ok=True)
    docs = art / "documents"
    subprocess.run(
        [str(bins / "calligra-native-stage-repeat-test"), "--documents", str(docs)],
        check=True,
    )
    for stem in ("repeated", "empty-table"):
        selected_source(
            docs / (stem + "-source.odp"), docs / (stem + "-source-selected.odp")
        )
    checks = {
        "repeated": oracle(docs / "expanded-selected.odp", False),
        "empty": oracle(docs / "empty-table-selected.odp", True),
    }
    names = [
        "repeated-source-selected",
        "expanded-selected",
        "empty-table-source-selected",
        "empty-table-selected",
    ]
    env = {
        **os.environ,
        "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap",
        "LC_ALL": "C",
    }
    with PrivateCups(art / "cups") as cups:
        print_producer.verify(bins, art / "suite", cups=cups, bitmap_fonts=True)
        env["CUPS_SERVER"] = cups.server
        for name in names:
            pdf = art / (name + ".pdf")
            saved = art / (name + "-saved.odp")
            run = subprocess.run(
                [
                    str(bins / "calligra-stage-fltk"),
                    str(docs / (name + ".odp")),
                    "--export-odf",
                    str(saved),
                    "--print-pdf",
                    str(pdf),
                    "--print-to",
                    cups.name,
                ],
                env=env,
                capture_output=True,
                timeout=90,
            )
            (art / (name + ".log")).write_bytes(run.stdout + run.stderr)
            assert run.returncode == 0, run.stderr
            assert all(
                int(n) == 0 for n in re.findall(rb"(\d+) missing glyphs", run.stderr)
            )
            assert len(PdfReader(pdf).pages) == 1
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
            for p in (pdf, reopened):
                subprocess.run(
                    ["pdftoppm", "-r", "100", "-png", str(p), str(p.with_suffix(""))],
                    check=True,
                    capture_output=True,
                )
            equal(art / (name + "-1.png"), art / (name + "-reopened-1.png"))
        equal(art / "repeated-source-selected-1.png", art / "expanded-selected-1.png")
        equal(
            art / "empty-table-source-selected-1.png",
            art / "empty-table-selected-1.png",
        )
        jobs = cups.completed(7)
        expected = [
            art / "suite" / f"{a}.pdf" for a in ("words", "sheets", "stage")
        ] + [art / (n + ".pdf") for n in names]
        digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
        assert len(jobs) == 7 and sum(len(PdfReader(p).pages) for p in jobs) == 13
        assert sorted(map(digest, jobs)) == sorted(map(digest, expected))
    blue = lambda p: sum(
        1
        for r, g, b in Image.open(p).convert("RGB").get_flattened_data()
        if abs(r - 32) < 4 and abs(g - 64) < 4 and abs(b - 160) < 4
    )
    assert blue(art / "empty-table-selected-1.png") > 100
    regression = None
    if args.before_stage:
        before = art / "empty-table-before-fix.pdf"
        subprocess.run(
            [
                str(args.before_stage.resolve()),
                str(docs / "empty-table-source-selected.odp"),
                "--print-pdf",
                str(before),
            ],
            env=env,
            check=True,
            capture_output=True,
            timeout=90,
        )
        subprocess.run(
            ["pdftoppm", "-r", "100", "-png", str(before), str(before.with_suffix(""))],
            check=True,
            capture_output=True,
        )
        old, new = (
            blue(art / "empty-table-before-fix-1.png"),
            blue(art / "empty-table-selected-1.png"),
        )
        assert old == 0 and new > 100
        regression = {"before_blue_border_pixels": old, "after_blue_border_pixels": new}
    (art / "results.json").write_text(
        json.dumps(
            {
                "xml_oracle": checks,
                "cups_jobs": 7,
                "pages": 13,
                "source_expanded_rasters_exact": True,
                "all_reopened_pages_exact": True,
                "cups_pdf_bytes_exact": True,
                "empty_table_regression": regression,
                "system_queues_changed": False,
            },
            indent=2,
        )
        + "\n"
    )
    print(
        "PASS independent repeated/empty table sources, exact expansion and reopen rasters, seven CUPS jobs/13 pages"
    )


if __name__ == "__main__":
    main()
