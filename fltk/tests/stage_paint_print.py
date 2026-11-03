#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual CUPS paint-authoring corpus with independent style/source and opacity pixels."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from PIL import Image
from pypdf import PdfReader
from cups_runtime import PrivateCups
from stage_paint_producer import verify_saved

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("directory", type=Path)
p.add_argument("--bin-dir", required=True, type=Path)
p.add_argument("--fixtures", required=True, type=Path)
p.add_argument("--ui-artifacts", type=Path)
a = p.parse_args()
art = a.directory.resolve()
art.mkdir(parents=True, exist_ok=True)
fixtures = a.fixtures.resolve()
original = fixtures / "paint.odp"
cases = [("original", original, "original", original, False)]
for case in [
    "paint",
    "group",
    "none",
    "presentation",
    "image",
    "rule",
    "image-fill",
    "image-background",
    "image-zero",
    "image-inherited",
]:
    cases.append(
        ("model-" + case, fixtures / ("model-" + case + ".odp"), case, original, False)
    )
if a.ui_artifacts:
    ui = a.ui_artifacts.resolve()
    for case in [
        "paint",
        "group",
        "none",
        "image",
        "rule",
        "image-fill",
        "image-zero",
        "image-inherited",
    ]:
        cases.append(
            (
                "native-" + case,
                ui / ("native-" + case + ".odp"),
                case,
                ui / "original.odp",
                True,
            )
        )
results = []
with PrivateCups(art / "cups") as cups:
    env = {**cups.env, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"}
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    binary = str(a.bin_dir.resolve() / "calligra-stage-fltk")
    for job, (name, source, case, reference, native) in enumerate(cases, 1):
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        pdf = art / (name + ".pdf")
        saved = art / (name + ".odp")
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
        verify_saved(reference, saved, case, native=native)
        assert hashlib.sha256(source.read_bytes()).hexdigest() == digest
        reopened = art / (name + "-reopened.pdf")
        subprocess.run(
            [binary, str(saved), "--print-pdf", str(reopened)],
            env=env,
            capture_output=True,
            check=True,
        )
        for path, prefix in [(pdf, name), (reopened, name + "-reopened")]:
            subprocess.run(
                ["pdftoppm", "-r", "100", "-png", str(path), str(art / prefix)],
                capture_output=True,
                check=True,
            )
        for page in [1, 2, 3]:
            first, second = (
                art / f"{name}-{page}.png",
                art / f"{name}-reopened-{page}.png",
            )
            assert first.read_bytes() == second.read_bytes()
            image = Image.open(first).convert("RGB")

            def pixel(x, y, color):
                px, py = round(x * 100 / 72), round(y * 100 / 72)
                for dx in range(-1, 2):
                    for dy in range(-1, 2):
                        got = image.getpixel((px + dx, py + dy))
                        assert all(abs(a - b) <= 1 for a, b in zip(got, color)), (
                            name,
                            page,
                            x,
                            y,
                            got,
                            color,
                        )

            white = (255, 255, 255)
            blend = (153, 179, 204)
            if page == 1:
                pixel(
                    50,
                    87,
                    blend
                    if case == "paint"
                    else white
                    if case == "none"
                    else (204, 48, 48),
                )
                pixel(250, 157, blend if case == "group" else (34, 85, 136))
                if case == "paint":
                    pixel(38, 87, (179, 64, 140))
                if case == "group":
                    pixel(238, 157, (179, 64, 140))
                pixel(207, 91, white)
                pixel(307, 91, (32, 160, 96))
                pixel(107, 161, white)
                pixel(207, 161, (224, 160, 32))
                pixel(
                    250,
                    215,
                    (201, 211, 231)
                    if case == "image"
                    else (191, 163, 183)
                    if case == "image-background"
                    else white
                    if case == "image-zero"
                    else (147, 167, 207),
                )
                pixel(175, 210, white if case == "rule" else (204, 48, 48))
                pixel(147, 197, (204, 48, 48))
                pixel(350, 10, (224, 160, 32))
            elif page == 2:
                pixel(
                    50,
                    170,
                    (201, 211, 231) if case == "image-inherited" else (174, 189, 219),
                )
                pixel(150, 170, (174, 189, 219))
                pixel(107, 101, white)
                pixel(207, 101, (32, 160, 96))
                pixel(307, 101, (224, 160, 32))
                pixel(350, 10, (224, 160, 32))
            else:
                pixel(50, 87, blend if case == "presentation" else (68, 136, 34))
                if case == "presentation":
                    pixel(38, 87, (179, 64, 140))
                pixel(350, 10, (34, 85, 136))
            unchanged = (
                (page == 2 and case != "image-inherited")
                or (case in ["presentation", "image-inherited"] and page == 1)
                or (case != "presentation" and page == 3)
            )
            if case != "original" and unchanged:
                assert first.read_bytes() == (art / f"original-{page}.png").read_bytes()
        results.append(
            dict(
                case=name,
                pages=3,
                cups_pdf_exact=True,
                reopen_pixels_exact=True,
                independent_fill_stroke_opacity_pixels=True,
                isolated_style_reference_and_archive_oracle=True,
            )
        )
(art / "results.json").write_text(
    json.dumps(
        dict(cups_jobs=len(cases), pages=len(cases) * 3, cases=results), indent=2
    )
    + "\n"
)
print(
    "PASS",
    len(cases),
    "paint authoring CUPS jobs /",
    len(cases) * 3,
    "pages; independent opacity pixels/style/source and exact reopen",
)
