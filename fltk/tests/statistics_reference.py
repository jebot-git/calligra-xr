#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Retained statistical corpus and independent high precision arithmetic checks."""

import argparse
import ast
from collections import Counter
from decimal import Decimal as D, localcontext
import json
import math
import os
from pathlib import Path
import random
import re
import subprocess
from sheets_function_inventory import uncomment

ROOT = Path(__file__).resolve().parents[2]
NAMES = {
    "AVERAGEA",
    "AVEDEV",
    "DEVSQ",
    "DEVSQA",
    "GEOMEAN",
    "HARMEAN",
    "STDEVA",
    "STDEVPA",
    "VARA",
    "VARPA",
}


def reference(name, values):
    with localcontext() as context:
        context.prec = 800
        values = [D.from_float(value) for value in values]
        count = D(len(values))
        mean = sum(values) / count
        squares = sum((value - mean) ** 2 for value in values)
        if name == "AVERAGEA":
            result = mean
        elif name == "AVEDEV":
            result = sum(abs(value - mean) for value in values) / count
        elif name in ("DEVSQ", "DEVSQA"):
            result = squares
        elif name == "GEOMEAN":
            # Test data are positive; Decimal product/root is independent of
            # the native logarithmic implementation.
            product = D(1)
            for value in values:
                product *= value
            result = product ** (1 / count)
        elif name == "HARMEAN":
            result = count / sum(1 / value for value in values)
        else:
            result = squares / (count - (name in ("VARA", "STDEVA")))
            if name.startswith("STDEV"):
                result = result.sqrt()
        result = float(result)
        return result if math.isfinite(result) else "#NUM!"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    retained = []
    source = uncomment((ROOT / "sheets/tests/TestStatisticalFunctions.cpp").read_text())
    for match in re.finditer(
        r'CHECK_EVAL(_SHORT)?\(("(?:\\.|[^"\\])*")\s*,\s*(.*?)\);', source, re.S
    ):
        formula = ast.literal_eval(match[2])
        name = re.match(r"\s*([A-Z.]+)\(", formula)
        if not name or name[1] not in NAMES:
            continue
        raw = match[3].strip()
        if raw.startswith("Value::error"):
            expected = {"NUM": "#NUM!", "VALUE": "#VALUE!", "DIV0": "#DIV/0!"}[
                re.search(r"error(\w+)\(", raw)[1]
            ]
        else:
            assert raw.startswith("Value(") and raw.endswith(")"), raw
            expected = float(raw[6:-1])
        retained.append(
            (formula.replace(";", ","), expected, 1e-6 if match[1] else 2e-9)
        )
    assert len(retained) >= 35, len(retained)
    env = {**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"}
    output = subprocess.check_output(
        [str(args.test_bin.resolve()), "--retained-eval"],
        input="".join(formula + "\n" for formula, _, _ in retained),
        text=True,
        env=env,
    )
    deviations, failures = [], []
    for (formula, expected, tolerance), line in zip(
        retained, output.splitlines(), strict=True
    ):
        got = json.loads(line)
        # Preserve the native engine's #DIV/0! convention for insufficient
        # sample size, also used by its existing STDEV/VAR functions.
        if expected == "#NUM!" and re.fullmatch(r"(STDEVA|VARA)\(1\)", formula):
            deviations.append(
                {"formula": formula, "retained": expected, "native": "#DIV/0!"}
            )
            expected = "#DIV/0!"
        valid = (
            got["display"] == expected
            if isinstance(expected, str)
            else (
                got["kind"] == 1
                and math.isclose(
                    got["value"], expected, rel_tol=tolerance, abs_tol=tolerance
                )
            )
        )
        if not valid or got["incomplete"]:
            failures.append({"formula": formula, "got": got, "expected": expected})
    assert len(deviations) == 2, deviations
    randomizer = random.Random(20261103)
    cases = []
    for _ in range(40):
        scale = randomizer.choice([1e-300, 1e-100, 1, 1e100, 1e300])
        values = [
            float(randomizer.randint(1, 99) * scale)
            for _ in range(randomizer.randint(2, 10))
        ]
        for name in sorted(NAMES):
            cases.append(
                (
                    name + "({" + ",".join(format(v, ".17g") for v in values) + "})",
                    reference(name, values),
                )
            )
    for origin in (1e12, -1e12, 1e15):
        values = [origin + value for value in [1, 2, 3, 5, 8]]
        for name in sorted(NAMES - {"GEOMEAN", "HARMEAN"}):
            cases.append(
                (
                    name + "({" + ",".join(format(v, ".17g") for v in values) + "})",
                    reference(name, values),
                )
            )
    output = subprocess.check_output(
        [str(args.test_bin.resolve()), "--eval"],
        input="".join(formula + "\n" for formula, _ in cases),
        text=True,
        env=env,
    )
    for (formula, expected), line in zip(cases, output.splitlines(), strict=True):
        got = json.loads(line)
        valid = (
            got["display"] == expected
            if isinstance(expected, str)
            else (
                got["kind"] == 1
                and math.isclose(got["value"], expected, rel_tol=2e-12, abs_tol=0)
            )
        )
        if not valid or got["incomplete"]:
            failures.append({"formula": formula, "got": got, "expected": expected})
    report = {
        "retained_cases": len(retained),
        "independent_cases": len(cases),
        "retained_error_code_differences": deviations,
        "failures": failures,
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    assert not failures, failures[:5]
    print(
        "PASS",
        len(retained),
        "retained cases and",
        len(cases),
        "independent decimal references;",
        len(deviations),
        "documented error-code differences",
    )


if __name__ == "__main__":
    main()
