#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual CUPS layer-authoring corpus with independent source and visibility pixels."""

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
from stage_layer_edit_producer import verify_saved

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("directory", type=Path)
p.add_argument("--bin-dir", required=True, type=Path)
p.add_argument("--fixtures", required=True, type=Path)
p.add_argument("--ui-artifacts", type=Path)
a = p.parse_args()
art = a.directory.resolve()
art.mkdir(parents=True, exist_ok=True)
fixtures = a.fixtures.resolve()
original = fixtures / "layer-edit.odp"
cases = [("original", original, "original", original)]
for case in ["print-only", "global-renamed"]:
    cases.append(
        ("model-" + case, fixtures / ("model-" + case + ".odp"), case, original)
    )
if a.ui_artifacts:
    ui = a.ui_artifacts.resolve()
    for case in [
        "print-only",
        "global-renamed",
        "screen-only",
        "unlocked",
        "moved",
        "master-local",
        "inherited",
        "group-assigned",
        "repaired",
    ]:
        cases.append(
            (
                "native-" + case,
                ui / ("native-" + case + ".odp"),
                case,
                ui / ("missing-source.odp" if case == "repaired" else "original.odp"),
            )
        )
results = []
with PrivateCups(art / "cups") as cups:
    env = {**cups.env, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"}
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    binary = str(a.bin_dir.resolve() / "calligra-stage-fltk")
    for job, (name, source, case, reference) in enumerate(cases, 1):
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
        verify_saved(reference, saved, case)
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

            if page == 1:
                visible = case in [
                    "original",
                    "print-only",
                    "global-renamed",
                    "inherited",
                    "repaired",
                ]
                pixel(107, 91, (204, 48, 48) if visible else (255, 255, 255))
                pixel(207, 91, (255, 255, 255))
                pixel(307, 91, (32, 160, 96))
                pixel(107, 161, (255, 255, 255))
                pixel(207, 161, (224, 160, 32))
                pixel(307, 231, (255, 255, 255))
                pixel(350, 10, (224, 160, 32))
                pixel(
                    307,
                    161,
                    (255, 255, 255) if case == "group-assigned" else (34, 85, 136),
                )
                pixel(144, 161, (255, 255, 255) if case == "moved" else (224, 160, 32))
                pixel(217, 161, (224, 160, 32) if case == "moved" else (255, 255, 255))
                if case in ["print-only", "global-renamed", "inherited", "repaired"]:
                    assert (
                        first.read_bytes() == (art / "original-1.png").read_bytes()
                    ), case
            elif page == 2:
                pixel(107, 101, (255, 255, 255))
                pixel(207, 101, (32, 160, 96))
                pixel(307, 101, (224, 160, 32))
                pixel(350, 10, (224, 160, 32))
            else:
                pixel(107, 91, (204, 48, 48))
                pixel(350, 10, (34, 85, 136))
            if case != "original" and page > 1:
                assert first.read_bytes() == (art / f"original-{page}.png").read_bytes()
        results.append(
            dict(
                case=name,
                pages=3,
                cups_pdf_exact=True,
                reopen_pixels_exact=True,
                independent_visibility_and_move_pixels=True,
                scope_metadata_object_and_archive_oracle=True,
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
    "layer authoring CUPS jobs /",
    len(cases) * 3,
    "pages; independent pixels/source/metadata/scopes and exact reopen",
)
