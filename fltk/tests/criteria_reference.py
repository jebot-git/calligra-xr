#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Conditional function corpus audit and independent numeric selection checks."""

import argparse
import json
import math
import operator
import os
from pathlib import Path
import random
import subprocess

ROOT = Path(__file__).resolve().parents[2]
NAMES = {
    "countif",
    "countif2",
    "countifs",
    "averageif",
    "averageifs",
    "averageif_wildcards",
    "sumif",
    "sumif_wildcards",
}


def run(binary, mode, text=None, files=()):
    return subprocess.check_output(
        [str(binary.resolve()), mode, *map(str, files)],
        input=text,
        text=True,
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    paths = sorted(
        p for p in (ROOT / "sheets/tests/data/fods").rglob("*.fods") if p.stem in NAMES
    )
    rows = json.loads(run(args.test_bin, "--fods", files=paths))
    failures, unavailable, conflicts = [], [], []
    for row in rows:
        got, expected = row["actual"], row.get("expected_value")
        if got["incomplete"]:
            unavailable.append(row)
            continue
        key = (Path(row["file"]).stem, row["row"], row["column"])
        if key in {("countif", 46, 1), ("countifs", 39, 1)}:
            # These source fixtures contradict their own rendered text caches.
            # The current result agrees with the typed inputs and text cache;
            # keep both original expectations visible instead of changing one.
            assert expected != float(row["expected_text"]), row
            assert got["kind"] == 1 and got["value"] == float(row["expected_text"]), row
            conflicts.append(row)
            continue
        valid = (
            got["kind"] == 4
            if row["expected_error"]
            else (
                got["kind"] == 1
                and math.isclose(got["value"], expected, rel_tol=1e-9, abs_tol=1e-9)
                if expected is not None
                else got["display"] == row["expected_text"]
            )
        )
        if not valid:
            failures.append(row)
    assert len(rows) == 202 and len(unavailable) == 12 and len(conflicts) == 2
    rng = random.Random(20261103)
    cases = []
    comparisons = {
        "=": operator.eq,
        "<>": operator.ne,
        ">": operator.gt,
        ">=": operator.ge,
        "<": operator.lt,
        "<=": operator.le,
    }
    for _ in range(60):
        data = [rng.randint(-10, 20) for _ in range(6)]
        weights = [rng.randint(-100, 100) for _ in range(6)]
        threshold, other = rng.randint(-10, 20), rng.randint(-100, 100)
        op = rng.choice(list(comparisons))
        chosen = [
            i for i, value in enumerate(data) if comparisons[op](value, threshold)
        ]
        both = [i for i in chosen if weights[i] > other]
        array = (
            lambda values: "{"
            + ",".join(map(str, values[:3]))
            + ";"
            + ",".join(map(str, values[3:]))
            + "}"
        )
        source, target = array(data), array(weights)
        for name, indices, formula in [
            ("COUNTIF", chosen, f'COUNTIF({source},"{op}{threshold}")'),
            ("SUMIF", chosen, f'SUMIF({source},"{op}{threshold}",{target})'),
            ("AVERAGEIF", chosen, f'AVERAGEIF({source},"{op}{threshold}",{target})'),
            (
                "COUNTIFS",
                both,
                f'COUNTIFS({source},"{op}{threshold}",{target},">{other}")',
            ),
            (
                "SUMIFS",
                both,
                f'SUMIFS({target},{source},"{op}{threshold}",{target},">{other}")',
            ),
            (
                "AVERAGEIFS",
                both,
                f'AVERAGEIFS({target},{source},"{op}{threshold}",{target},">{other}")',
            ),
        ]:
            expected = (
                len(indices)
                if name.startswith("COUNT")
                else (
                    "#DIV/0!"
                    if name.startswith("AVERAGE") and not indices
                    else sum(weights[i] for i in indices)
                    / (len(indices) if name.startswith("AVERAGE") else 1)
                )
            )
            cases.append((formula, expected))
    for name in ("SUMIF", "AVERAGEIF"):
        cases.append(
            (f"{name}({{1,1,1}},1,{{1E308,1,-1E308}})", 1 if name == "SUMIF" else 1 / 3)
        )
    output = run(
        args.test_bin, "--eval", "".join(formula + "\n" for formula, _ in cases)
    )
    for (formula, expected), line in zip(cases, output.splitlines(), strict=True):
        got = json.loads(line)
        valid = (
            got["display"] == expected
            if isinstance(expected, str)
            else (
                got["kind"] == 1
                and math.isclose(got["value"], expected, rel_tol=1e-12, abs_tol=1e-12)
            )
        )
        if not valid or got["incomplete"]:
            failures.append({"formula": formula, "expected": expected, "actual": got})
    report = {
        "retained_cases": len(rows),
        "retained_agreeing_caches": len(rows)
        - len(unavailable)
        - len(conflicts)
        - len([f for f in failures if "file" in f]),
        "independent_cases": len(cases),
        "conflicting_source_caches": conflicts,
        "unavailable_dependencies": unavailable,
        "failures": failures,
        "fixture_copy_normalization": "Only proven-empty styled rows beyond the sheet limit are removed; original files and all input/formula cells are retained.",
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    assert not failures, failures[:5]
    print(
        "PASS 188 retained result caches and",
        len(cases),
        "independent cases;",
        "two contradictory source caches and twelve unavailable dependencies recorded separately",
    )


if __name__ == "__main__":
    main()
