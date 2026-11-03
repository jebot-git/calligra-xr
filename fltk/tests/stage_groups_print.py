#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Stage grouping/ungrouping, independent overlap pixels and CUPS jobs."""

import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
from PIL import Image
from pypdf import PdfReader
from cups_runtime import PrivateCups
from stage_groups_producer import verify_saved, verify_nested
from stage_groups_raster import verify_red_bounds

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("directory", type=Path)
p.add_argument("--bin-dir", type=Path, required=True)
p.add_argument("--fixtures", type=Path, required=True)
p.add_argument("--ui-artifacts", type=Path)
a = p.parse_args()
art = a.directory.resolve()
art.mkdir(parents=True, exist_ok=True)
fixtures = a.fixtures.resolve()
ui = a.ui_artifacts.resolve() if a.ui_artifacts else None

results = []
with PrivateCups(art / "cups") as cups:
    env = {**cups.env, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"}
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    original = fixtures / "grouping.odp"
    cases = [
        ("original", original, original, None),
        ("model-grouped", fixtures / "model-grouped.odp", original, True),
        ("model-ungrouped", fixtures / "model-ungrouped.odp", original, False),
    ]
    if ui:
        cases += [
            ("native-grouped", ui / "native-grouped.odp", ui / "original.odp", True),
            (
                "native-ungrouped",
                ui / "native-ungrouped.odp",
                ui / "original.odp",
                False,
            ),
        ]
    if ui:
        cases += [
            (name, ui / (name + ".odp"), ui / "original.odp", True)
            for name in [
                "native-moved",
                "native-resized",
                "native-rotated",
                "native-mirrored",
            ]
        ]
    if ui:
        cases.append(
            (
                "native-nested",
                ui / "native-nested.odp",
                ui / "native-grouped.odp",
                "nested",
            )
        )
    for count, (name, source, original_source, grouped) in enumerate(cases, 1):
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
        metadata = dict(
            action=[1, 0, 0, 1, 0, 0], tolerance=1e-7, linear_tolerance=1e-8
        )
        action_file = ui / (name + "-action.json") if ui else None
        transformed = action_file is not None and action_file.exists()
        if transformed:
            metadata.update(json.loads(action_file.read_text()))
        if grouped == "nested":
            verify_nested(original_source, saved)
        elif grouped is not None:
            verify_saved(
                original_source,
                saved,
                grouped,
                metadata["action"] if transformed else None,
                metadata["tolerance"],
                metadata["linear_tolerance"],
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
        # Authored interior points remain independent of application reports.
        # Mirroring puts B outside the page; sample a visible point in A instead.
        color = (
            (224, 160, 32)
            if name == "original"
            else (204, 48, 48)
            if name == "native-mirrored"
            else (32, 160, 96)
        )
        x, y = (50, 100) if name == "native-mirrored" else (150, 190)
        aa, bb, cc, dd, ee, ff = metadata["action"]
        px, py = (
            round((aa * x + cc * y + ee) * 100 / 72),
            round((bb * x + dd * y + ff) * 100 / 72),
        )
        raster = Image.open(art / f"{name}-1.png").convert("RGB")
        for dx in range(-2, 3):
            for dy in range(-2, 3):
                got = raster.getpixel((px + dx, py + dy))
                assert all(abs(a - b) <= 1 for a, b in zip(got, color)), (
                    name,
                    got,
                    color,
                )
        red_bounds = verify_red_bounds(
            art / f"{name}-1.png",
            metadata["action"],
            metadata["tolerance"],
            metadata["linear_tolerance"],
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
                independent_red_bounds=red_bounds,
            )
        )
assert (art / "original-1.png").read_bytes() != (
    art / "model-grouped-1.png"
).read_bytes()
assert (art / "model-grouped-1.png").read_bytes() == (
    art / "model-ungrouped-1.png"
).read_bytes()
(art / "results.json").write_text(json.dumps(results, indent=2) + "\n")
print(
    f"PASS {len(results)} actual Stage CUPS jobs, {len(results) * 2} pages, ODP grouping/ungrouping overlap pixels and exact reopen",
    flush=True,
)
