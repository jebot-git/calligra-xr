#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Stage sibling/group stacking, independent overlap pixels and CUPS jobs."""

import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
from PIL import Image
from pypdf import PdfReader
from cups_runtime import PrivateCups
from stage_order_producer import verify_saved, INITIAL_GROUP, MODEL_ROOT, MODEL_GROUP

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("directory", type=Path)
p.add_argument("--bin-dir", type=Path, required=True)
p.add_argument("--ui-artifacts", type=Path, required=True)
a = p.parse_args()
art = a.directory.resolve()
art.mkdir(parents=True, exist_ok=True)
ui = a.ui_artifacts.resolve()

results = []
with PrivateCups(art / "cups") as cups:
    env = {**cups.env, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"}
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    cases = [
        ("original", ui / "original.odp"),
        ("edited", ui / "native-saved.odp"),
        ("front", ui / "front-saved.odp"),
        ("model", ui / "producer" / "model-saved.odp"),
    ]
    for count, (name, source) in enumerate(cases, 1):
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        pdf = art / (name + ".pdf")
        saved = art / (name + ".odp")
        binary = str(a.bin_dir.resolve() / "calligra-stage-fltk")
        command = [
            binary,
            str(source),
            "--export-odf",
            str(saved),
            "--print-pdf",
            str(pdf),
            "--print-to",
            cups.name,
        ]
        run = subprocess.run(command, env=env, capture_output=True, text=True)
        (art / (name + ".log")).write_text(run.stdout + run.stderr)
        run.check_returncode()
        # The retained connector is deliberately opaque to the native renderer.
        # A diagnostic for that one known gap must still report zero missing glyphs.
        assert not re.search(r"\b[1-9]\d* missing glyphs", run.stderr)
        assert "0 missing glyphs, 1 unsupported features" in run.stderr
        assert len(PdfReader(pdf).pages) == 2
        assert cups.completed(count)[-1].read_bytes() == pdf.read_bytes()
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
        for page in (1, 2):
            first, second = (
                art / f"{name}-{page}.png",
                art / f"{name}-reopened-{page}.png",
            )
            assert first.read_bytes() == second.read_bytes()
            assert (
                sum(max(c) < 100 for c in Image.open(first).convert("RGB").getdata())
                > 100
            )
        if name in ["edited", "model"]:
            verify_saved(ui / "original.odp", saved, MODEL_ROOT, MODEL_GROUP)
        elif name == "front":
            verify_saved(
                ui / "original.odp",
                saved,
                ["C", "Group", "Spare", "Opaque", "A", "Last", "B"],
                INITIAL_GROUP,
            )
        if name != "original":
            assert (art / "original-2.png").read_bytes() == (
                art / f"{name}-2.png"
            ).read_bytes()
        media = PdfReader(pdf).pages[0].mediabox
        assert (
            abs(float(media.width) - 400) < 1e-7
            and abs(float(media.height) - 240) < 1e-7
        )
        # The authored overlap at150,130pt lies well inside A, B, X and Y,
        # away from text/strokes. Its visible color must follow source stacking.
        color = {
            "original": (204, 48, 48),
            "front": (32, 160, 96),
            "edited": (224, 160, 32),
            "model": (224, 160, 32),
        }[name]
        raster = Image.open(art / f"{name}-1.png").convert("RGB")
        px, py = round(150 * 100 / 72), round(130 * 100 / 72)
        for dx in range(-2, 3):
            for dy in range(-2, 3):
                got = raster.getpixel((px + dx, py + dy))
                assert all(abs(a - b) <= 1 for a, b in zip(got, color)), (
                    name,
                    got,
                    color,
                )
        assert hashlib.sha256(source.read_bytes()).hexdigest() == before
        results.append(
            dict(
                name=name,
                pages=2,
                cups_pdf_exact=True,
                reopened_pixels_exact=True,
                source_unchanged=True,
                independent_overlap_color=color,
            )
        )
assert (art / "original-1.png").read_bytes() != (art / "edited-1.png").read_bytes()
assert (art / "original-2.png").read_bytes() == (art / "edited-2.png").read_bytes()
(art / "results.json").write_text(json.dumps(results, indent=2) + "\n")
print(
    "PASS four actual Stage CUPS jobs, eight pages, ODP exports, sibling/group stacking overlap pixels and exact reopen",
    flush=True,
)
