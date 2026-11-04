#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual CUPS connector authoring, source/style/ID isolation and exact reopen."""

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

from cups_runtime import PrivateCups
from PIL import Image
from pypdf import PdfReader
from stage_connector_authoring_producer import COLOR, verify_saved
from stage_connector_producer import probes as base_probes

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
    cases.extend(
        ("native-" + case, a.ui_artifacts.resolve() / ("native-" + case + ".odp"), case)
        for case in ["attached", "free", "skew", "label"]
    )
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
        assert len(PdfReader(pdf).pages) == 4
        verify_saved(original, saved, case)
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
        for page in [1, 2, 3, 4]:
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
                pixel(185, 190, COLOR if case == "attached" else white)
                pixel(100, 90, COLOR if case in {"free", "skew", "label"} else white)
                pixel(215, 95, COLOR if case == "skew" else white)
                if case != "label":
                    pixel(215, 90, COLOR if case == "free" else white)
                if case == "label":
                    reference = Image.open(art / "native-free-1.png").convert("RGB")
                    # Label ink must appear around the connector's center,
                    # outside the unchanged one-point shaft.
                    changed = [
                        (x, y)
                        for x in range(340, 521)
                        for y in range(120, 241)
                        if image.getpixel((x, y)) != reference.getpixel((x, y))
                    ]
                    assert len(changed) > 12, (
                        "The authored label has no printed glyph ink"
                    )
                    left, right = min(x for x, y in changed), max(x for x, y in changed)
                    top, bottom = min(y for x, y in changed), max(y for x, y in changed)
                    assert abs((left + right) / 4 - 215) < 6
                    assert abs((top + bottom) / 4 - 90) < 8
            else:
                for x, y, expected in base_probes(page - 1):
                    pixel(x, y, expected)
            if case and page != 1:
                assert first.read_bytes() == (art / f"original-{page}.png").read_bytes()
        results.append(
            {
                "case": name,
                "pages": 4,
                "cups_pdf_exact": True,
                "reopen_pixels_exact": True,
                "independent_authored_curve_free_skew_and_centered_label_pixels": True,
                "created_ids_styles_references_and_archive_oracle": True,
            }
        )
(art / "results.json").write_text(
    json.dumps(
        {"cups_jobs": len(results), "pages": 4 * len(results), "cases": results},
        indent=2,
    )
    + "\n"
)
print(
    f"PASS {len(results)} connector CUPS jobs / {4 * len(results)} pages; independent routes/attachments/alpha/styles/archive and exact reopen"
)
