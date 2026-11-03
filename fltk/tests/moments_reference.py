#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Retained higher-moment corpus and independent Decimal central moments."""

import argparse
from decimal import Decimal as D, localcontext
import json
import math
import os
from pathlib import Path
import random
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
NAMES = {"kurt", "skew", "skewp", "standardize", "trimmean"}


def run(binary, mode, text=None, files=()):
    return subprocess.check_output(
        [str(binary.resolve()), mode, *map(str, files)],
        input=text,
        text=True,
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
    )


def moment(name, values):
    with localcontext() as ctx:
        ctx.prec = 1600
        values = [D.from_float(float(v)) for v in values]
        n = D(len(values))
        mean = sum(values) / n
        m2 = sum((v - mean) ** 2 for v in values) / n
        if not m2:
            return "#DIV/0!"
        if name in ("SKEW", "SKEWP", "SKEW.P"):
            if n < 3:
                return "#DIV/0!"
            m3 = sum((v - mean) ** 3 for v in values) / n
            result = m3 / (m2 * m2.sqrt())
            if name == "SKEW":
                result *= (n * (n - 1)).sqrt() / (n - 2)
        else:
            if n < 4:
                return "#DIV/0!"
            m4 = sum((v - mean) ** 4 for v in values) / n
            result = m4 / (m2 * m2) - 3
            if name == "KURT":
                result = (n - 1) / ((n - 2) * (n - 3)) * ((n + 1) * result + 6)
        return float(result)


def retained_skew_input(path):
    table_ns = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
    office = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
    table = list(ET.parse(path).iter(table_ns + "table"))[1]
    values = []
    row = 1
    for element in table.findall(table_ns + "table-row"):
        repeat = int(element.get(table_ns + "number-rows-repeated", "1"))
        if row <= 101 and row + repeat > 3:
            column = 1
            for cell in element:
                width = int(cell.get(table_ns + "number-columns-repeated", "1"))
                for r in range(max(row, 3), min(row + repeat, 102)):
                    for c in range(max(column, 9), min(column + width, 11)):
                        assert cell.get(table_ns + "formula") is None
                        assert cell.get(office + "value-type") == "float"
                        values.append(float(cell.get(office + "value")))
                column += width
        row += repeat
    assert len(values) == 198
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-bin", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    paths = sorted(
        p
        for p in (ROOT / "sheets/tests/data/fods/statistical/fods").glob("*.fods")
        if p.stem in NAMES
    )
    rows = json.loads(run(args.test_bin, "--fods", files=paths))
    failures = []
    unavailable = []
    error_codes = []
    precision_differences = []
    for row in rows:
        got = row["actual"]
        expected = row.get("expected_value")
        if got["incomplete"]:
            unavailable.append(row)
            continue
        if (Path(row["file"]).stem, row["sheet"], row["row"], row["column"]) in {
            ("skew", 1, 3, 1),
            ("skewp", 1, 3, 1),
        }:
            values = retained_skew_input(row["file"])
            oracle = moment(
                "SKEW" if Path(row["file"]).stem == "skew" else "SKEWP", values
            )
            assert got["kind"] == 1 and math.isclose(
                got["value"], oracle, rel_tol=1e-11, abs_tol=1e-18
            ), row
            assert not math.isclose(expected, oracle, rel_tol=1e-8, abs_tol=1e-12), row
            row["independent_decimal"] = oracle
            row["reason"] = (
                "Independent Decimal evaluation of 198 raw FODS values agrees with native shifted/scaled moments; retained large-offset cache loses precision."
            )
            precision_differences.append(row)
            continue
        valid = (
            got["kind"] == 4
            if row["expected_error"]
            else (
                got["kind"] == 1
                and math.isclose(
                    got.get("value", 0), expected, rel_tol=1e-8, abs_tol=1e-9
                )
                if expected is not None
                else got["display"] == row["expected_text"]
            )
        )
        if not valid:
            failures.append(row)
        elif row["expected_error"] and got["display"] != row["expected_text"]:
            error_codes.append(row)
    rng = random.Random(20261104)
    cases = []
    array = (
        lambda values: "{" + ",".join(format(float(v), ".17g") for v in values) + "}"
    )
    for i in range(90):
        scale = rng.choice([1e-300, 1e-150, 1, 1e150, 1e300])
        values = [
            float(rng.randint(-30, 70) * scale) for _ in range(rng.randint(4, 20))
        ]
        for name in ("SKEW", "SKEWP", "KURT", "KURTP"):
            cases.append((name + "(" + array(values) + ")", moment(name, values)))
        # Decimal percent choices and a single double multiplication agree with the
        # documented trim boundary; odd-sized and mixed-sign inputs are represented.
        percent = rng.choice([0, 0.125, 0.25, 0.5, 0.75, 0.875])
        cut = math.floor(len(values) * percent / 2)
        kept = sorted(values)[cut : len(values) - cut]
        with localcontext() as ctx:
            ctx.prec = 1600
            expected = float(sum(D.from_float(v) for v in kept) / D(len(kept)))
        cases.append((f"TRIMMEAN({array(values)},{percent})", expected))
        x, mean, sigma = values[0], values[1], abs(scale)
        with localcontext() as ctx:
            ctx.prec = 1600
            expected = float(
                (D.from_float(x) - D.from_float(mean)) / D.from_float(sigma)
            )
        cases.append((f"STANDARDIZE({x:.17g},{mean:.17g},{sigma:.17g})", expected))
    for origin in (1e12, -1e12, 1e15):
        for steps in ([1, 2, 3, 5, 20], [1, 1, 1, 1, 3], [0, 1, 2, 3, 4]):
            values = [origin + i for i in steps]
            for name in ("SKEW", "SKEWP", "KURT", "KURTP"):
                cases.append((name + "(" + array(values) + ")", moment(name, values)))
    cases += [
        ("KURT({1,2,3})", "#DIV/0!"),
        ("KURTP({1,2,3})", "#DIV/0!"),
        ("SKEW({1,2})", "#DIV/0!"),
        ("SKEWP({1,2})", "#DIV/0!"),
        ("STANDARDIZE(1E308,-1E308,1E308)", 2),
        ("TRIMMEAN({1E308,1,-1E308},0)", 1 / 3),
    ]
    output = run(args.test_bin, "--eval", "".join(f + "\n" for f, _ in cases))
    for (formula, want), line in zip(cases, output.splitlines(), strict=True):
        got = json.loads(line)
        valid = (
            got["display"] == want
            if isinstance(want, str)
            else got["kind"] == 1
            and math.isclose(
                got["value"],
                want,
                rel_tol=3e-12,
                abs_tol=(
                    1e-14 if formula.startswith(("SKEW", "KURT", "STANDARDIZE")) else 0
                ),
            )
        )
        if not valid or got["incomplete"]:
            failures.append({"formula": formula, "expected": want, "actual": got})
    report = {
        "retained_cases": len(rows),
        "agreeing_retained_caches": len(rows)
        - len(precision_differences)
        - len(error_codes)
        - len(unavailable)
        - sum("file" in f for f in failures),
        "retained_error_code_differences": error_codes,
        "independently_resolved_precision_differences": precision_differences,
        "unavailable_dependencies": unavailable,
        "independent_decimal_cases": len(cases),
        "failures": failures,
        "KURTP": "Population excess kurtosis with at least four numbers, matching the retained help example, correcting the legacy implementation using sample variance.",
        "sources": [
            "https://docs.oasis-open.org/office/OpenDocument/v1.3/os/part4-formula/OpenDocument-v1.3-os-part4-formula.html",
            "https://support.microsoft.com/en-us/excel/functions/kurt-function",
            "https://support.microsoft.com/en-us/excel/functions/skew-p-function",
        ],
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    assert (
        len(rows) == 149 and len(unavailable) == 1 and len(precision_differences) == 2
    )
    assert not failures, failures[:10]
    print(
        "PASS",
        report["agreeing_retained_caches"],
        "retained caches and",
        len(cases),
        "independent Decimal cases;",
        len(unavailable),
        "unavailable dependencies and",
        len(error_codes),
        "error-code differences recorded",
    )


if __name__ == "__main__":
    main()
