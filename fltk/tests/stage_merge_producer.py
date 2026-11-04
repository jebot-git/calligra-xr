#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent receiving package/definition retention and assembly CUPS checks.

These are internal graph assembly drafts with matched known defaults, not a
complete receiving-context isolator or an integrated Stage clipboard workflow.
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
from stage_context_producer import freeze, definitions

OFFICE = "urn:oasis:names:tc:opendocument:xmlns:office:1.0"
DRAW = "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0"


def read(path):
    with zipfile.ZipFile(path) as package:
        assert package.testzip() is None
        return {name: package.read(name) for name in package.namelist()}


def oracle(before, after):
    a, b = read(before), read(after)
    assert a.keys() <= b.keys()
    for name in a.keys() - {"content.xml", "styles.xml", "META-INF/manifest.xml"}:
        assert a[name] == b[name], name
    for name in ("content.xml", "styles.xml"):
        x, y = ET.fromstring(a[name]), ET.fromstring(b[name])
        old, new = definitions(x), definitions(y)
        assert all(new.get(k) == v for k, v in old.items())
        defaults = lambda n: [
            freeze(c) for g in n for c in g if c.tag == f"{{{STYLE}}}default-style"
        ]
        assert defaults(x) == defaults(y)
    x, y = ET.fromstring(a["content.xml"]), ET.fromstring(b["content.xml"])
    pages = lambda n: list(n.find(f"{{{OFFICE}}}body/{{{OFFICE}}}presentation"))
    old_pages, new_pages = pages(x), pages(y)
    assert len(old_pages) == len(new_pages) == 2
    assert freeze(old_pages[1]) == freeze(new_pages[1])
    assert old_pages[0].attrib == new_pages[0].attrib
    old_set = old_pages[0].find(f"{{{DRAW}}}layer-set")
    new_set = new_pages[0].find(f"{{{DRAW}}}layer-set")
    assert new_set is not None
    # The imported wrapper is appended after every old page node. Restoring the
    # old layer-set state must recover exactly the receiving body XML.
    added = [
        n
        for n in new_pages[0]
        if n.get(f"{{{DRAW}}}name", "").startswith("NativePaste")
    ]
    assert len(added) == 1 and added[0].tag == f"{{{DRAW}}}g"
    assert added[0].get(f"{{{DRAW}}}z-index") == "28"
    new_pages[0].remove(added[0])
    if old_set is None:
        if new_set.tail:
            previous = new_set.getprevious()
            if previous is None:
                new_pages[0].text = (new_pages[0].text or "") + new_set.tail
            else:
                previous.tail = (previous.tail or "") + new_set.tail
        new_pages[0].remove(new_set)
    else:
        assert len(old_set) == 0
        for n in list(new_set):
            new_set.remove(n)
    assert freeze(x.find(f"{{{OFFICE}}}body")) == freeze(y.find(f"{{{OFFICE}}}body"))
    return {
        "old_body_exact_after_removing_import": True,
        "old_definitions_defaults_assets_retained": True,
        "other_slide_exact": True,
    }


def equal(a, b):
    x, y = Image.open(a).convert("RGBA"), Image.open(b).convert("RGBA")
    assert x.size == y.size and x.tobytes() == y.tobytes(), (a, b)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    bins, art = args.bin_dir.resolve(), args.artifacts.resolve()
    art.mkdir(parents=True, exist_ok=True)
    docs = art / "documents"
    subprocess.run(
        [str(bins / "calligra-native-stage-merge-test"), "--documents", str(docs)],
        check=True,
    )
    cases = ("global", "empty-local", "master")
    checks = {
        case: oracle(docs / (case + "-before.odp"), docs / (case + "-merged.odp"))
        for case in cases
    }
    env = {
        **os.environ,
        "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap",
        "LC_ALL": "C",
    }
    with PrivateCups(art / "cups") as cups:
        print_producer.verify(bins, art / "suite", cups=cups, bitmap_fonts=True)
        env["CUPS_SERVER"] = cups.server
        for case in cases:
            for kind in ("before", "merged"):
                name = case + "-" + kind
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
                    int(n) == 0
                    for n in re.findall(rb"(\d+) missing glyphs", run.stderr)
                )
                assert len(PdfReader(pdf).pages) == 2
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
                for path in (pdf, reopened):
                    subprocess.run(
                        [
                            "pdftoppm",
                            "-r",
                            "100",
                            "-png",
                            str(path),
                            str(path.with_suffix("")),
                        ],
                        check=True,
                        capture_output=True,
                    )
                for page in (1, 2):
                    equal(
                        art / f"{name}-{page}.png", art / f"{name}-reopened-{page}.png"
                    )
            equal(art / f"{case}-before-2.png", art / f"{case}-merged-2.png")
        jobs = cups.completed(9)
        assert len(jobs) == 9 and sum(len(PdfReader(p).pages) for p in jobs) == 21
        expected = [
            art / "suite" / f"{app}.pdf" for app in ("words", "sheets", "stage")
        ]
        expected += [
            art / f"{case}-{kind}.pdf"
            for case in cases
            for kind in ("before", "merged")
        ]
        digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
        assert sorted(map(digest, jobs)) == sorted(map(digest, expected))
    (art / "results.json").write_text(
        json.dumps(
            dict(
                xml_oracle=checks,
                cups_jobs=9,
                pages=21,
                all_reopened_pages_exact=True,
                other_slide_rasters_exact=True,
                system_queues_changed=False,
                scope="Internal receiving graph assembly; default/context isolation, navigation and actual paste/history/UI pending",
            ),
            indent=2,
        )
        + "\n"
    )
    print(
        "PASS receiving source/definition/assets retention, other-slide and reopen rasters, nine CUPS jobs/21 pages"
    )


if __name__ == "__main__":
    main()
