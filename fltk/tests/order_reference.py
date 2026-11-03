#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Audit retained order-statistic caches and independent exact rational cases."""

import argparse
from collections import Counter
from fractions import Fraction as F
import json
import math
import os
from pathlib import Path
import random
import subprocess

ROOT = Path(__file__).resolve().parents[2]
NAMES = {
    "small",
    "large",
    "rank",
    "rank.eq",
    "rank.avg",
    "percentile",
    "percentile.inc",
    "percentile.exc",
    "percentrank",
    "percentrank.inc",
    "percentrank.exc",
    "quartile",
    "quartile.inc",
    "quartile.exc",
    "mode",
    "mode.sngl",
    "mode.mult",
}
# Keep exact cache disagreements visible. Do not alter the original fixtures.
# Calc rounded these ranks; native/retained Calligra use decimal truncation,
# matching Microsoft's published INC/EXC examples. The singleton EXC convention
# and pre-2018 sorted MODE.MULT caches are separate interoperability differences.
RANK_DIFFERENCES = {
    "percentrank": {
        2: (0.6667, 0.6666),
        3: (0.667, 0.666),
        4: (0.887, 0.886),
        7: (0.4, 0.3),
        9: (0.67, 0.66),
        10: (0.667, 0.666),
        12: (0.758, 0.757),
        14: (0.556, 0.555),
        15: (0.667, 0.666),
    },
    "percentrank.inc": {
        2: (0.6667, 0.6666),
        3: (0.667, 0.666),
        4: (0.887, 0.886),
        7: (0.4, 0.3),
        9: (0.67, 0.66),
        10: (0.667, 0.666),
        12: (0.758, 0.757),
        14: (0.556, 0.555),
        15: (0.667, 0.666),
        25: (0.4, 0.3),
        27: (0.556, 0.555),
        28: (0.667, 0.666),
    },
    "percentrank.exc": {
        4: (0.874, 0.873),
        5: (0.0172, 0.017),
        12: (0.655, 0.654),
        13: (0.364, 0.363),
        18: (1, 0.5),
        25: (0.4, 0.3),
    },
    "mode.mult": {17: (2, 4), 18: (3, 2), 19: (4, 3)},
    "rank": {6: (5, 6)},
}


def run(binary, mode, text=None, files=()):
    return subprocess.check_output(
        [str(binary.resolve()), mode, *map(str, files)],
        input=text,
        text=True,
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
    )


def percentile(data, p, exclusive=False):
    data = sorted(map(F, data))
    pos = p * (len(data) + 1) - 1 if exclusive else p * (len(data) - 1)
    lo = pos.numerator // pos.denominator
    return float(
        data[lo]
        if lo == len(data) - 1
        else data[lo] + (data[lo + 1] - data[lo]) * (pos - lo)
    )


def percentrank(data, x, exclusive, digits):
    data = sorted(data)
    first = data.index(x) if x in data else None
    if first is None:
        high = next(i for i, v in enumerate(data) if v > x)
        low = high - 1
        rank = F(low + exclusive) + F(x - data[low]) / F(data[high] - data[low])
    else:
        rank = F(first + exclusive)
    rank /= len(data) + 1 if exclusive else len(data) - 1
    scale = 10**digits
    return float((rank * scale).__floor__() / F(scale))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    paths = sorted(
        p
        for p in (ROOT / "sheets/tests/data/fods/statistical/fods").glob("*.fods")
        if p.stem in NAMES
    )
    rows = json.loads(run(args.test_bin, "--fods", files=paths))
    failures = []
    unavailable = []
    deviations = []
    for r in rows:
        got = r["actual"]
        expected = r.get("expected_value")
        name = Path(r["file"]).stem
        if got["incomplete"]:
            unavailable.append(r)
            continue
        pair = (
            RANK_DIFFERENCES.get(name, {}).get(r["row"]) if r["column"] == 1 else None
        )
        if pair:
            assert expected == pair[0] and got.get("value") == pair[1], r
            r["reason"] = (
                "Inherited binary duration subtraction/INT boundary changes M7 from 1054 to 1053; rank follows recomputed inputs."
                if name == "rank"
                else (
                    "OOXML modes retain encounter order; these old caches sorted the remaining modes."
                    if name == "mode.mult"
                    else (
                        "Exclusive singleton convention differs: native uses 1/(n+1)."
                        if name == "percentrank.exc" and r["row"] == 18
                        else "Decimal truncation versus Calc rounded result cache."
                    )
                )
            )
            deviations.append(r)
            continue
        valid = (
            got["kind"] == 4
            if r["expected_error"]
            else (
                got["kind"] == 1
                and math.isclose(
                    got.get("value", 0), expected, rel_tol=1e-9, abs_tol=1e-9
                )
                if expected is not None
                else got["display"] == r["expected_text"]
            )
        )
        if not valid:
            failures.append(r)
    assert len(rows) == 336 and len(unavailable) == 3 and len(deviations) == 31, (
        len(rows),
        len(unavailable),
        len(deviations),
    )
    cases = []
    rng = random.Random(20261103)
    for _ in range(60):
        data = [rng.randint(-5, 8) for _ in range(rng.randint(2, 24))]
        ordered = sorted(data)
        n = len(data)
        x = rng.choice(data)
        k = rng.randint(1, n)
        source = "{" + ",".join(map(str, data)) + "}"

        def add(name, arguments, value):
            cases.append(
                (
                    name + "(" + source + ("," + arguments if arguments else "") + ")",
                    value,
                )
            )

        add("SMALL", str(k), ordered[k - 1])
        add("LARGE", str(k), ordered[n - k])
        add("SMALL", str(k) + ".9", ordered[k - 1])
        for ascending in (False, True):
            rank = 1 + sum(v < x if ascending else v > x for v in data)
            for name, expected in [
                ("RANK", rank),
                ("RANK.AVG", rank + (data.count(x) - 1) / 2),
            ]:
                cases.append((f"{name}({x},{source},{int(ascending)})", expected))
        for p in (F(0), F(1, 4), F(1, 2), F(1)):
            add("PERCENTILE", str(float(p)), percentile(data, p))
        add("PERCENTILE.EXC", ".5", percentile(data, F(1, 2), True))
        add("QUARTILE", "1", percentile(data, F(1, 4)))
        add("QUARTILE.EXC", "2", percentile(data, F(1, 2), True))
        add("MEDIAN", "", percentile(data, F(1, 2)))
        for exclusive in (False, True):
            for query in (F(x), F(min(data) + max(data), 2)):
                digits = rng.randint(1, 7)
                add(
                    "PERCENTRANK.EXC" if exclusive else "PERCENTRANK",
                    f"{float(query)},{digits}",
                    percentrank(data, query, exclusive, digits),
                )
        counts = Counter(data)
        maximum = max(counts.values())
        modes = [v for v in dict.fromkeys(data) if counts[v] == maximum]
        add("MODE", "", min(modes) if maximum > 1 else "#N/A")
        add("MODE.SNGL", "", modes[0] if maximum > 1 else "#N/A")
        add("MODE.MULT", "", modes if maximum > 1 else "#N/A")
    for scale in (1e-300, 1e-100, 1, 1e100, 1e300):
        data = [float(i * scale) for i in (-8, -2, 1, 7)]
        source = "{" + ",".join(format(v, ".17g") for v in data) + "}"
        exact = [F(v) for v in data]
        for p in (F(0), F(1, 4), F(1, 2), F(7, 8), F(1)):
            cases.append((f"PERCENTILE({source},{float(p)})", percentile(exact, p)))
    output = run(
        args.test_bin, "--eval", "".join(formula + "\n" for formula, _ in cases)
    )

    def valid(got, want):
        if isinstance(want, list):
            return (
                got["kind"] == 5
                and got["rows"] == len(want)
                and got["columns"] == 1
                and all(valid(g, v) for g, v in zip(got["items"], want, strict=True))
            )
        if isinstance(want, str):
            return got["display"] == want and not got["incomplete"]
        return (
            got["kind"] == 1
            and not got["incomplete"]
            and math.isclose(got["value"], want, rel_tol=2e-12, abs_tol=0)
        )

    for (formula, want), line in zip(cases, output.splitlines(), strict=True):
        got = json.loads(line)
        if not valid(got, want):
            failures.append({"formula": formula, "expected": want, "actual": got})
    report = {
        "retained_cases_including_matrix_children": len(rows),
        "agreeing_retained_caches": len(rows)
        - len(deviations)
        - len(unavailable)
        - sum("file" in f for f in failures),
        "independent_exact_rational_cases": len(cases),
        "documented_differences": deviations,
        "unavailable_dependencies": unavailable,
        "failures": failures,
        "fixture_copy_normalization": "Only proven-empty styled tails beyond the sheet limit; original fixtures unchanged.",
        "sources": [
            "https://docs.oasis-open.org/office/OpenDocument/v1.3/os/part4-formula/OpenDocument-v1.3-os-part4-formula.html",
            "https://support.microsoft.com/en-us/excel/functions/percentrank-inc-function",
            "https://support.microsoft.com/en-us/excel/functions/percentrank-exc-function",
            "https://support.microsoft.com/en-us/excel/functions/mode-mult-function",
            "https://github.com/LibreOffice/core/blob/master/sc/source/core/tool/interpr3.cxx",
        ],
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    assert not failures, failures[:5]
    print(
        f'PASS {report["agreeing_retained_caches"]} retained result caches and {len(cases)} independent cases; 31 documented cache differences and three unavailable dependencies retained separately'
    )


if __name__ == "__main__":
    main()
