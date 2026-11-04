#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent receiving XML/asset/default oracle and real paste CUPS specimens."""

import argparse
from collections import Counter
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


def read(path):
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None
        return {n: z.read(n) for n in z.namelist()}


def freeze(n):
    return (n.tag, sorted(n.attrib.items()), n.text, [(freeze(c), c.tail) for c in n])


def retained(before, after):
    a, b = read(before), read(after)
    assert a.keys() <= b.keys()
    for name in a.keys() - {"content.xml", "styles.xml", "META-INF/manifest.xml"}:
        assert a[name] == b[name], name
    for name in ("content.xml", "styles.xml"):
        x, y = ET.fromstring(a[name]), ET.fromstring(b[name])
        for group in ("styles", "automatic-styles", "font-face-decls"):
            old = x.find(f"{{{OFFICE}}}{group}")
            new = y.find(f"{{{OFFICE}}}{group}")
            if old is None:
                continue
            assert new is not None
            for node in old:
                if node.tag == f"{{{STYLE}}}default-style":
                    matches = [
                        c
                        for c in new
                        if c.tag == node.tag
                        and c.get(f"{{{STYLE}}}family")
                        == node.get(f"{{{STYLE}}}family")
                    ]
                else:
                    matches = [
                        c
                        for c in new
                        if c.tag == node.tag
                        and c.get(f"{{{STYLE}}}name") == node.get(f"{{{STYLE}}}name")
                        and c.get(f"{{{STYLE}}}family")
                        == node.get(f"{{{STYLE}}}family")
                    ]
                assert len(matches) == 1 and freeze(matches[0]) == freeze(node), (
                    name,
                    node.tag,
                    node.attrib,
                )
    x, y = ET.fromstring(a["content.xml"]), ET.fromstring(b["content.xml"])
    pages = [
        r.findall(f"{{{OFFICE}}}body/{{{OFFICE}}}presentation/{{{DRAW}}}page")
        for r in (x, y)
    ]
    assert len(pages[0]) == len(pages[1]) == 2
    for i, (old, new) in enumerate(zip(*pages)):
        before_attrs, after_attrs = dict(old.attrib), dict(new.attrib)
        navigation = f"{{{DRAW}}}nav-order"
        old_nav, new_nav = (
            before_attrs.pop(navigation, None),
            after_attrs.pop(navigation, None),
        )
        assert before_attrs == after_attrs
        if new_nav != old_nav:
            assert i == 0 and old_nav is not None and new_nav.startswith(old_nav + " ")
            added = new_nav[len(old_nav) :].split()
            assert added and len(set(added)) == len(added)
            assert not (set(added) & set(old_nav.split()))
            identities = [
                n.get("{http://www.w3.org/XML/1998/namespace}id") for n in new.iter()
            ]
            assert all(identities.count(name) == 1 for name in added)
        if i:
            assert freeze(old) == freeze(new)
        else:
            assert len(new) > len(old)
            assert [freeze(c) for c in old] == [
                freeze(c) for c in list(new)[: len(old)]
            ]
    return True


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--bin-dir", required=True, type=Path)
    p.add_argument("--artifacts", required=True, type=Path)
    a = p.parse_args()
    bins = a.bin_dir.resolve()
    art = a.artifacts.resolve()
    art.mkdir(parents=True, exist_ok=True)
    docs = art / "documents"
    subprocess.run(
        [str(bins / "calligra-native-stage-paste-test"), "--documents", str(docs)],
        check=True,
    )
    retained(docs / "destination-before.odp", docs / "materialized.odp")
    retained(docs / "destination-before.odp", docs / "transaction-pasted.odp")
    for name in (
        "source-selected",
        "materialized-selected",
        "transaction-source-selected",
        "transaction-pasted-selected",
    ):
        package = read(docs / (name + ".odp"))
        assert "meta.xml" not in package
        assert not any(b"UNSELECTED_" in data for data in package.values())
    names = (
        "source",
        "destination-before",
        "materialized",
        "transaction-pasted",
        "source-selected",
        "materialized-selected",
        "transaction-source-selected",
        "transaction-pasted-selected",
    )
    env = {
        **os.environ,
        "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap",
        "LC_ALL": "C",
    }
    outputs = []
    page_counts = {}
    with PrivateCups(art / "cups") as cups:
        print_producer.verify(bins, art / "suite", cups=cups, bitmap_fonts=True)
        outputs.extend(
            art / "suite" / (n + ".pdf") for n in ("words", "sheets", "stage")
        )
        env["CUPS_SERVER"] = cups.server
        for name in names:
            original = docs / (name + ".odp")
            saved = art / (name + "-saved.odp")
            pdf = art / (name + ".pdf")
            digest = hashlib.sha256(original.read_bytes()).hexdigest()
            command = [
                str(bins / "calligra-stage-fltk"),
                str(original),
                "--export-odf",
                str(saved),
                "--print-pdf",
                str(pdf),
                "--print-to",
                cups.name,
            ]
            r = subprocess.run(command, env=env, capture_output=True, timeout=90)
            (art / (name + "-print.log")).write_bytes(r.stdout + r.stderr)
            assert r.returncode == 0, r.stderr
            assert all(
                int(n) == 0 for n in re.findall(rb"(\d+) missing glyphs", r.stderr)
            ), r.stderr
            expected = 2 if name in names[:4] else 1
            page_counts[name] = len(PdfReader(pdf).pages)
            assert page_counts[name] == expected
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
            for item in (pdf, reopened):
                subprocess.run(
                    [
                        "pdftoppm",
                        "-r",
                        "100",
                        "-png",
                        str(item),
                        str(item.with_suffix("")),
                    ],
                    check=True,
                    capture_output=True,
                )
            for page in range(1, expected + 1):
                x, y = (
                    Image.open(art / (n + f"-{page}.png")).convert("RGBA")
                    for n in (name, name + "-reopened")
                )
                assert x.size == y.size and x.tobytes() == y.tobytes(), (name, page)
            outputs.append(pdf)
        for first, second in (
            ("source-selected", "materialized-selected"),
            ("transaction-source-selected", "transaction-pasted-selected"),
        ):
            x, y = (
                Image.open(art / (n + "-1.png")).convert("RGBA")
                for n in (first, second)
            )
            assert x.size == y.size and x.tobytes() == y.tobytes(), (first, second)
        captured = cups.completed(len(outputs))
        assert len(captured) == len(outputs)

        def hashes(paths):
            return Counter(hashlib.sha256(f.read_bytes()).hexdigest() for f in paths)

        assert hashes(captured) == hashes(outputs)
        pages = sum(len(PdfReader(f).pages) for f in captured)
    (art / "results.json").write_text(
        json.dumps(
            dict(
                cups_jobs=len(outputs),
                printed_pages=pages,
                page_counts=page_counts,
                cups_bytes_exact=True,
                receiving_defaults_definitions_body_and_parts_retained=True,
                old_navigation_retained_and_new_targets_appended=True,
                other_slide_unchanged=True,
                selected_outputs_exclude_unselected_data=True,
                source_materialized_rasters_exact=True,
                actual_transaction_selected_rasters_exact=True,
                all_reopened_pages_exact=True,
                system_queues_changed=False,
                scope="Selected native paste transactions and contexts; full ODF1.2 remains incomplete",
            ),
            indent=2,
        )
        + "\n"
    )
    print(
        "PASS independent receiving source/asset/default oracles, exact selected paste rasters, reopen and",
        len(outputs),
        "CUPS jobs/",
        pages,
        "pages",
    )


if __name__ == "__main__":
    main()
