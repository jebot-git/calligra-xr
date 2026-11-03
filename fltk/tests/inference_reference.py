#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact rational sample arithmetic and independent high-precision test tails."""

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

ROOT = Path(__file__).resolve().parents[2]
mp.mp.dps = 110


def run(binary, mode, text=None, files=()):
    return subprocess.check_output(
        [str(binary.resolve()), mode, *map(str, files)],
        input=text,
        text=True,
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
    )


def real(x):
    x = F(x)
    return mp.mpf(x.numerator) / x.denominator


def sample(xs):
    xs = list(map(F, xs))
    n = len(xs)
    mean = sum(xs) / n
    var = sum((x - mean) ** 2 for x in xs) / (n - 1) if n > 1 else None
    return n, mean, var


def student(t, df):
    return mp.betainc(df / 2, mp.mpf(".5"), 0, df / (df + t * t), regularized=True) / 2


def tests(xs, ys, tails=2):
    n, mx, vx = sample(xs)
    m, my, vy = sample(ys)
    out = {}
    if vx and vy:
        r = real(vx / vy)
        a = mp.mpf(n - 1) / 2
        b = mp.mpf(m - 1) / 2
        z = (n - 1) * r / ((n - 1) * r + m - 1)
        out["FTEST"] = float(
            2
            * min(
                mp.betainc(a, b, 0, z, regularized=True),
                mp.betainc(a, b, z, 1, regularized=True),
            )
        )
    if n == m:
        _, md, vd = sample([F(x) - F(y) for x, y in zip(xs, ys, strict=True)])
        if vd:
            t = abs(real(md)) * mp.sqrt(real(F(n) / vd))
            out["T1"] = float(tails * student(t, mp.mpf(n - 1)))
    pooled = ((n - 1) * vx + (m - 1) * vy) / (n + m - 2) * (F(1, n) + F(1, m))
    if pooled:
        t = abs(real(mx - my)) / mp.sqrt(real(pooled))
        out["T2"] = float(tails * student(t, mp.mpf(n + m - 2)))
    aa, bb = vx / n, vy / m
    if aa + bb:
        t = abs(real(mx - my)) / mp.sqrt(real(aa + bb))
        df = (aa + bb) ** 2 / (aa * aa / (n - 1) + bb * bb / (m - 1))
        out["T3"] = float(tails * student(t, real(df)))
    return out


def independent(binary):
    cases = []
    rng = random.Random(20261104)
    array = lambda a: "{" + ",".join(format(float(x), ".17g") for x in a) + "}"

    def add(f, want):
        cases.append((f, want))

    datasets = []
    for _ in range(100):
        nx = rng.randint(3, 12)
        ny = rng.choice([nx, rng.randint(3, 12)])
        offset = rng.choice([0, 2**45, -(2**45)])
        scale = rng.choice([1.0, 1e-200, 1e200])
        xs = [float(offset + rng.randint(-20, 20)) * scale for _ in range(nx)]
        ys = [float(offset + rng.randint(-25, 25)) * scale for _ in range(ny)]
        datasets.append((xs, ys))
    datasets += [
        ([1e308, -1e308, 0], [5e307, -1e308, 1e308]),
        ([1e-300, 2e-300, 3e-300], [2e-300, 4e-300, 7e-300]),
        (
            [1e308, math.nextafter(1e308, 0), math.nextafter(1e308, math.inf)],
            [1e308, math.nextafter(1e308, 0), 1e308],
        ),
        ([1e15 + i for i in range(8)], [1e15 + i for i in range(7)] + [1e15 + 6]),
    ]
    for xs, ys in datasets:
        for tails in [1, 2]:
            want = tests(xs, ys, tails)
            if tails == 2 and "FTEST" in want:
                add(f"F.TEST({array(xs)},{array(ys)})", want["FTEST"])
            for kind in [1, 2, 3]:
                if "T" + str(kind) in want:
                    add(
                        f"T.TEST({array(xs)},{array(ys)},{tails},{kind})",
                        want["T" + str(kind)],
                    )
        n, mean, var = sample(xs)
        # Hypothesized means are actual binary64 inputs too, not unrounded
        # rational means that a cell could never hold.
        for query in [float(mean), xs[0], 0.0]:
            if var:
                z = real(mean - F(query)) * mp.sqrt(n / real(var))
                add(
                    f"ZTEST({array(xs)},{query:.17g})",
                    float(mp.erfc(z / mp.sqrt(2)) / 2),
                )
            sigma = abs(xs[1] - xs[0])
            if sigma and math.isfinite(sigma):
                z = real(mean - F(query)) * mp.sqrt(n) / real(sigma)
                add(
                    f"Z.TEST({array(xs)},{query:.17g},{sigma:.17g})",
                    float(mp.erfc(z / mp.sqrt(2)) / 2),
                )
    for _ in range(100):
        data = [rng.randint(-10, 20) for _ in range(rng.randint(2, 30))]
        bins = [rng.randint(-10, 20) for _ in range(rng.randint(0, 8))]
        order = sorted(range(len(bins)), key=lambda i: bins[i])
        counts = [0] * (len(bins) + 1)
        for x in data:
            for i in order:
                if x <= bins[i]:
                    counts[i] += 1
                    break
            else:
                counts[-1] += 1
        # An all-text collection is the empty numeric-bin sequence.
        bb = array(bins) if bins else '{"empty"}'
        add(f"FREQUENCY({array(data)},{bb})", counts)
        weights = [rng.randint(1, 50) for _ in range(6)]
        total = sum(weights)
        probabilities = [float(F(w, total)) for w in weights]
        values = [rng.randint(-5, 5) for _ in weights]
        for low, high in [(-2, 2), (2, -2), (0, 0)]:
            want = float(
                sum(
                    F(p)
                    for x, p in zip(values, probabilities, strict=True)
                    if low <= x <= high
                )
            )
            add(f"PROB({array(values)},{array(probabilities)},{low},{high})", want)
        observed = [rng.randint(0, 100) for _ in range(6)]
        expected = [rng.randint(1, 100) for _ in range(6)]
        chi = sum((F(o) - e) ** 2 / e for o, e in zip(observed, expected, strict=True))
        want = float(
            mp.gammainc(mp.mpf(5) / 2, real(chi) / 2, mp.inf, regularized=True)
        )
        add(f"CHISQ.TEST({array(observed)},{array(expected)})", want)
    for name, xs, ys in [("TTEST", [1, 4], [2, 8]), ("T.TEST", [1, 4], [2, 3, 8])]:
        for kind in [1, 2, 3]:
            if kind == 1:
                want = tests([1, 4], [2, 8])["T1"]
            else:
                want = tests(xs, ys)["T" + str(kind)]
            add(f'{name}({{1,"skip",4}},{{2,3,8}},2,{kind})', want)
    # A tiny isolated mass following a unit mass must not be rounded to zero.
    for p in [1e-300, 1e-200, 1e-100, 1e-20]:
        add(f"PROB({{1,2}},{{1,{p:.17g}}},2)", p)
    output = [
        json.loads(line)
        for line in run(
            binary, "--eval", "".join(f + "\n" for f, _ in cases)
        ).splitlines()
    ]
    assert len(output) == len(cases)
    for (formula, want), got in zip(cases, output, strict=True):
        assert not got["incomplete"], (formula, got, want)
        if isinstance(want, list):
            assert (
                got["kind"] == 5 and got["columns"] == 1 and got["rows"] == len(want)
            ), (formula, got, want)
            assert [v["value"] for v in got["items"]] == want, (formula, got, want)
        else:
            assert got["kind"] == 1 and math.isclose(
                got["value"], want, rel_tol=3e-11, abs_tol=5e-324
            ), (formula, got, want)
    return {
        "cases": len(cases),
        "precision_digits": mp.mp.dps,
        "sample_arithmetic": "exact rational over actual binary64 inputs",
    }


def retained(binary):
    names = "prob ftest f.test ttest t.test ztest z.test chitest chisq.test".split()
    paths = [
        ROOT / "sheets/tests/data/fods/statistical/fods" / (n + ".fods") for n in names
    ]
    paths.append(ROOT / "sheets/tests/data/fods/array/fods/frequency.fods")
    rows = json.loads(run(binary, "--inference-fods", files=paths))
    report = {
        "cases": len(rows),
        "agreeing": 0,
        "error_label_differences": 0,
        "unavailable": 0,
        "differences": [],
    }
    for row in rows:
        got = row["actual"]
        if got["incomplete"]:
            report["unavailable"] += 1
            report["differences"].append(row)
        elif row["expected_error"] and got["kind"] == 4:
            report[
                "agreeing"
                if got["display"] == row["expected_text"]
                else "error_label_differences"
            ] += 1
        elif (
            "expected_value" in row
            and got["kind"] == 1
            and math.isclose(
                got["value"], row["expected_value"], rel_tol=3e-9, abs_tol=1e-14
            )
        ):
            report["agreeing"] += 1
        else:
            report["differences"].append(row)

    def raw(path, positions):
        table_ns = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
        office = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
        table = list(ET.parse(path).getroot().iter(table_ns + "table"))[1]
        result = {p: None for p in positions}
        r = 1
        for row in table.findall(table_ns + "table-row"):
            nr = int(row.get(table_ns + "number-rows-repeated", "1"))
            c = 1
            for cell in row.findall(table_ns + "table-cell"):
                nc = int(cell.get(table_ns + "number-columns-repeated", "1"))
                for rr, cc in positions:
                    if r <= rr < r + nr and c <= cc < c + nc:
                        assert cell.get(table_ns + "formula") is None
                        result[(rr, cc)] = (
                            float(cell.get(office + "value"))
                            if cell.get(office + "value-type") == "float"
                            else None
                        )
                c += nc
            r += nr
        return result

    seen = set()
    for row in report["differences"]:
        key = (Path(row["file"]).stem, row["sheet"], row["row"], row["column"])
        if key in {("t.test", 1, 2, 1), ("t.test", 1, 6, 1)}:
            cells = raw(row["file"], [(r, c) for r in range(1, 12) for c in (6, 7)])
            differences = [F(cells[(r, 6)]) - F(cells[(r, 7)]) for r in range(1, 12)]
            assert len(set(differences)) == 1 and differences[0] != 0
            assert row["expected_value"] == 0 and row["actual"]["display"] == "#DIV/0!"
            row["reason"] = (
                "Exact paired differences have zero variance, so the t statistic divides by zero. The native error convention is retained instead of Calc's limiting zero cache."
            )
        elif key == ("z.test", 1, 15, 1):
            cells = raw(row["file"], [(1, c) for c in (6, 7, 8)])
            z = (real(cells[(1, 6)]) - real(cells[(1, 7)])) / real(cells[(1, 8)])
            oracle = float(mp.erfc(z / mp.sqrt(2)) / 2)
            assert row["actual"]["value"] == oracle == 1 and row["expected_error"]
            row["independent_reference"] = oracle
            row["reason"] = (
                "Modern known-sigma Z.TEST is defined on a single observation; its independently evaluated normal survival probability rounds to one. ODF ZTEST still requires two observations."
            )
        elif key in {("chitest", 1, 23, 1), ("chisq.test", 1, 23, 1)}:
            cells = raw(
                row["file"], [(r, c) for r in range(18, 22) for c in range(6, 9)]
            )
            assert all(v is None for v in cells.values())
            assert row["actual"]["value"] == 1 and row["expected_error"]
            row["reason"] = (
                "All paired entries are empty and ignored; the ODF chi-square sum is zero with geometry df=2, giving survival one. Calc caches an input error; this boundary difference remains explicit."
            )
        else:
            raise AssertionError(row)
        seen.add(key)
    assert seen == {
        ("t.test", 1, 2, 1),
        ("t.test", 1, 6, 1),
        ("z.test", 1, 15, 1),
        ("chitest", 1, 23, 1),
        ("chisq.test", 1, 23, 1),
    }, seen
    assert (
        report["cases"],
        report["agreeing"],
        report["error_label_differences"],
        report["unavailable"],
    ) == (229, 214, 10, 0)
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = {
        "independent": independent(args.test_bin),
        "retained": retained(args.test_bin),
    }
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print("PASS independent inference", result["independent"], flush=True)
    print(
        "Retained inference",
        {k: v if k != "differences" else len(v) for k, v in result["retained"].items()},
        flush=True,
    )
