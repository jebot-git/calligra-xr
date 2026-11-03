#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Print the initial and edited criteria workbooks through a private CUPS queue."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from cups_runtime import PrivateCups
from PIL import Image
from pypdf import PdfReader
import importlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--test-bin-dir", type=Path, required=True)
    parser.add_argument("--edited", type=Path, required=True)
    parser.add_argument(
        "--suite",
        choices=(
            "criteria",
            "order",
            "moments",
            "paired",
            "distribution",
            "inference",
            "regression",
            "log_regression",
        ),
        default="criteria",
    )
    args = parser.parse_args()
    producer = importlib.import_module(args.suite + "_producer")
    produce, verify = producer.produce, producer.verify
    art = args.artifacts.resolve()
    art.mkdir(parents=True, exist_ok=True)
    initial = produce(art / "fixtures")[0]
    results = []
    with PrivateCups(art / "cups") as cups:
        env = {**cups.env, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"}
        for name in ("DISPLAY", "WAYLAND_DISPLAY"):
            env.pop(name, None)
        for count, (name, source, money) in enumerate(
            [("original", initial, 2), ("edited", args.edited.resolve(), 8)], 1
        ):
            before = hashlib.sha256(source.read_bytes()).hexdigest()
            pdf, saved = art / (name + ".pdf"), art / (name + ".ods")
            binary = str(args.bin_dir.resolve() / "calligra-sheets-fltk")
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
                text=True,
                capture_output=True,
            )
            (art / (name + ".log")).write_text(run.stdout + run.stderr)
            run.check_returncode()
            assert "missing glyphs" not in run.stderr
            # Default A4 paper fits four of the native 100pt columns. Twenty-one
            # worksheet columns require six horizontal print tiles.
            assert len(PdfReader(pdf).pages) == 6
            capture = cups.completed(count)[-1]
            assert pdf.read_bytes() == capture.read_bytes()
            reopened = art / (name + "-reopened.pdf")
            subprocess.run(
                [binary, str(saved), "--print-pdf", str(reopened)],
                env=env,
                check=True,
                capture_output=True,
            )
            for path, prefix in ((pdf, name), (reopened, name + "-reopened")):
                subprocess.run(
                    ["pdftoppm", "-r", "100", "-png", str(path), str(art / prefix)],
                    check=True,
                    capture_output=True,
                )
            for page in range(1, 7):
                first, second = (
                    art / f"{name}-{page}.png",
                    art / f"{name}-reopened-{page}.png",
                )
                assert first.read_bytes() == second.read_bytes(), (name, page)
                image = Image.open(first).convert("RGB")
                assert sum(max(pixel) < 100 for pixel in image.getdata()) > 100
            if hasattr(producer, "verify_caches"):
                producer.verify_caches(saved, money)
            verify(saved, args.test_bin_dir, art / ("verified-" + name), money)
            assert hashlib.sha256(source.read_bytes()).hexdigest() == before
            results.append(
                {
                    "name": name,
                    "pages": 6,
                    "cups_pdf_exact": True,
                    "reopened_pixels_exact": True,
                    "calculation_caches_verified": True,
                    "source_unchanged": True,
                }
            )
    (art / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(
        "PASS initial/edited Sheets calculations, ODS export and two actual CUPS jobs; twelve print pages match after reopen"
    )


if __name__ == "__main__":
    main()
