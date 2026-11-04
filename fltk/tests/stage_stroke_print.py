#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual CUPS stroke corpus, independent silhouette/alpha/source and exact reopen."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
from PIL import Image
from pypdf import PdfReader
from cups_runtime import PrivateCups
from stage_stroke_producer import entries, normalized, verify_saved, INK

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
        for case in ["cap", "join", "inherited", "group"]
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

            def pixel(x, y, expected):
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
                pixel(25, 60, white)
                pixel(150, 60, INK)
                pixel(146, 51, INK if case == "cap" else white)
                pixel(271, 51, INK)
                pixel(79, 121, white if case == "join" else INK)
                pixel(76, 124, INK)
                pixel(169, 121, white)
                pixel(166, 124, INK)
                pixel(256, 124, white)
                pixel(252, 124, INK)
                pixel(342, 124, white)
                pixel(339, 130, INK)
                pixel(120, 210, INK)
                pixel(111, 201, white)
                pixel(201, 201, INK)
                pixel(320, 210, white)
            if page == 2:
                pixel(316, 157, white)
                pixel(31, 61, INK if case == "inherited" else white)
                pixel(161, 61, white)
                pixel(35, 150, INK if case == "group" else white)
                pixel(206, 144, white if case == "group" else INK)
                pixel(195, 155, INK)
            if page == 3:
                pixel(249.5, 183, INK)
                pixel(231.6, 144.6, white)
                pixel(234.2, 146.7, INK)
                pixel(55, 75, INK)
                pixel(142.5, 135.5, INK)
                pixel(40, 60, INK)
            if case and page != (1 if case in ["cap", "join"] else 2):
                assert (
                    first.read_bytes() == (art / f"original-{page}.png").read_bytes()
                ), (case, "Unrelated page changed")
        results.append(
            dict(
                case=name,
                pages=3,
                cups_pdf_exact=True,
                reopen_pixels_exact=True,
                independent_stroke_cap_join_zero_alpha_pixels=True,
                shared_style_source_archive_oracle=True,
            )
        )
(art / "results.json").write_text(
    json.dumps(
        dict(cups_jobs=len(results), pages=3 * len(results), cases=results), indent=2
    )
    + "\n"
)
print(
    f"PASS {len(results)} stroke CUPS jobs / {3 * len(results)} pages; independent silhouettes/alpha/styles/archive and exact reopen"
)
