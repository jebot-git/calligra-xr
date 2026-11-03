#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact Fraction regression weights with independent 160/1200-digit logarithms."""

import argparse
from fractions import Fraction as F
import json
import math
import os
from pathlib import Path
import random
import subprocess
import xml.etree.ElementTree as ET
import mpmath as mp
from regression_reference import invert, literal, transpose

mp.mp.dps = 160


def real(x):
    x = F(x)
    return mp.mpf(x.numerator) / x.denominator


def reference(y, x, constant, query, perfect=False):
    n = len(y)
    p = len(x[0])
    design = [[F(1)] * constant + list(map(F, row)) for row in x]
    d = p + constant
    gram = [
        [sum(row[i] * row[j] for row in design) for j in range(d)] for i in range(d)
    ]
    selected = []
    inv = []
    for j in range(d):
        subset = selected + [j]
        candidate = invert([[gram[i][k] for k in subset] for i in subset])
        if candidate is not None:
            selected = subset
            inv = candidate
    weights = [
        [
            sum(row[k] * design[o][selected[k]] for k in range(len(selected)))
            for o in range(n)
        ]
        for row in inv
    ]
    logs = [mp.log(real(v)) for v in y]
    coefs = [mp.mpf(0)] * d
    for j, row in zip(selected, weights, strict=True):
        coefs[j] = mp.fsum(real(w) * v for w, v in zip(row, logs, strict=True))
    fitted = [
        mp.fsum(real(v) * b for v, b in zip(row, coefs, strict=True)) for row in design
    ]
    mean = mp.fsum(logs) / n if constant else 0
    residual = (
        0
        if perfect
        else mp.fsum((a - b) ** 2 for a, b in zip(logs, fitted, strict=True))
    )
    total = mp.fsum((v - mean) ** 2 for v in logs)
    explained = mp.fsum((v - mean) ** 2 for v in fitted)
    df = n - len(selected)
    m = len(selected) - constant
    se = [mp.mpf(0)] * d
    for i, j in enumerate(selected):
        se[j] = mp.sqrt(residual / df * real(inv[i][i]))
    first = [mp.exp(v) for v in reversed(coefs[constant:])] + [
        mp.exp(coefs[0]) if constant else 1
    ]
    second = list(reversed(se[constant:])) + [se[0] if constant else 0]
    rows = [
        first,
        second,
        [explained / total if total else "#DIV/0!", mp.sqrt(residual / df)],
        [explained * df / (residual * m) if residual and m else "#DIV/0!", df],
        [explained, residual],
    ]
    rows = [row + ["#N/A"] * (p + 1 - len(row)) for row in rows]
    forecast = [
        mp.exp(
            mp.fsum(
                real(v) * b
                for v, b in zip(
                    ([F(1)] * constant + list(map(F, row))), coefs, strict=True
                )
            )
        )
        for row in query
    ]
    return rows, forecast


def independent(binary):
    rng = random.Random(20261103)
    cases = []

    def add(y, x, q, constant=True, orientation="column", perfect=False, omitted=False):
        stats, forecast = reference(y, x, constant, q, perfect)
        if orientation == "column":
            Y = [[v] for v in y]
            X = x
            Q = q
            shape = (len(q), 1)
        else:
            Y = [y]
            X = transpose(x)
            Q = transpose(q)
            shape = (1, len(q))
        argx = "" if omitted else literal(X)
        cases.append(
            (
                f"LOGEST({literal(Y)},{argx},{int(constant)},TRUE)",
                5,
                len(x[0]) + 1,
                sum(stats, []),
            )
        )
        cases.append(
            (
                f"GROWTH({literal(Y)},{argx},{literal(Q)},{int(constant)})",
                *shape,
                forecast,
            )
        )

    for _ in range(100):
        n = rng.randrange(4, 11)
        p = rng.randrange(1, min(3, n - 2) + 1)
        constant = bool(rng.randrange(2))
        scales = [2.0 ** rng.randrange(-100, 101) for _ in range(p)]
        x = [[rng.randint(-8, 8) * s for s in scales] for _ in range(n)]
        if p > 1 and rng.randrange(3) == 0:
            for row in x:
                row[-1] = row[0] * 2
        y = [math.ldexp(rng.uniform(1, 2), rng.randrange(-700, 701)) for _ in range(n)]
        q = [[rng.randint(-5, 5) * s for s in scales] for _ in range(3)]
        add(y, x, q, constant, "row" if rng.randrange(2) else "column")
    for constant in (True, False):
        for n in (3, 5, 10):
            add(
                [3**i for i in range(1, n + 1)],
                [[i] for i in range(1, n + 1)],
                [[0], [n + 1], [n + 2]],
                constant,
                perfect=True,
                omitted=True,
            )
        # Offset predictors with one-ULP differences; independent exact weights.
        add(
            [2, 3, 5, 7, 11],
            [[1e15 + i] for i in range(5)],
            [[1e15 + 6], [1e15 - 1]],
            constant,
        )
        add(
            [5e-324, 1e-323, 2e-323],
            [[1], [2], [3]],
            [[4], [5]],
            constant,
            perfect=constant,
        )
        add(
            [10, 15, 30, 60, 90, 360],
            [[1, 0], [0, 1], [1, 1], [2, 1], [1, 2], [3, 2]],
            [[3, 2], [2, 3]],
            constant,
            perfect=constant,
        )
        add(
            [21, 63, 189, 567, 1701],
            [[1, 2], [2, 4], [3, 6], [4, 8], [5, 10]],
            [[6, 12], [7, 14]],
            constant,
            perfect=constant,
        )
    with mp.workdps(1200):
        add([1, 2, 4], [[0], [5e-324], [1e-323]], [[5e-324], [0]], True, perfect=True)
        add([1, 2, 4], [[0], [1e-308], [2e-308]], [[1e-308], [0]], True, perfect=True)
        # Both enormous coefficients are retained until they cancel within a query.
        add(
            [1, 2, 4, 8, 16],
            [
                [0, 0],
                [1e-308, 1e-308],
                [2e-308, 2e-308],
                [3e-308, math.nextafter(3e-308, math.inf)],
                [4e-308, 4e-308],
            ],
            [[1e-308, 1e-308], [0, 0]],
            True,
        )
    text = "\n".join(row[0] for row in cases) + "\n"
    out = subprocess.check_output(
        [str(binary.resolve()), "--eval"],
        input=text,
        text=True,
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
    )
    results = list(map(json.loads, out.splitlines()))
    assert len(results) == len(cases)
    cells = 0
    for (formula, nr, nc, expected), got in zip(cases, results, strict=True):
        assert not got["incomplete"] and (got.get("rows"), got.get("columns")) == (
            nr,
            nc,
        ), (formula, got)
        for want, item in zip(expected, got["items"], strict=True):
            cells += 1
            if isinstance(want, str):
                assert item["display"] == want, (formula, want, item)
            else:
                try:
                    value = float(want)
                except (OverflowError, ValueError):
                    value = math.inf
                if math.isinf(value):
                    assert item["display"] == "#NUM!", (formula, value, item)
                else:
                    assert "value" in item and math.isclose(
                        item["value"], value, rel_tol=3e-12, abs_tol=5e-324
                    ), (formula, value, item)
    return {
        "formulas": len(cases),
        "array_cells": cells,
        "ordinary_precision_digits": 160,
        "adversarial_precision_digits": 1200,
        "regression_weights": "independent exact Fraction Gaussian elimination",
    }


def retained(binary):
    root = Path(__file__).resolve().parents[2]
    paths = [
        root / "sheets/tests/data/fods/array/fods" / (name + ".fods")
        for name in ("logest", "growth")
    ]
    originals = [p.read_bytes() for p in paths]
    records = json.loads(
        subprocess.check_output(
            [str(binary.resolve()), "--regression-fods", *map(str, paths)],
            text=True,
            env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
        )
    )
    agreeing = errors = unavailable = 0
    for r in records:
        a = r["actual"]
        assert not a["incomplete"], r
        if (Path(r["file"]).stem, r["sheet"], r["row"], r["column"]) == (
            "logest",
            1,
            19,
            24,
        ):
            # FORMULA(A2) is now real text metadata. The numeric predictor
            # conversion reports the expected known error; no cached text is
            # substituted for the formula and no unavailable flag remains.
            assert a["kind"] == 4 and a["display"] == "#VALUE!", r
        if r["expected_error"]:
            assert a["kind"] == 4, r
            errors += 1
        else:
            assert "value" in a and math.isclose(
                a["value"], r["expected_value"], rel_tol=1e-10, abs_tol=1e-12
            ), r
            agreeing += 1
    assert (len(records), agreeing, errors, unavailable) == (68, 58, 10, 0)
    assert [p.read_bytes() for p in paths] == originals
    return {
        "cases": len(records),
        "agreeing_numeric": agreeing,
        "error_labels": errors,
        "unavailable_FORMULA_dependency": unavailable,
        "source_unchanged": True,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    a = parser.parse_args()
    result = {"independent": independent(a.test_bin), "retained": retained(a.test_bin)}
    a.output.write_text(json.dumps(result, indent=2) + "\n")
    print("PASS", result, flush=True)
