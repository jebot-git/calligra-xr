#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent layer visibility pixels, exact exports/reopens and actual CUPS jobs."""

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
from stage_layers_producer import verify_saved

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("directory", type=Path)
p.add_argument("--bin-dir", required=True, type=Path)
p.add_argument("--fixtures", required=True, type=Path)
p.add_argument("--ui-artifacts", type=Path)
a = p.parse_args()
art = a.directory.resolve()
art.mkdir(parents=True, exist_ok=True)
original = a.fixtures.resolve() / "layers.odp"
cases = [
    ("original", original, False, original),
    ("model-moved", a.fixtures.resolve() / "model-moved.odp", True, original),
]
if a.ui_artifacts:
    cases.append(
        (
            "native-moved",
            a.ui_artifacts.resolve() / "native-moved.odp",
            True,
            a.ui_artifacts.resolve() / "original.odp",
        )
    )
results = []
with PrivateCups(art / "cups") as cups:
    env = {**cups.env, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"}
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    for job, (name, source, moved, reference) in enumerate(cases, 1):
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        pdf = art / (name + ".pdf")
        saved = art / (name + ".odp")
        binary = str(a.bin_dir.resolve() / "calligra-stage-fltk")
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
        assert not re.search(r"\b[1-9]\d* missing glyphs", run.stderr), run.stderr
        assert not re.search(r"\b[1-9]\d* unsupported features", run.stderr), run.stderr
        assert cups.completed(job)[-1].read_bytes() == pdf.read_bytes()
        assert len(PdfReader(pdf).pages) == 3
        verify_saved(reference, saved, moved)
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
                check=True,
                capture_output=True,
            )
        for page in range(1, 4):
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
                pixel(
                    107 + (10 if moved else 0), 91 + (5 if moved else 0), (204, 48, 48)
                )
                pixel(207, 91, (255, 255, 255))  # Screen-only excluded.
                pixel(307, 91, (32, 160, 96))  # Print-only included.
                pixel(107, 161, (255, 255, 255))  # Hidden in both targets.
                pixel(207, 161, (224, 160, 32))  # Protected still prints.
                pixel(307, 231, (255, 255, 255))  # Hidden protected member.
                pixel(350, 10, (224, 160, 32))  # Global print master.
            elif page == 2:
                pixel(107, 101, (255, 255, 255))
                pixel(207, 101, (32, 160, 96))
                pixel(307, 101, (224, 160, 32))
                pixel(350, 10, (224, 160, 32))
            else:
                pixel(107, 91, (204, 48, 48))
                pixel(350, 10, (34, 85, 136))  # Master's own override prints blue.
            if moved and page > 1:
                assert first.read_bytes() == (art / f"original-{page}.png").read_bytes()
        results.append(
            dict(
                case=name,
                pages=3,
                cups_pdf_exact=True,
                reopen_pixels_exact=True,
                independent_visibility_pixels=True,
                source_and_protected_bytes_preserved=True,
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
    "CUPS layer jobs /",
    len(cases) * 3,
    "pages; independent screen/print-mode and scoped master pixels, exact source/reopen",
)
