#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Audit active retained function registrations against native signature dispatch.

An arity probe identifies catalogue coverage only; it does not prove numerical
or interoperability fidelity. Comments and quoted strings are distinguished so
disabled registrations and dotted/u-prefixed names are counted correctly.
"""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def uncomment(source):
    return re.sub(
        r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',
        lambda m: m[0] if m[0].startswith('"') else "",
        source,
    )


def inventory(test_bin):
    modules = {}
    for path in sorted((ROOT / "sheets/functions").glob("*.cpp")):
        source = uncomment(path.read_text())
        names = set(re.findall(r'new\s+Function\s*\(\s*u?"([A-Z0-9_.]+)"', source))
        names.update(re.findall(r'setAlternateName\(\s*u?"([A-Z0-9_.]+)"', source))
        if names:
            modules[path.stem] = sorted(names)
    names = sorted(set(name for group in modules.values() for name in group))
    output = subprocess.check_output(
        [str(test_bin.resolve()), "--eval"],
        input="".join(name + "()\n" for name in names),
        text=True,
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
    )
    values = [json.loads(line) for line in output.splitlines()]
    assert len(values) == len(names)
    covered = {
        name for name, value in zip(names, values) if value["display"] != "#NAME?"
    }
    result = {
        "scope": "active registrations and alternate names; signature coverage only",
        "registered_names": len(names),
        "covered_names": len(covered),
        "remaining_names": len(set(names) - covered),
        "modules": {},
    }
    for module, names in modules.items():
        result["modules"][module] = {
            "registered": len(names),
            "covered": sorted(set(names) & covered),
            "remaining": sorted(set(names) - covered),
        }
    new = {
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
    assert new <= covered
    assert new <= set(modules["statistical"])
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = inventory(args.test_bin)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(
        "PASS active registration inventory:",
        result["covered_names"],
        "native names,",
        result["remaining_names"],
        "remaining",
    )
