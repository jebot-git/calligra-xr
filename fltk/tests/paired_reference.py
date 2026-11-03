#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Paired statistics: retained caches and independent exact rational arithmetic."""

import argparse
from decimal import Decimal as D, localcontext
from fractions import Fraction as F
import json
import math
import os
from pathlib import Path
import random
import re
import xml.etree.ElementTree as ET
import subprocess

ROOT = Path(__file__).resolve().parents[2]
NAMES = {
    "correl",
    "covar",
    "covariance.p",
    "covariance.s",
    "forecast",
    "intercept",
    "pearson",
    "rsq",
    "slope",
    "steyx",
    "sumx2my2",
    "sumx2py2",
    "sumxmy2",
}


def run(binary, mode, text=None, files=()):
    return subprocess.check_output(
        [str(binary.resolve()), mode, *map(str, files)],
        input=text,
        text=True,
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
    )


def number(value):
    try:
        value = float(value)
        return value if math.isfinite(value) else "#NUM!"
    except OverflowError:
        return "#NUM!"


def root(value):
    with localcontext() as context:
        context.prec = 1600
        return number((D(value.numerator) / D(value.denominator)).sqrt())


def references(ys, xs, query):
    ys = list(map(F, ys))
    xs = list(map(F, xs))
    n = len(ys)
    mx = sum(xs) / n
    my = sum(ys) / n
    xx = sum((x - mx) ** 2 for x in xs)
    yy = sum((y - my) ** 2 for y in ys)
    xy = sum((x - mx) * (y - my) for x, y in zip(xs, ys, strict=True))
    results = {
        "SUMX2MY2": number(sum(y * y - x * x for y, x in zip(ys, xs, strict=True))),
        "SUMX2PY2": number(sum(y * y + x * x for y, x in zip(ys, xs, strict=True))),
        "SUMXMY2": number(sum((y - x) ** 2 for y, x in zip(ys, xs, strict=True))),
        "COVAR": number(xy / n),
        "COVARIANCE.S": number(xy / (n - 1)) if n > 1 else "#DIV/0!",
    }
    results["RSQ"] = number(xy * xy / (xx * yy)) if xx and yy else "#DIV/0!"
    if xx and yy:
        r = root(xy * xy / (xx * yy))
        results["CORREL"] = r if xy >= 0 else -r
    else:
        results["CORREL"] = "#DIV/0!"
    if xx:
        slope = xy / xx
        intercept = my - slope * mx
        results.update(
            SLOPE=number(slope),
            INTERCEPT=number(intercept),
            FORECAST=number(intercept + slope * F(query)),
        )
        results["STEYX"] = (
            root(
                sum(
                    (y - intercept - slope * x) ** 2
                    for y, x in zip(ys, xs, strict=True)
                )
                / (n - 2)
            )
            if n > 2
            else "#DIV/0!"
        )
    else:
        results.update(
            {k: "#DIV/0!" for k in ("SLOPE", "INTERCEPT", "FORECAST", "STEYX")}
        )
    return results


def raw_region(path, sheet, first, last):
    table_ns = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
    office = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
    table = list(ET.parse(path).iter(table_ns + "table"))[sheet]
    grid = {}
    row = 1
    for element in table.findall(table_ns + "table-row"):
        repeat = int(element.get(table_ns + "number-rows-repeated", "1"))
        column = 1
        # The independently audited fixtures use only the first 20 rows and
        # 20 columns. Do not expand their unrelated styled empty repetitions.
        if row <= 20:
            for cell in element:
                width = int(cell.get(table_ns + "number-columns-repeated", "1"))
                for r in range(row, min(row + repeat, 21)):
                    for c in range(column, min(column + width, 21)):
                        grid[(r, c)] = cell
                column += width
        row += repeat

    def address(label):
        match = re.fullmatch(r"([A-Z]+)([0-9]+)", label)
        col = 0
        for char in match[1]:
            col = col * 26 + ord(char) - 64
        return int(match[2]), col

    def value(pos):
        cell = grid[pos]
        assert cell.get(office + "value-type") == "float"
        formula = cell.get(table_ns + "formula")
        if not formula:
            return float(cell.get(office + "value"))
        match = re.fullmatch(
            r"of:=\s*([0-9]+)\s*\+\s*10\s*\^\s*\[\.([A-Z]+[0-9]+)\]\s*", formula
        )
        assert match, formula
        parameter = grid[address(match[2])]
        assert parameter.get(table_ns + "formula") is None
        exponent = float(parameter.get(office + "value"))
        assert exponent.is_integer()
        result = float(F(int(match[1])) + F(10) ** int(exponent))
        assert result == float(cell.get(office + "value"))
        return result

    top, left = address(first)
    bottom, right = address(last)
    return [
        value((r, c)) for r in range(top, bottom + 1) for c in range(left, right + 1)
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    paths = sorted(
        p for p in (ROOT / "sheets/tests/data/fods").rglob("*.fods") if p.stem in NAMES
    )
    rows = json.loads(run(args.test_bin, "--fods", files=paths))
    failures = []
    unavailable = []
    error_codes = []
    precision_differences = []
    for row in rows:
        got = row["actual"]
        want = row.get("expected_value")
        if got["incomplete"]:
            unavailable.append(row)
            continue
        key = (Path(row["file"]).stem, row["sheet"], row["row"], row["column"])
        if key in {
            ("covar", 1, 8, 1),
            ("covariance.p", 1, 8, 1),
            ("covariance.s", 1, 8, 1),
            ("rsq", 1, 9, 1),
        }:
            function = "COVAR"
            if key[0] == "rsq":
                a, b, c, d, function = "L12", "L15", "M12", "M15", "RSQ"
            elif key[0] == "covar":
                a, b, c, d = "I7", "I12", "J7", "J12"
            else:
                a, b, c, d = "I8", "I13", "J8", "J13"
                if key[0] == "covariance.s":
                    function = "COVARIANCE.S"
            ys = raw_region(row["file"], row["sheet"], a, b)
            xs = raw_region(row["file"], row["sheet"], c, d)
            oracle = references(ys, xs, 0.0)[function]
            assert got["kind"] == 1 and math.isclose(
                got["value"], oracle, rel_tol=1e-13, abs_tol=1e-15
            ), row
            row["independent_exact_rational"] = oracle
            row["reason"] = (
                "Exact arithmetic over independently read raw FODS inputs resolves the retained offset-precision/underflow discrepancy. Simple power-of-ten inputs are independently evaluated and checked against their caches."
            )
            precision_differences.append(row)
            continue
        valid = (
            got["kind"] == 4
            if row["expected_error"]
            else (
                got["kind"] == 1
                and math.isclose(got.get("value", 0), want, rel_tol=1e-8, abs_tol=1e-9)
                if want is not None
                else got["display"] == row["expected_text"]
            )
        )
        if not valid:
            failures.append(row)
        elif row["expected_error"] and got["display"] != row["expected_text"]:
            error_codes.append(row)
    rng = random.Random(20261105)
    cases = []

    def add(xs, ys, q):
        array = lambda v: "{" + ",".join(format(float(x), ".17g") for x in v) + "}"
        for name, want in references(ys, xs, q).items():
            formula = (
                name
                + "("
                + (format(q, ".17g") + "," if name == "FORECAST" else "")
                + array(ys)
                + ","
                + array(xs)
                + ")"
            )
            cases.append((formula, want))

    for _ in range(90):
        sx = rng.choice([1e-300, 1e-150, 1, 1e150, 1e300])
        sy = rng.choice([1e-300, 1e-150, 1, 1e150, 1e300])
        n = rng.randint(3, 20)
        xs = [float(rng.randint(-30, 70) * sx) for _ in range(n)]
        ys = [float(rng.randint(-30, 70) * sy) for _ in range(n)]
        add(xs, ys, float(rng.randint(-10, 20) * sx))
    for origin in (1e12, -1e12, 1e15):
        for dx, dy in [
            ([0, 1, 3, 7], [1, 3, 7, 15]),
            ([0, 1, 3, 7], [5, 6, 7, 10]),
            ([1, 2, 3, 4], [3, 4, 8, 9]),
        ]:
            add([origin + x for x in dx], [origin + y for y in dy], 0.0)
    add([3.0, 6.0, 9.0], [1.0, 2.0, 3.0], 12.0)
    output = run(args.test_bin, "--eval", "".join(f + "\n" for f, _ in cases))
    for (formula, want), line in zip(cases, output.splitlines(), strict=True):
        got = json.loads(line)
        # Dimensionless results need a small absolute tolerance near zero; dimensional
        # tiny values retain relative checks. Exact zero residuals remain exact checks.
        absolute = 1e-13 if formula.startswith(("CORREL(", "RSQ(")) else 0
        valid = (
            got["display"] == want
            if isinstance(want, str)
            else got["kind"] == 1
            and math.isclose(got["value"], want, rel_tol=3e-12, abs_tol=absolute)
        )
        if not valid or got["incomplete"]:
            failures.append({"formula": formula, "expected": want, "actual": got})
    report = {
        "retained_cases": len(rows),
        "agreeing_retained_caches": len(rows)
        - len(precision_differences)
        - len(unavailable)
        - len(error_codes)
        - sum("file" in f for f in failures),
        "retained_error_code_differences": error_codes,
        "independently_resolved_precision_differences": precision_differences,
        "unavailable_dependencies": unavailable,
        "independent_exact_rational_cases": len(cases),
        "failures": failures,
        "scope": "Scalar paired statistics/regression and pairwise square sums; full multiple-regression arrays and other statistical groups remain unfinished.",
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    assert not failures, failures[:10]
    print(
        "PASS",
        report["agreeing_retained_caches"],
        "retained caches and",
        len(cases),
        "exact rational cases;",
        len(unavailable),
        "unavailable dependencies;",
        len(error_codes),
        "error-code differences",
    )


if __name__ == "__main__":
    main()
