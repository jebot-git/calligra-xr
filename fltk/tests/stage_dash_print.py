#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual CUPS dash corpus, independent alpha/unit/source and exact reopen."""

import argparse
import hashlib
import json
import re
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

from cups_runtime import PrivateCups
from PIL import Image
from pypdf import PdfReader
from stage_dash_producer import INK, entries, normalized, verify_saved

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("directory", type=Path)
p.add_argument("--bin-dir", type=Path, required=True)
p.add_argument("--source", type=Path, required=True)
p.add_argument("--ui-artifacts", type=Path)
a = p.parse_args()
art = a.directory.resolve()
art.mkdir(parents=True, exist_ok=True)
original = a.source.resolve()
cases = [("original", original, None)]
if a.ui_artifacts:
    cases += [
        ("native-" + case, a.ui_artifacts.resolve() / ("native-" + case + ".odp"), case)
        for case in ["dash"]
    ]
results = []
white = (255, 255, 255)
with PrivateCups(art / "cups") as cups:
    env = {**cups.env, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"}
    for key in ["DISPLAY", "WAYLAND_DISPLAY"]:
        env.pop(key, None)
    binary = str(a.bin_dir.resolve() / "calligra-stage-fltk")
    for job, (name, source, case) in enumerate(cases, 1):
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        pdf, saved = art / (name + ".pdf"), art / (name + ".odp")
        run = subprocess.run(
            [
                binary,
                str(source),
                "--export-odf",
                str(saved),
                "--print-pdf",
                str(pdf),
                "--print-to",
                cups.name,
            ],
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )
        (art / (name + ".log")).write_text(run.stdout + run.stderr)
        run.check_returncode()
        assert not re.search(
            r"\b[1-9]\d* (missing glyphs|unsupported features)", run.stderr
        ), run.stderr
        assert cups.completed(job)[-1].read_bytes() == pdf.read_bytes()
        assert len(PdfReader(pdf).pages) == 3
        if case:
            verify_saved(original, saved, case)
        else:
            before, after = entries(original), entries(saved)
            assert before.keys() == after.keys()
            for part in before:
                if part == "content.xml":
                    assert normalized(ET.fromstring(before[part])) == normalized(
                        ET.fromstring(after[part])
                    )
                else:
                    assert before[part] == after[part], part
        assert hashlib.sha256(source.read_bytes()).hexdigest() == digest
        reopened = art / (name + "-reopened.pdf")
        subprocess.run(
            [binary, str(saved), "--print-pdf", str(reopened)],
            env=env,
            check=True,
            capture_output=True,
        )
        for path, prefix in [(pdf, name), (reopened, name + "-reopened")]:
            subprocess.run(
                ["pdftoppm", "-r", "144", "-png", str(path), str(art / prefix)],
                check=True,
                capture_output=True,
            )
        for page in [1, 2, 3]:
            first, second = (
                art / f"{name}-{page}.png",
                art / f"{name}-reopened-{page}.png",
            )
            assert first.read_bytes() == second.read_bytes()
            image = Image.open(first).convert("RGB")

            def pixel(x, y, expected, image=image, name=name, page=page):
                got = image.getpixel((round(x * 2), round(y * 2)))
                assert all(abs(a - b) <= 2 for a, b in zip(got, expected)), (
                    name,
                    page,
                    x,
                    y,
                    got,
                    expected,
                )

            if page == 1:
                pixel(50, 60, white if case else INK)
                pixel(36, 60, INK)
                pixel(27, 60, INK if case else white)
                pixel(26, 55, white)
                pixel(50, 110, INK)
                pixel(31, 160, (60, 74, 116))
                pixel(37, 160, (88, 100, 136))
                pixel(50, 160, white)
                pixel(26, 160, INK)
                pixel(30, 210, INK)
                pixel(45, 210, white)
                pixel(234, 60, INK)
                pixel(239, 60, white)
            if page == 3:
                pixel(249.5, 183, INK)
                pixel(231.6, 144.6, white)
                pixel(234.2, 146.7, INK)
                pixel(55, 75, INK)
                pixel(142.5, 135.5, INK)
                pixel(40, 60, INK)
            if case and page != 1:
                assert (
                    first.read_bytes() == (art / f"original-{page}.png").read_bytes()
                ), (case, "Unrelated page changed")
        results.append(
            {
                "case": name,
                "pages": 3,
                "cups_pdf_exact": True,
                "reopen_pixels_exact": True,
                "independent_dash_unit_overlay_zero_alpha_pixels": True,
                "scoped_dash_resource_style_archive_oracle": True,
            }
        )
(art / "results.json").write_text(
    json.dumps(
        {"cups_jobs": len(results), "pages": 3 * len(results), "cases": results},
        indent=2,
    )
    + "\n"
)
print(
    f"PASS {len(results)} dash CUPS jobs / {3 * len(results)} pages; independent silhouettes/alpha/styles/archive and exact reopen"
)
