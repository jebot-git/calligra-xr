#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Stage image original/edited ODP exports and actual private CUPS jobs."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from PIL import Image
from pypdf import PdfReader
from cups_runtime import PrivateCups
from stage_images_producer import verify_saved, UI, MODEL

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
    cases = [("original", ui / "original.odp"), ("edited", ui / "native-saved.odp")]
    cases += [
        ("model-" + ext, ui / "producer" / ("model-" + ext + ".odp"))
        for ext in ("png", "jpg", "gif", "bmp", "svg")
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
        assert "missing glyphs" not in run.stderr
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
        if name == "edited":
            verify_saved(ui / "original.odp", saved, ui / "input-image.png", UI)
        elif name.startswith("model-"):
            extension = name.removeprefix("model-")
            verify_saved(
                ui / "original.odp", saved, ui / ("input-image." + extension), MODEL
            )
        if name != "original":
            assert (art / "original-2.png").read_bytes() == (
                art / (name + "-2.png")
            ).read_bytes()
        assert hashlib.sha256(source.read_bytes()).hexdigest() == before
        results.append(
            dict(
                name=name,
                pages=2,
                cups_pdf_exact=True,
                reopened_pixels_exact=True,
                source_unchanged=True,
            )
        )
assert (art / "original-1.png").read_bytes() != (art / "edited-1.png").read_bytes()
assert (art / "original-2.png").read_bytes() == (art / "edited-2.png").read_bytes()
(art / "results.json").write_text(json.dumps(results, indent=2) + "\n")
print(
    "PASS seven actual Stage CUPS jobs, fourteen pages, five image formats, ODP exports and exact reopen",
    flush=True,
)
