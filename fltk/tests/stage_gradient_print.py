#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Real CUPS gradients with an independent SVG consumer and source/reopen checks."""

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

import cairosvg
from cups_runtime import PrivateCups
from PIL import Image, ImageChops, ImageFilter, ImageStat
from pypdf import PdfReader
from stage_gradient_producer import produce, reference_svg, verify_saved

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("directory", type=Path)
parser.add_argument("--bin-dir", type=Path, required=True)
parser.add_argument("--ui-artifacts", type=Path)
args = parser.parse_args()
art = args.directory.resolve()
art.mkdir(parents=True, exist_ok=True)
source = (
    args.ui_artifacts.resolve() / "original.odp"
    if args.ui_artifacts
    else produce(art / "producer")
)
original_hash = hashlib.sha256(source.read_bytes()).hexdigest()
references = []
for page in range(4):
    svg = reference_svg(page)
    (art / f"reference-{page + 1}.svg").write_bytes(svg)
    png = art / f"reference-{page + 1}.png"
    cairosvg.svg2png(bytestring=svg, write_to=str(png), scale=2)
    references.append(png)
results = []
with PrivateCups(art / "cups") as cups:
    env = {**cups.env, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"}
    for key in ["DISPLAY", "WAYLAND_DISPLAY"]:
        env.pop(key, None)
    binary = str(args.bin_dir.resolve() / "calligra-stage-fltk")
    cases = [("original", source, False), ("reopened", art / "original.odp", False)]
    if args.ui_artifacts:
        cases.append(
            ("authored", args.ui_artifacts.resolve() / "native-authored.odp", True)
        )
    for job, (name, input_path, authored) in enumerate(cases, 1):
        saved, pdf = art / (name + ".odp"), art / (name + ".pdf")
        run = subprocess.run(
            [
                binary,
                str(input_path),
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
        assert "unsupported features" not in run.stderr, run.stderr
        assert cups.completed(job)[-1].read_bytes() == pdf.read_bytes()
        assert len(PdfReader(pdf).pages) == 4
        verify_saved(source, saved, authored)
        subprocess.run(
            ["pdftoppm", "-r", "144", "-png", str(pdf), str(art / name)],
            check=True,
            capture_output=True,
        )
        comparisons = []
        for page, reference in enumerate(references, 1):
            actual = Image.open(art / f"{name}-{page}.png").convert("RGB")
            if authored and page == 1:
                png = art / "reference-authored.png"
                cairosvg.svg2png(
                    bytestring=reference_svg(0, True), write_to=str(png), scale=2
                )
                reference = png
            expected = Image.open(reference).convert("RGB")
            assert actual.size == expected.size == (1200, 840)
            difference = ImageChops.difference(actual, expected)
            difference.save(art / f"{name}-difference-{page}.png")
            channels = difference.split()
            maximum = ImageChops.lighter(
                ImageChops.lighter(channels[0], channels[1]), channels[2]
            )
            # Exclude a two-pixel neighborhood of independently detected hard
            # edges (object silhouettes and discrete step boundaries). Smooth
            # gradient interiors still compare across the entire page.
            edge = Image.new("L", expected.size)
            for dx, dy in [(1, 0), (-1, 0), (0, 1), (0, -1)]:
                d = ImageChops.difference(
                    expected, ImageChops.offset(expected, dx, dy)
                ).split()
                d = ImageChops.lighter(ImageChops.lighter(d[0], d[1]), d[2])
                edge = ImageChops.lighter(edge, d)
            edge = edge.point(lambda v: 255 if v > 8 else 0).filter(
                ImageFilter.MaxFilter(5)
            )
            interior = ImageChops.invert(edge)
            bad = ImageChops.multiply(
                maximum.point(lambda v: 255 if v > 6 else 0), interior
            )
            bad_pixels = sum(bad.histogram()[1:])
            means = ImageStat.Stat(difference).mean
            assert max(means) < 1.25, (name, page, means)
            assert bad_pixels == 0, (name, page, bad_pixels)
            if job == 2:
                assert (art / f"original-{page}.png").read_bytes() == (
                    art / f"reopened-{page}.png"
                ).read_bytes()
            if authored and page != 1:
                assert (art / f"original-{page}.png").read_bytes() == (
                    art / f"authored-{page}.png"
                ).read_bytes()
            comparisons.append(
                {
                    "page": page,
                    "mean_difference": means,
                    "interior_pixels_over_six": bad_pixels,
                    "edge_exclusion_pixels": sum(edge.histogram()[1:]),
                }
            )
        results.append(
            {
                "case": name,
                "pages": 4,
                "cups_pdf_exact": True,
                "source_parts_preserved": True,
                "independent_svg_consumer": comparisons,
            }
        )
assert hashlib.sha256(source.read_bytes()).hexdigest() == original_hash
(art / "results.json").write_text(
    json.dumps(
        {
            "cups_jobs": len(results),
            "pages": 4 * len(results),
            "exact_reopen_rasters": True,
            "cases": results,
        },
        indent=2,
    )
    + "\n"
)
print(
    f"PASS {len(results)} gradient CUPS jobs/{4 * len(results)} pages, independent SVG interiors and exact source/archive/reopen"
)
