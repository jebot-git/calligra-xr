#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent Fraction Gauss-Jordan reference for matrix linear regression."""

import argparse
from fractions import Fraction as F
from decimal import Decimal, localcontext
import json
import math
import os
from pathlib import Path
import random
import subprocess


def invert(a):
    n = len(a)
    a = [list(map(F, row)) + [F(i == j) for j in range(n)] for i, row in enumerate(a)]
    for j in range(n):
        pivot = next((i for i in range(j, n) if a[i][j]), None)
        if pivot is None:
            return None
        a[j], a[pivot] = a[pivot], a[j]
        q = a[j][j]
        a[j] = [v / q for v in a[j]]
        for i in range(n):
            if i != j:
                q = a[i][j]
                a[i] = [x - q * y for x, y in zip(a[i], a[j], strict=True)]
    return [r[n:] for r in a]


def square_root(x):
    with localcontext() as c:
        c.prec = 110
        return float((Decimal(x.numerator) / Decimal(x.denominator)).sqrt())


def reference(ys, xs, intercept, queries):
    y = list(map(F, ys))
    n = len(y)
    p = len(xs[0])
    x = [[F(1)] * intercept + list(map(F, r)) for r in xs]
    d = p + intercept
    g = [[sum(row[i] * row[j] for row in x) for j in range(d)] for i in range(d)]
    selected = []
    inverse = []
    # Greedy independent columns retain the constant first. Gaussian elimination
    # is independent of the native exact symmetric LDL factorization.
    for j in range(d):
        subset = selected + [j]
        candidate = invert([[g[i][k] for k in subset] for i in subset])
        if candidate is not None:
            selected = subset
            inverse = candidate
    rhs = [sum(row[j] * v for row, v in zip(x, y, strict=True)) for j in selected]
    b = [sum(v * z for v, z in zip(row, rhs, strict=True)) for row in inverse]
    full = [F(0)] * d
    for j, v in zip(selected, b, strict=True):
        full[j] = v
    predicted = [sum(a * b for a, b in zip(row, full, strict=True)) for row in x]
    sse = sum((a - b) ** 2 for a, b in zip(y, predicted, strict=True))
    mean = sum(y) / n if intercept else F(0)
    total = sum((v - mean) ** 2 for v in y)
    ssr = total - sse
    df = n - len(selected)
    m = len(selected) - intercept
    ordered = list(reversed(full[intercept:])) + [full[0] if intercept else F(0)]
    stderr = [F(0)] * d
    for i, j in enumerate(selected):
        stderr[j] = square_root(sse / df * inverse[i][i])
    se = list(reversed(stderr[intercept:])) + [stderr[0] if intercept else F(0)]
    rows = [
        ordered,
        se,
        [ssr / total if total else "#DIV/0!", square_root(sse / df)],
        [ssr * df / (sse * m) if sse and m else "#DIV/0!", df],
        [ssr, sse],
    ]
    rows = [r + ["#N/A"] * (p + 1 - len(r)) for r in rows]
    forecast = [
        sum(
            a * b
            for a, b in zip(([F(1)] * intercept + list(map(F, row))), full, strict=True)
        )
        for row in queries
    ]
    return rows, forecast


def literal(matrix):
    return (
        "{"
        + ";".join(",".join(format(float(v), ".17g") for v in row) for row in matrix)
        + "}"
    )


def transpose(rows):
    return list(map(list, zip(*rows, strict=True)))


def run(binary):
    cases = []
    rng = random.Random(20261103)

    def add(y, x, queries, constant=True, orientation="column", omitted=False):
        stats, forecast = reference(y, x, constant, queries)
        if orientation == "column":
            Y = [[v] for v in y]
            X = x
            Q = queries
            shape = (len(queries), 1)
        else:
            Y = [y]
            X = transpose(x)
            Q = transpose(queries)
            shape = (1, len(queries))
        argx = "" if omitted else literal(X)
        cases.append(
            (
                f"LINEST({literal(Y)},{argx},{int(constant)},TRUE)",
                5,
                len(x[0]) + 1,
                sum(stats, []),
            )
        )
        cases.append(
            (
                f"TREND({literal(Y)},{argx},{literal(Q)},{int(constant)})",
                *shape,
                forecast,
            )
        )

    for _ in range(200):
        n = rng.randrange(4, 18)
        p = rng.randrange(1, min(5, n - 2) + 1)
        constant = bool(rng.randrange(2))
        scale = [2.0 ** rng.randrange(-200, 201) for _ in range(p)]
        ys = 2.0 ** rng.randrange(-300, 301)
        x = [[rng.randint(-20, 20) * s for s in scale] for _ in range(n)]
        if p > 1 and rng.randrange(3) == 0:
            for row in x:
                row[-1] = row[0] * 2
        if rng.randrange(8) == 0:
            for row in x:
                row[0] = 3 * scale[0]
        y = [rng.randint(-20, 20) * ys for _ in range(n)]
        q = [[rng.randint(-100, 100) * s for s in scale] for _ in range(5)]
        add(y, x, q, constant, "row" if rng.randrange(2) else "column")
    for constant in (False, True):
        for n in (3, 5, 20):
            y = [rng.randrange(-20, 21) for _ in range(n)]
            x = [[i] for i in range(1, n + 1)]
            add(y, x, [[0], [n + 1], [n + 3]], constant, omitted=True)
        add([0, 1, 2], [[0], [5e-324], [1e-323]], [[5e-324], [1e-323], [0]], constant)
        add(
            [-1e308, 0, 1e308],
            [[-1e308], [0], [1e308]],
            [[-1e308], [1e308], [0]],
            constant,
        )
        add(
            [0, 5e-324, 1e-323],
            [[0], [1e-323], [2e-323]],
            [[1e308], [-1e308]],
            constant,
        )
        # A tiny offset in two almost dependent predictors, exact binary rank.
        add(
            [1, 2, 4, 7, 11],
            [[1, 1], [2, 2], [3, 3], [4, math.nextafter(4, 5)], [5, 5]],
            [[6, 6], [7, math.nextafter(7, 8)]],
            constant,
        )
        # Large offsets would erase the variance in floating normal equations.
        add(
            [1, 3, 6, 10, 15],
            [[1e15 + i] for i in range(5)],
            [[1e15 + 8], [1e15 - 1]],
            constant,
        )
    # Equal matrix shapes use a single predictor and column-major observations.
    ys = [[5, 9], [7, 11]]
    xs = [[1, 3], [2, 4]]
    q = [[5, 6], [7, 8]]
    stats, predict = reference(
        [5, 7, 9, 11], [[1], [2], [3], [4]], True, [[5], [6], [7], [8]]
    )
    cases.append(
        (f"LINEST({literal(ys)},{literal(xs)},TRUE,TRUE)", 5, 2, sum(stats, []))
    )
    cases.append((f"TREND({literal(ys)},{literal(xs)},{literal(q)})", 2, 2, predict))
    output = subprocess.check_output(
        [str(binary.resolve()), "--eval"],
        input="\n".join(row[0] for row in cases) + "\n",
        text=True,
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
    )
    results = list(map(json.loads, output.splitlines()))
    assert len(results) == len(cases)
    checked = 0
    for (formula, rows, cols, expected), got in zip(cases, results, strict=True):
        assert not got["incomplete"] and (got.get("rows"), got.get("columns")) == (
            rows,
            cols,
        ), (formula, got)
        assert len(expected) == len(got["items"])
        for want, item in zip(expected, got["items"], strict=True):
            checked += 1
            if isinstance(want, str):
                assert item["display"] == want, (formula, want, item)
            else:
                try:
                    v = float(want)
                except OverflowError:
                    v = math.inf
                if math.isinf(v):
                    assert item["display"] == "#NUM!", (formula, v, item)
                else:
                    assert "value" in item, (formula, v, item)
                    assert math.isclose(
                        item["value"], v, rel_tol=3e-13, abs_tol=5e-324
                    ), (formula, v, item)
    return {
        "formulas": len(cases),
        "array_cells": checked,
        "sample_arithmetic": "exact Fraction Gaussian elimination over actual binary64 inputs",
        "sqrt_precision_digits": 110,
    }


def retained(binary):
    path = (
        Path(__file__).resolve().parents[2]
        / "sheets/tests/data/fods/array/fods/trend.fods"
    )
    before = path.read_bytes()
    records = json.loads(
        subprocess.check_output(
            [str(binary.resolve()), "--regression-fods", str(path)],
            text=True,
            env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
        )
    )
    assert len(records) == 7
    for r in records:
        actual = r["actual"]
        assert not actual["incomplete"] and "value" in actual, r
        assert math.isclose(
            actual["value"], r["expected_value"], rel_tol=1e-11, abs_tol=1e-12
        ), r
    assert path.read_bytes() == before
    return {
        "cases": len(records),
        "agreeing": len(records),
        "unavailable": 0,
        "source_unchanged": True,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = {"independent": run(args.test_bin), "retained": retained(args.test_bin)}
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print("PASS", result, flush=True)
