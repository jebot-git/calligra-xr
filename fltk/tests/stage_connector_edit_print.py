#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual CUPS connector target edits, portable cached paths and exact reopen."""

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
from stage_connector_edit_producer import INK, TARGET, D, entries, verify_saved
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
        for case in ["move", "resize", "group", "enhanced"]
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
                center = 190 if case == "move" else 195 if case == "resize" else 185
                pixel(center, 130, INK)
                pixel(35, 60, white if case == "move" else TARGET)
                pixel(75, 60, TARGET if case in {"move", "resize"} else white)
                pixel(185, 130, white if case in {"move", "resize"} else INK)
                pixel(250 if case == "group" else 240, 230, INK)
                start = 80
                if case == "enhanced":
                    content = ET.fromstring(entries(source)["content.xml"])
                    node = next(
                        n for n in content.iter() if n.get(D + "name") == "EditEnhanced"
                    )
                    start = (
                        30
                        + float(node.find(D + "enhanced-geometry").get(D + "modifiers"))
                        * 0.5
                    )
                pixel((start + 320) / 2, 180, INK)
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
                "independent_target_group_modifier_and_route_pixels": True,
                "portable_endpoint_path_source_styles_archive_oracle": True,
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
